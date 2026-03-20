/**
 * basic_usage.cpp
 * Демонстрація повного циклу роботи з Spartan7 Dynamic Optimization Library:
 *   MUX → DEMUX → BlockRAM → FIFO → ALU → DSP → compile() → program()
 */

#include "spartan7_optimizer.hpp"
#include <iostream>
#include <iomanip>

using namespace spartan7;

// ─────────────────────────────────────────────
//  Callback прогресу (виводить у консоль)
// ─────────────────────────────────────────────
static void onProgress(double pct, const std::string& msg) {
    std::cout << "[" << std::setw(3) << static_cast<int>(pct * 100) << "%] "
              << msg << "\n";
}

int main() {
    std::cout << "╔══════════════════════════════════════════════╗\n"
              << "║  Spartan-7 Dynamic Optimization Library      ║\n"
              << "║  xc7s50-1fgg484  |  Example: signal router   ║\n"
              << "╚══════════════════════════════════════════════╝\n\n";

    // ── 1. Ініціалізація оптимізатора ─────────────────────────
    GAParameters ga_params;
    ga_params.population_size = 100;
    ga_params.max_generations = 50;
    ga_params.mutation_rate   = 0.15;
    ga_params.weight_speed    = 0.60;
    ga_params.weight_power    = 0.20;
    ga_params.weight_area     = 0.20;
    ga_params.verbose         = false;

    Spartan7Optimizer optimizer("xc7s50-1fgg484", ga_params);
    optimizer.setClockFrequency(100.0);  // 100 МГц

    // ── 2. Визначення модулів ──────────────────────────────────
    auto mux_8_to_2 = optimizer.createMUX(8, 4);      // 8 входів, 4-bit шина
    auto mux_16_to_4 = optimizer.createMUX(16, 4);    // 16 входів, 4-bit
    auto demux_1_to_8 = optimizer.createDEMUX(8, 4);  // 1→8, 4-bit

    auto ram_8k = optimizer.createBlockRAM(8192, 8);   // 8 KB
    auto fifo   = optimizer.createFIFO(256, 8);        // FIFO 256×8

    auto alu  = optimizer.createALU(8, true);           // 8-bit ALU з carry
    auto dsp  = optimizer.createDSP48E1(18, 18, 48);   // 18×18→48 accumulator

    std::cout << "Модулів визначено: 7\n";
    std::cout << "  • mux_8_to_2   : MUX(8:1) × 4-bit\n";
    std::cout << "  • mux_16_to_4  : MUX(16:1) × 4-bit\n";
    std::cout << "  • demux_1_to_8 : DEMUX(1:8) × 4-bit\n";
    std::cout << "  • ram_8k       : BlockRAM 8 KB\n";
    std::cout << "  • fifo         : FIFO 256×8-bit\n";
    std::cout << "  • alu          : ALU 8-bit з carry\n";
    std::cout << "  • dsp          : DSP48E1 18×18→48\n\n";

    // ── 3. З'єднання модулів ───────────────────────────────────
    //  mux_8_to_2.Y → mux_16_to_4.I0
    optimizer.connect(mux_8_to_2.output, mux_16_to_4.inputs[0]);
    //  mux_16_to_4.Y → alu.A
    optimizer.connect(mux_16_to_4.output, alu.inputs[0]);
    //  alu.RESULT → fifo.DIN
    optimizer.connect(alu.output, fifo.inputs[0]);
    //  fifo.DOUT  → demux_1_to_8.I
    optimizer.connect(fifo.output, demux_1_to_8.inputs[0]);

    std::cout << "З'єднань: 4\n\n";

    // ── 4. Обмеження площі ────────────────────────────────────
    AreaBudget budget;
    budget.lut_budget  = 20000;  // 60% від xc7s50
    budget.bram_budget = 50;
    budget.dsp_budget  = 10;
    optimizer.setAreaBudget(budget);

    // ── 5. Компіляція з ГА-оптимізацією ──────────────────────
    std::cout << "Запуск compile()...\n";
    BuildResult result = optimizer.compile(onProgress);

    // ── 6. Звіт ───────────────────────────────────────────────
    std::cout << "\n" << optimizer.getOptimizationReport();
    std::cout << "\nTiming: " << optimizer.getTimingReport();

    auto usage = optimizer.getResourceUsage();
    std::cout << "\n── Підсумок ресурсів ──────────────────────────\n";
    std::cout << "  LUTs : " << usage.lut_used  << "/" << usage.lut_total
              << " (" << std::fixed << std::setprecision(1)
              << usage.lut_utilization() << "%)\n";
    std::cout << "  BRAMs: " << usage.bram_used << "/" << usage.bram_total << "\n";
    std::cout << "  DSPs : " << usage.dsp_used  << "/" << usage.dsp_total  << "\n";

    std::cout << "\n── GA статистика ───────────────────────────────\n";
    std::cout << "  Покращення: " << result.ga_stats.improvement_pct << "%\n";
    std::cout << "  Best fitness: " << result.ga_stats.best_fitness    << "\n";
    std::cout << "  Покоління збіжності: " << result.ga_stats.convergence_gen << "\n";

    if (result.status == BuildStatus::DONE) {
        std::cout << "\n✓ Компіляцію завершено успішно за "
                  << result.total_time_s << " с\n";
        std::cout << "  Бітстрім: " << result.bitstream_path << "\n";
    } else {
        std::cout << "\n✗ Помилка: " << result.message << "\n";
        return 1;
    }

    // ── 7. Прошивання (якщо Vivado + JTAG доступні) ──────────
    // BuildResult prog = optimizer.program("auto");
    // std::cout << "Прошивання: " << prog.message << "\n";

    return 0;
}
