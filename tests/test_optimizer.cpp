/**
 * test_optimizer.cpp
 * Юніт-тести бібліотеки (без зовнішніх фреймворків — тільки assert)
 * Перевіряє: HardwareMapper, GeneticAlgorithm, ConstraintSolver, ModuleRegistry
 */

#include "spartan7_optimizer.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace spartan7;

// ─────────────────────────────────────────────
static int passed = 0, failed = 0;

#define TEST(name)  std::cout << "  [TEST] " name "... "
#define PASS()      { std::cout << "PASS\n"; ++passed; }
#define FAIL(msg)   { std::cout << "FAIL: " << (msg) << "\n"; ++failed; }
#define ASSERT_TRUE(cond)  if (!(cond)) { FAIL(#cond); return; }
#define ASSERT_EQ(a, b)    if ((a) != (b)) { FAIL(#a " != " #b); return; }
#define ASSERT_GT(a, b)    if (!((a) > (b))) { FAIL(std::to_string(a) + " not > " + std::to_string(b)); return; }

// ─────────────────────────────────────────────
void test_hardware_mapper() {
    std::cout << "\n── HardwareMapper ─────────────────────────────\n";
    HardwareMapper mapper("xc7s50-1fgg484");

    TEST("MUX(2,1) — базовий маппінг"); {
        auto m = mapper.mapMuxToSlice(2, 1);
        ASSERT_TRUE(m.is_valid);
        ASSERT_GT(m.lut_count, 0);
        ASSERT_GT(m.timing_ns, 0.0);
        PASS();
    }

    TEST("MUX(8,4) — 8-входовий 4-bit"); {
        auto m = mapper.mapMuxToSlice(8, 4);
        ASSERT_TRUE(m.is_valid);
        ASSERT_GT(m.lut_count, 3);
        PASS();
    }

    TEST("BlockRAM mapping"); {
        auto m = mapper.mapMemoryBlock(8 * 1024 * 8, 8);  // 8KB
        ASSERT_TRUE(m.is_valid);
        ASSERT_GT(m.bram_count, 0);
        PASS();
    }

    TEST("Distributed RAM (малий)"); {
        auto m = mapper.mapMemoryBlock(64 * 8, 8);  // 64 bytes → distributed
        ASSERT_TRUE(m.is_valid);
        ASSERT_EQ(m.bram_count, 0);  // має бути без BRAM
        PASS();
    }

    TEST("DSP mapping"); {
        auto m = mapper.mapDSP(18, 18);
        ASSERT_TRUE(m.is_valid);
        ASSERT_EQ(m.dsp_count, 1);
        PASS();
    }

    TEST("ALU 8-bit"); {
        auto m = mapper.mapALU(8, true);
        ASSERT_TRUE(m.is_valid);
        ASSERT_EQ(m.lut_count, 8);
        PASS();
    }

    TEST("Дані про використання ресурсів"); {
        auto u = mapper.getResourceUsage();
        ASSERT_GT(u.lut_used, 0);
        ASSERT_EQ(u.lut_total, 32600);  // xc7s50
        PASS();
    }

    TEST("checkFit()"); {
        ASSERT_TRUE(mapper.checkFit());
        PASS();
    }

    TEST("Ланцюжок затримок"); {
        HardwareMapper m2("xc7s50-1fgg484");
        auto mux = m2.mapMuxToSlice(4, 8);
        auto alu = m2.mapALU(8, false);
        double lat = m2.calculatePathLatency({mux, alu});
        ASSERT_GT(lat, 0.5);
        PASS();
    }
}

// ─────────────────────────────────────────────
void test_genetic_algorithm() {
    std::cout << "\n── GeneticAlgorithm ───────────────────────────\n";

    GAParameters p;
    p.population_size  = 30;
    p.max_generations  = 20;
    p.mutation_rate    = 0.15;
    p.verbose          = false;

    GeneticAlgorithm ga(p);

    TEST("Базова оптимізація"); {
        GATask task;
        task.num_genes  = 4;
        task.gene_range = 3;
        task.evaluate_fn = [](const Chromosome& c) {
            double t = 0, pw = 0, area = 0;
            for (int g : c.genes) { t += g; pw += (2 - g); area += 1; }
            return std::make_tuple(t, pw, area);
        };
        Chromosome best = ga.optimize(task);
        ASSERT_EQ(static_cast<int>(best.genes.size()), 4);
        ASSERT_GT(best.fitness, 0.0);
        PASS();
    }

    TEST("Статистика після ГА"); {
        const auto& stats = ga.getStatistics();
        ASSERT_EQ(stats.total_generations, 20);
        ASSERT_GT(stats.best_fitness, 0.0);
        PASS();
    }

    TEST("Популяція після оптимізації"); {
        ASSERT_EQ(static_cast<int>(ga.getPopulation().size()), 30);
        PASS();
    }

    TEST("Перший елемент — найкращий (sorted)"); {
        const auto& pop = ga.getPopulation();
        ASSERT_TRUE(pop[0].fitness >= pop[1].fitness);
        PASS();
    }
}

// ─────────────────────────────────────────────
void test_constraint_solver() {
    std::cout << "\n── ConstraintSolver ───────────────────────────\n";

    ConstraintSolver solver("xc7s50-1fgg484", 10.0);  // 100 МГц → 10 ns

    HardwareMapper mapper("xc7s50-1fgg484");
    auto m1 = mapper.mapMuxToSlice(4, 8);
    auto m2 = mapper.mapALU(8, false);

    TEST("Timing OK для малих модулів"); {
        std::vector<std::pair<int,int>> conns = {{0, 1}};
        auto r = solver.checkTimingConstraints({m1, m2}, conns);
        // затримка mux(~1ns) + alu(~1ns) + routing(0.15) < 10ns
        ASSERT_TRUE(r.passed);
        PASS();
    }

    TEST("Area check OK"); {
        AreaBudget budget;
        budget.lut_budget = 10000;
        auto r = solver.checkAreaConstraints({m1, m2}, budget);
        ASSERT_TRUE(r.passed);
        PASS();
    }

    TEST("Area violation detection"); {
        AreaBudget tight;
        tight.lut_budget = 1;  // тільки 1 LUT — має порушуватись
        auto r = solver.checkAreaConstraints({m1, m2}, tight);
        ASSERT_TRUE(!r.passed);
        ASSERT_TRUE(!r.violations.empty());
        PASS();
    }

    TEST("buildTimingPath"); {
        auto path = solver.buildTimingPath({m1, m2});
        ASSERT_GT(path.total_delay_ns, 0.0);
        ASSERT_EQ(static_cast<int>(path.nodes.size()), 2);
        ASSERT_TRUE(path.meets_timing);
        PASS();
    }
}

// ─────────────────────────────────────────────
void test_module_registry() {
    std::cout << "\n── ModuleRegistry ─────────────────────────────\n";
    auto& reg = ModuleRegistry::instance();

    TEST("Базові модулі зареєстровані"); {
        ASSERT_GT(reg.totalCount(), 5);
        PASS();
    }

    TEST("queryByName MUX_GENERIC"); {
        auto info = reg.queryByName("MUX_GENERIC");
        ASSERT_TRUE(info.has_value());
        ASSERT_EQ(info->type, ModuleType::MUX);
        PASS();
    }

    TEST("queryModule (MUX, LUT_BASED)"); {
        ModuleQuery q;
        q.type      = ModuleType::MUX;
        q.preferred = ImplementationStyle::AUTO;
        auto info   = reg.queryModule(q);
        ASSERT_TRUE(info.has_value());
        PASS();
    }

    TEST("getAlternatives BRAM36K"); {
        auto alts = reg.getAlternatives("BRAM36K");
        ASSERT_GT(static_cast<int>(alts.size()), 0);
        PASS();
    }

    TEST("aggregateRequirements"); {
        auto req = reg.aggregateRequirements({"MUX_GENERIC", "DSP48E1"});
        ASSERT_GT(req.min_dsp, 0);
        PASS();
    }

    TEST("Ручна реєстрація"); {
        ModuleInfo custom;
        custom.name = "MY_CUSTOM_IP";
        custom.type = ModuleType::CUSTOM;
        custom.requirements = {.min_lut = 50};
        reg.registerModule(custom);
        auto q = reg.queryByName("MY_CUSTOM_IP");
        ASSERT_TRUE(q.has_value());
        ASSERT_EQ(q->requirements.min_lut, 50);
        PASS();
    }
}

// ─────────────────────────────────────────────
void test_spartan7_optimizer() {
    std::cout << "\n── Spartan7Optimizer (інтеграційний) ──────────\n";

    TEST("Ініціалізація з відомим пристроєм"); {
        Spartan7Optimizer opt("xc7s50-1fgg484");
        PASS();
    }

    TEST("Невідомий пристрій — не крашиться (fallback)"); {
        // xc7s999 буде невідомим, але mapper не ломається
        Spartan7Optimizer opt("xc7s999-1fgg484");
        auto m = opt.createMUX(2, 1);
        ASSERT_TRUE(m.id > 0);
        PASS();
    }

    TEST("createMUX повертає валідний handle"); {
        Spartan7Optimizer opt("xc7s50-1fgg484");
        auto m = opt.createMUX(8, 4);
        ASSERT_GT(m.id, 0);
        ASSERT_EQ(m.type, ModuleType::MUX);
        ASSERT_EQ(static_cast<int>(m.inputs.size()), 8);
        PASS();
    }

    TEST("connect не крашиться"); {
        Spartan7Optimizer opt("xc7s50-1fgg484");
        auto m1 = opt.createMUX(4, 8);
        auto m2 = opt.createALU(8);
        opt.connect(m1.output, m2.inputs[0]);
        PASS();
    }

    TEST("compile() завершується (Vivado не потрібен)"); {
        Spartan7Optimizer opt("xc7s50-1fgg484");
        opt.createMUX(4, 8);
        opt.createALU(8, true);
        // Очікуємо DONE або FAILED (якщо system() недоступний)
        auto r = opt.compile();
        bool acceptable = (r.status == BuildStatus::DONE
                        || r.status == BuildStatus::FAILED);
        ASSERT_TRUE(acceptable);
        PASS();
    }

    TEST("reset() очищає стан"); {
        Spartan7Optimizer opt("xc7s50-1fgg484");
        opt.createMUX(4, 8);
        opt.reset();
        auto u = opt.getResourceUsage();
        ASSERT_EQ(u.lut_used, 0);
        PASS();
    }
}

// ─────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║  Spartan-7 Library Unit Tests            ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    test_hardware_mapper();
    test_genetic_algorithm();
    test_constraint_solver();
    test_module_registry();
    test_spartan7_optimizer();

    std::cout << "\n════════════════════════════════════════════\n";
    std::cout << "  Результат: " << passed << " passed, " << failed << " failed\n";
    std::cout << "════════════════════════════════════════════\n";

    return (failed == 0) ? 0 : 1;
}
