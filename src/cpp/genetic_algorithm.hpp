#pragma once

#include "hardware_mapper.hpp"
#include <vector>
#include <functional>
#include <random>
#include <cstdint>

namespace spartan7 {

// ─────────────────────────────────────────────
//  Хромосома — кодування одного розміщення
// ─────────────────────────────────────────────
struct Chromosome {
    // Кожен ген — індекс вибраного ресурсу або стилю реалізації
    std::vector<int> genes;

    // Метрики особини (заповнюються під час оцінки)
    double timing_score = 0.0;  // нормована [0,1]: менше затримка → більше
    double power_score  = 0.0;  // нормована [0,1]: менше споживання → більше
    double area_score   = 0.0;  // нормована [0,1]: менше зайнятих ресурсів → більше
    double fitness      = 0.0;  // зважена сума

    bool operator<(const Chromosome& o) const { return fitness > o.fitness; } // сортування: більший fitness — кращий
};

// ─────────────────────────────────────────────
//  Параметри генетичного алгоритму
// ─────────────────────────────────────────────
struct GAParameters {
    int    population_size  = 100;
    int    max_generations  = 50;
    double mutation_rate    = 0.15;
    double crossover_rate   = 0.80;
    int    elite_count      = 5;    // кількість еліт, що переходять без змін
    int    tournament_k     = 3;    // розмір турнірної вибірки

    // Ваги фітнес-функції (сума = 1.0)
    double weight_speed  = 0.60;
    double weight_power  = 0.20;
    double weight_area   = 0.20;

    bool   verbose       = false;   // виводити прогрес по поколіннях
};

// ─────────────────────────────────────────────
//  Опис задачі для ГА
// ─────────────────────────────────────────────
struct GATask {
    int    num_genes;           // кількість модулів/рішень для кодування
    int    gene_range;          // кількість варіантів на один ген (0..gene_range-1)

    // Функція обчислення реальних метрик для конкретної хромосоми
    // Повертає {timing_ns, power_mw, area_lut_equiv}
    std::function<std::tuple<double,double,double>(const Chromosome&)> evaluate_fn;
};

// ─────────────────────────────────────────────
//  Статистика виконання
// ─────────────────────────────────────────────
struct GAStatistics {
    int    total_generations  = 0;
    double best_fitness       = 0.0;
    double avg_fitness        = 0.0;
    double initial_fitness    = 0.0;
    double improvement_pct    = 0.0; // (best - initial) / initial * 100
    int    convergence_gen    = -1;  // покоління де досягнуто 99% від фінального
};

// ─────────────────────────────────────────────
//  Генетичний алгоритм
// ─────────────────────────────────────────────
class GeneticAlgorithm {
public:
    explicit GeneticAlgorithm(const GAParameters& params = {});

    // Запуск оптимізації; повертає найкращу хромосому
    Chromosome optimize(const GATask& task);

    // Статистика останнього запуску
    const GAStatistics& getStatistics() const { return stats_; }

    // Доступ до всього фінального населення (для аналізу)
    const std::vector<Chromosome>& getPopulation() const { return population_; }

private:
    GAParameters            params_;
    std::vector<Chromosome> population_;
    GAStatistics            stats_;
    std::mt19937            rng_;

    // Ключові оператори
    void   initializePopulation(const GATask& task);
    void   evaluatePopulation(const GATask& task,
                               double max_timing, double max_power, double max_area);
    double computeFitness(const Chromosome& c) const;
    Chromosome tournamentSelect() const;
    Chromosome singlePointCrossover(const Chromosome& a, const Chromosome& b);
    void       mutate(Chromosome& c, int gene_range);
    void       elitePreserve(std::vector<Chromosome>& next_gen);

    // Нормалізація метрик по всій популяції
    void normalizeMetrics(double max_timing, double max_power, double max_area);
};

} // namespace spartan7
