#pragma once

#include "hardware_mapper.hpp"
#include <vector>
#include <string>
#include <optional>

namespace spartan7 {

// ─────────────────────────────────────────────
//  Вузол на timing-шляху
// ─────────────────────────────────────────────
struct PathNode {
    std::string component_name;
    std::string resource_site;  // "SLICE_X5Y10"
    double      arrival_time_ns = 0.0;
    double      required_time_ns = 0.0;
    double      slack_ns         = 0.0;  // required - arrival
};

// ─────────────────────────────────────────────
//  Критичний timing-шлях
// ─────────────────────────────────────────────
struct TimingPath {
    std::vector<PathNode> nodes;
    double total_delay_ns  = 0.0;
    double wns_ns          = 0.0; // Worst Negative Slack
    double tns_ns          = 0.0; // Total Negative Slack
    bool   meets_timing    = true;

    // Критичний вузол (найменший slack)
    std::optional<PathNode> criticalNode() const;
};

// ─────────────────────────────────────────────
//  Граф маршрутизації (спрощена матриця суміжності)
// ─────────────────────────────────────────────
struct RoutingGraph {
    int                       num_nodes  = 0;
    std::vector<std::vector<double>> adj; // adj[i][j] = затримка нету (ns), -1 якщо немає зв'язку
    std::vector<int>          congestion; // рівень завантаженості [0..100]

    bool isConnected(int from, int to) const;
    double routingDelay(int from, int to) const; // Dijkstra
    int    maxCongestion() const;
};

// ─────────────────────────────────────────────
//  Бюджет площі
// ─────────────────────────────────────────────
struct AreaBudget {
    int lut_budget   = 0;   // максимально допустимо LUT
    int ff_budget    = 0;
    int bram_budget  = 0;
    int dsp_budget   = 0;

    // Поточні витрати
    int lut_used   = 0;
    int ff_used    = 0;
    int bram_used  = 0;
    int dsp_used   = 0;

    bool isLUToverfill()  const { return lut_budget  > 0 && lut_used  > lut_budget;  }
    bool isBRAMoverfill() const { return bram_budget > 0 && bram_used > bram_budget; }
    bool isDSPoverfill()  const { return dsp_budget  > 0 && dsp_used  > dsp_budget;  }
    bool anyOverfill()    const { return isLUToverfill() || isBRAMoverfill() || isDSPoverfill(); }
};

// ─────────────────────────────────────────────
//  Результат перевірки обмежень
// ─────────────────────────────────────────────
struct ConstraintResult {
    bool   passed     = true;
    std::string reason;         // опис першого порушення
    std::vector<std::string> violations; // усі порушення

    explicit operator bool() const { return passed; }
};

// ─────────────────────────────────────────────
//  Вирішувач обмежень
// ─────────────────────────────────────────────
class ConstraintSolver {
public:
    explicit ConstraintSolver(const std::string& device_id,
                               double clock_period_ns = 10.0);

    // ── Перевірка обмежень ──────────────────
    ConstraintResult checkTimingConstraints(
        const std::vector<OptimalMapping>& mappings,
        const std::vector<std::pair<int,int>>& connections) const;

    ConstraintResult checkRoutingFeasibility(
        const std::vector<OptimalMapping>& mappings) const;

    ConstraintResult checkAreaConstraints(
        const std::vector<OptimalMapping>& mappings,
        const AreaBudget& budget) const;

    // Перевірка всіх одночасно; повертає першу невдачу або Ok
    ConstraintResult checkAll(
        const std::vector<OptimalMapping>& mappings,
        const std::vector<std::pair<int,int>>& connections,
        const AreaBudget& budget) const;

    // ── Аналіз timing-шляху ─────────────────
    TimingPath buildTimingPath(
        const std::vector<OptimalMapping>& chain) const;

    // ── Налаштування ────────────────────────
    void setClockPeriod(double ns)     { clock_period_ns_ = ns; }
    void setRoutingGraph(RoutingGraph g) { routing_graph_ = std::move(g); }

private:
    std::string   device_id_;
    double        clock_period_ns_;
    RoutingGraph  routing_graph_;

    // Оцінка затримки міжз'єднання між двома маппінгами
    double estimateInterconnectDelay(const OptimalMapping& src,
                                      const OptimalMapping& dst) const;
};

} // namespace spartan7
