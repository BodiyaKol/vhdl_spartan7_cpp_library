#include "genetic_algorithm.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <iostream>
#include <cassert>

namespace spartan7 {

GeneticAlgorithm::GeneticAlgorithm(const GAParameters& params)
    : params_(params), rng_(std::random_device{}()) {}

// ─────────────────────────────────────────
//  Публічний інтерфейс
// ─────────────────────────────────────────
Chromosome GeneticAlgorithm::optimize(const GATask& task) {
    if (task.num_genes <= 0 || task.gene_range <= 0) {
        throw std::invalid_argument("GATask: num_genes and gene_range must be > 0");
    }

    initializePopulation(task);

    // Первинна оцінка для статистики
    double max_t = 1e-9, max_p = 1e-9, max_a = 1e-9;
    for (auto& c : population_) {
        auto [t, p, a] = task.evaluate_fn(c);
        c.timing_score = t; c.power_score = p; c.area_score = a;
        max_t = std::max(max_t, t);
        max_p = std::max(max_p, p);
        max_a = std::max(max_a, a);
    }
    normalizeMetrics(max_t, max_p, max_a);
    for (auto& c : population_) c.fitness = computeFitness(c);
    std::sort(population_.begin(), population_.end());

    stats_.initial_fitness = population_.front().fitness;
    int convergence_threshold_gen = -1;

    // ── Головний цикл ──────────────────────
    for (int gen = 0; gen < params_.max_generations; ++gen) {
        std::vector<Chromosome> next_gen;
        next_gen.reserve(params_.population_size);

        // Елітизм
        elitePreserve(next_gen);

        // Кросовер + мутація
        while (static_cast<int>(next_gen.size()) < params_.population_size) {
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            Chromosome child;
            if (prob(rng_) < params_.crossover_rate) {
                Chromosome p1 = tournamentSelect();
                Chromosome p2 = tournamentSelect();
                child = singlePointCrossover(p1, p2);
            } else {
                child = tournamentSelect();
            }
            if (prob(rng_) < params_.mutation_rate) {
                mutate(child, task.gene_range);
            }
            next_gen.push_back(std::move(child));
        }

        // Оцінка нового покоління
        max_t = max_p = max_a = 1e-9;
        for (auto& c : next_gen) {
            auto [t, p, a] = task.evaluate_fn(c);
            c.timing_score = t; c.power_score = p; c.area_score = a;
            max_t = std::max(max_t, t);
            max_p = std::max(max_p, p);
            max_a = std::max(max_a, a);
        }
        normalizeMetrics(max_t, max_p, max_a);  // нормалізуємо вже next_gen через population_
        for (auto& c : next_gen) c.fitness = computeFitness(c);
        population_ = std::move(next_gen);
        std::sort(population_.begin(), population_.end());

        double best = population_.front().fitness;
        double avg  = std::accumulate(population_.begin(), population_.end(), 0.0,
                        [](double s, const Chromosome& c){ return s + c.fitness; })
                      / population_.size();

        if (params_.verbose) {
            std::cout << "[GA] gen " << gen
                      << "  best=" << best << "  avg=" << avg << "\n";
        }

        // Перевірка збіжності (99% від найкращого)
        if (convergence_threshold_gen < 0 && best >= 0.99 * population_.front().fitness) {
            convergence_threshold_gen = gen;
        }
    }

    // ── Заповнення статистики ───────────────
    stats_.total_generations = params_.max_generations;
    stats_.best_fitness      = population_.front().fitness;
    stats_.avg_fitness       = std::accumulate(population_.begin(), population_.end(), 0.0,
                                [](double s, const Chromosome& c){ return s + c.fitness; })
                               / population_.size();
    if (stats_.initial_fitness > 0.0) {
        stats_.improvement_pct = (stats_.best_fitness - stats_.initial_fitness)
                                  / stats_.initial_fitness * 100.0;
    }
    stats_.convergence_gen = convergence_threshold_gen;

    return population_.front();
}

// ─────────────────────────────────────────
//  Приватні методи
// ─────────────────────────────────────────
void GeneticAlgorithm::initializePopulation(const GATask& task) {
    population_.clear();
    population_.reserve(params_.population_size);
    std::uniform_int_distribution<int> gene_dist(0, task.gene_range - 1);

    for (int i = 0; i < params_.population_size; ++i) {
        Chromosome c;
        c.genes.resize(task.num_genes);
        for (auto& g : c.genes) g = gene_dist(rng_);
        population_.push_back(std::move(c));
    }
}

double GeneticAlgorithm::computeFitness(const Chromosome& c) const {
    // timing_score, power_score, area_score вже нормовані [0,1]
    // де 1.0 = найкраще (менша затримка/споживання/площа)
    return params_.weight_speed * c.timing_score
         + params_.weight_power * c.power_score
         + params_.weight_area  * c.area_score;
}

void GeneticAlgorithm::normalizeMetrics(double max_t, double max_p, double max_a) {
    // Нормалізуємо поточну популяцію: 1.0 = найкращий (найменший)
    for (auto& c : population_) {
        c.timing_score = (max_t > 0) ? (1.0 - c.timing_score / max_t) : 1.0;
        c.power_score  = (max_p > 0) ? (1.0 - c.power_score  / max_p) : 1.0;
        c.area_score   = (max_a > 0) ? (1.0 - c.area_score   / max_a) : 1.0;
    }
}

Chromosome GeneticAlgorithm::tournamentSelect() const {
    std::uniform_int_distribution<int> idx(0, static_cast<int>(population_.size()) - 1);
    Chromosome best = population_[idx(const_cast<std::mt19937&>(rng_))];
    for (int i = 1; i < params_.tournament_k; ++i) {
        const Chromosome& c = population_[idx(const_cast<std::mt19937&>(rng_))];
        if (c.fitness > best.fitness) best = c;
    }
    return best;
}

Chromosome GeneticAlgorithm::singlePointCrossover(const Chromosome& a, const Chromosome& b) {
    assert(a.genes.size() == b.genes.size());
    std::uniform_int_distribution<int> cut(1, static_cast<int>(a.genes.size()) - 1);
    int point = cut(rng_);
    Chromosome child;
    child.genes.insert(child.genes.end(), a.genes.begin(), a.genes.begin() + point);
    child.genes.insert(child.genes.end(), b.genes.begin() + point, b.genes.end());
    return child;
}

void GeneticAlgorithm::mutate(Chromosome& c, int gene_range) {
    std::uniform_int_distribution<int> gene_idx(0, static_cast<int>(c.genes.size()) - 1);
    std::uniform_int_distribution<int> gene_val(0, gene_range - 1);
    c.genes[gene_idx(rng_)] = gene_val(rng_);
}

void GeneticAlgorithm::elitePreserve(std::vector<Chromosome>& next_gen) {
    int count = std::min(params_.elite_count, static_cast<int>(population_.size()));
    for (int i = 0; i < count; ++i) {
        next_gen.push_back(population_[i]); // population_ вже відсортована
    }
}

} // namespace spartan7
