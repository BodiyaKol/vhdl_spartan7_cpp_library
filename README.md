# Spartan-7 Dynamic Hardware Optimization Library

Інтегрована C++/Python/VHDL бібліотека для **автоматичної оптимізації** розміщення логічних модулів на FPGA **Xilinx Spartan-7** за допомогою **генетичних алгоритмів** та аналізу реальних характеристик кристалу.

---

## Навіщо це потрібно?

Розробник, що пише RTL вручну, вирішує задачу розміщення логіки інтуїтивно. Цей підхід не оптимальний: один і той самий MUX на 16 входів може бути реалізований через LUT-дерево, через SRL-зсувні регістри або через Block RAM — і кожен варіант дає різний компроміс між **швидкістю**, **споживанням** та **зайнятою площею**.

Ця бібліотека вирішує задачу **автоматично**:

1. Розробник описує схему на рівні логічних блоків (`MUX`, `DEMUX`, `RAM`, `FIFO`, `ALU`, `DSP`)
2. Генетичний алгоритм перебирає всі комбінації реалізацій та знаходить оптимальну
3. VHDL-файли генеруються автоматично з правильними параметрами
4. Vivado синтезує, розміщує та генерує бітстрім

---

## Архітектура проєкту

```
vhdl_spartan7_cpp_library/
├── src/
│   ├── cpp/                    # C++ ядро
│   │   ├── hardware_mapper     # Маппінг логіки → фізичні ресурси FPGA
│   │   ├── genetic_algorithm   # Еволюційний пошук оптимуму
│   │   ├── constraint_solver   # Перевірка timing/area/routing обмежень
│   │   ├── module_registry     # Реєстр підтримуваних модулів
│   │   └── spartan7_optimizer  # Публічний C++ API
│   ├── python/                 # Python оркестратор
│   │   ├── spartan7_optimizer  # Головний Python API + CLI
│   │   ├── vivado_bridge       # Комунікація з Vivado (TCL + звіти)
│   │   ├── hardware_analyzer   # Профайлінг пристрою
│   │   └── code_generator      # Генерація VHDL та C++ параметрів
│   └── vhdl/                   # VHDL шаблони
│       ├── mux_generic.vhdl    # MUX: LUT / SRL / BRAM реалізації
│       ├── demux_generic.vhdl  # DEMUX: комбінаційний / зареєстрований
│       ├── memory_controller   # SDP BRAM, Distributed RAM, FIFO
│       ├── wrapper_template    # AXI4-Lite slave обгортка
│       └── vivado_tcl/
│           ├── device_analyzer # Аналіз ресурсів кристалу
│           ├── synthesis       # Синтез + Place & Route
│           └── bitstream_gen   # Генерація бітстріму та прошивання
├── examples/
│   └── basic_usage.cpp         # Повний приклад використання
├── tests/
│   └── test_optimizer.cpp      # Юніт-тести без зовнішніх залежностей
└── CMakeLists.txt
```

---

## Підтримувані пристрої Spartan-7

| Part Number      | LUT    | FF      | BRAM (36Kb) | DSP48E1 |
|------------------|--------|---------|-------------|---------|
| xc7s6-*          | 3 750  | 7 500   | 5           | 10      |
| xc7s15-*         | 8 000  | 16 000  | 20          | 40      |
| xc7s25-*         | 14 600 | 29 200  | 45          | 80      |
| **xc7s50-***     | 32 600 | 65 200  | 75          | 120     |
| xc7s75-*         | 47 000 | 94 000  | 135         | 150     |
| xc7s100-*        | 64 000 | 128 000 | 200         | 160     |

---

## Підтримувані логічні модулі

| Модуль      | Стилі реалізації              | Опис                                   |
|-------------|-------------------------------|----------------------------------------|
| `MUX`       | LUT / SRL / BRAM              | Мультиплексор 2..256 входів, 1..64 bit |
| `DEMUX`     | LUT / registered              | Демультиплексор                        |
| `RAM`       | BRAM-36K / Distributed RAM    | Single/Dual-Port, до 200 блоків        |
| `FIFO`      | BRAM (FWFT або standard)      | З сигналами full/empty/count           |
| `ALU`       | LUT + CARRY4                  | ADD/SUB/AND/OR/XOR/NOT, з carry        |
| `MUL`       | DSP48E1 / LUT                 | Множник, авто-вибір                    |
| `DSP48E1`   | DSP48E1 primitive             | 25×18 MAC, 48-bit акумулятор           |

---

## Генетичний алгоритм

Для кожного набору модулів GA шукає оптимальне **поєднання стилів реалізації**.

**Фітнес-функція:**
```
fitness = speed × 0.60 + power × 0.20 + area × 0.20
```

| Параметр          | Значення за замовч. | Опис                              |
|-------------------|---------------------|-----------------------------------|
| `population_size` | 100                 | Особин у популяції                |
| `max_generations` | 50                  | Максимум поколінь                 |
| `mutation_rate`   | 0.15                | Імовірність мутації гена          |
| `crossover_rate`  | 0.80                | Імовірність кросоверу             |
| `elite_count`     | 5                   | Елітні особини без змін           |
| `tournament_k`    | 3                   | Розмір турнірної вибірки          |

---

## Приклад використання (C++)

```cpp
#include "spartan7_optimizer.hpp"
using namespace spartan7;

int main() {
    // Ініціалізація оптимізатора для xc7s50, 100 МГц
    Spartan7Optimizer optimizer("xc7s50-1fgg484");
    optimizer.setClockFrequency(100.0);

    // Визначення логічних модулів
    auto mux      = optimizer.createMUX(8, 4);       // MUX 8→1, 4-bit шина
    auto ram      = optimizer.createBlockRAM(8192, 8);// 8 KB BlockRAM
    auto fifo     = optimizer.createFIFO(256, 8);     // FIFO 256×8
    auto alu      = optimizer.createALU(8, true);     // 8-bit ALU з carry
    auto dsp      = optimizer.createDSP48E1(18, 18);  // 18×18→48 accumulator

    // З'єднання між модулями
    optimizer.connect(mux.output, alu.inputs[0]);
    optimizer.connect(alu.output, fifo.inputs[0]);

    // Обмеження площі (опційно)
    AreaBudget budget;
    budget.lut_budget = 20000;
    optimizer.setAreaBudget(budget);

    // Запуск: аналіз → GA → VHDL → Vivado синтез
    BuildResult result = optimizer.compile([](double pct, const std::string& msg){
        printf("[%3.0f%%] %s\n", pct * 100, msg.c_str());
    });

    // Звіт
    printf("%s\n", optimizer.getOptimizationReport().c_str());

    // Прошивання (потребує підключеного JTAG)
    optimizer.program();
}
```

## Приклад використання (Python)

```python
from spartan7_optimizer import Spartan7Optimizer

opt = Spartan7Optimizer("xc7s50-1fgg484", clock_mhz=100.0)

# Визначення модулів
opt.add_mux(inputs=8,  data_width=4, name="sel_mux")
opt.add_ram(depth=1024, data_width=8, name="buf_ram")
opt.add_fifo(depth=256, data_width=8, name="tx_fifo")
opt.add_dsp(a_width=18, b_width=18,  name="mac_unit")

# З'єднання
opt.connect("sel_mux", "Y", "tx_fifo", "DIN")

# Компіляція з відображенням прогресу
result = opt.compile(progress=lambda p, m: print(f"[{p:.0%}] {m}"))
print(result.report)
```

---

## Збірка

**Вимоги:** GCC ≥ 10 або Clang ≥ 12, CMake ≥ 3.16, Python ≥ 3.9

```bash
# Клонування та збірка
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Запуск тестів
ctest --test-dir build --output-on-failure

# Запуск прикладу
./build/basic_usage
```

**Для роботи з Vivado** (синтез та прошивання):
```bash
# Переконайтесь що Vivado в PATH
source /opt/Xilinx/Vivado/2023.2/settings64.sh

# Аналіз ресурсів пристрою
vivado -mode batch -source src/vhdl/vivado_tcl/device_analyzer.tcl \
       -tclargs xc7s50-1fgg484 build/device_analysis

# Синтез + P&R
vivado -mode batch -source src/vhdl/vivado_tcl/synthesis.tcl \
       -tclargs xc7s50-1fgg484 top_wrapper 100.0 build

# Генерація бітстріму + прошивання
vivado -mode batch -source src/vhdl/vivado_tcl/bitstream_gen.tcl \
       -tclargs build auto
```

---

## Робочий процес

```
  C++ код (createMUX, createRAM, ...)
           │
           ▼
  Python аналізує вимоги до ресурсів
           │
           ▼
  Vivado TCL → характеристики кристалу (LUT, BRAM, DSP, timing)
           │
           ▼
  Генетичний алгоритм (C++) → оптимальна конфігурація
  fitness = speed×0.6 + power×0.2 + area×0.2
           │
           ▼
  CodeGenerator → .vhd файли для кожного модуля
           │
           ▼
  Vivado: synthesis → place & route → DRC → bitstream
           │
           ▼
  Program device через JTAG
```

---

## Ліцензія

MIT License — дивіться [LICENSE](LICENSE)

In this project we are implementing library on cpp language for spartan7 microcontroller.
