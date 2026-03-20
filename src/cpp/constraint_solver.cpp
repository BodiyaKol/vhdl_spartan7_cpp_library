#include "constraint_solver.hpp"
#include <sstream>
#include <algorithm>
#include <limits>
#include <queue>

namespace spartan7 {

// ─────────────────────────────────────────
//  TimingPath helpers
// ─────────────────────────────────────────
std::optional<PathNode> TimingPath::criticalNode() const {
    if (nodes.empty()) return std::nullopt;
    auto it = std::min_element(nodes.begin(), nodes.end(),
        [](const PathNode& a, const PathNode& b) {
            return a.slack_ns < b.slack_ns;
        });
    return *it;
}

// ─────────────────────────────────────────
//  RoutingGraph helpers
// ─────────────────────────────────────────
bool RoutingGraph::isConnected(int from, int to) const {
    if (from < 0 || to < 0 || from >= num_nodes || to >= num_nodes) return false;
    return adj[from][to] >= 0.0;
}

double RoutingGraph::routingDelay(int from, int to) const {
    if (from == to) return 0.0;
    // Dijkstra
    std::vector<double> dist(num_nodes, std::numeric_limits<double>::infinity());
    dist[from] = 0.0;
    using PD = std::pair<double,int>;
    std::priority_queue<PD, std::vector<PD>, std::greater<PD>> pq;
    pq.push({0.0, from});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (int v = 0; v < num_nodes; ++v) {
            if (adj[u][v] < 0) continue;
            double nd = dist[u] + adj[u][v];
            if (nd < dist[v]) { dist[v] = nd; pq.push({nd, v}); }
        }
    }
    return dist[to];
}

int RoutingGraph::maxCongestion() const {
    if (congestion.empty()) return 0;
    return *std::max_element(congestion.begin(), congestion.end());
}

// ─────────────────────────────────────────
//  ConstraintSolver
// ─────────────────────────────────────────
ConstraintSolver::ConstraintSolver(const std::string& device_id,
                                    double clock_period_ns)
    : device_id_(device_id), clock_period_ns_(clock_period_ns) {}

double ConstraintSolver::estimateInterconnectDelay(
        const OptimalMapping& src, const OptimalMapping& dst) const {
    // Спрощена модель: 0.15 ns базова + 0.05 ns на кожен 1 BRAM відстані
    (void)src; (void)dst;
    return 0.15;
}

ConstraintResult ConstraintSolver::checkTimingConstraints(
        const std::vector<OptimalMapping>& mappings,
        const std::vector<std::pair<int,int>>& connections) const {
    ConstraintResult result;

    for (const auto& [from_idx, to_idx] : connections) {
        if (from_idx < 0 || to_idx < 0
            || from_idx >= static_cast<int>(mappings.size())
            || to_idx   >= static_cast<int>(mappings.size())) {
            result.violations.push_back("Invalid connection index");
            continue;
        }
        const OptimalMapping& src = mappings[from_idx];
        const OptimalMapping& dst = mappings[to_idx];
        double path_delay = src.timing_ns
                          + estimateInterconnectDelay(src, dst)
                          + dst.timing_ns;
        if (path_delay > clock_period_ns_) {
            std::ostringstream ss;
            ss << "Timing violation on path [" << from_idx << " -> " << to_idx
               << "]: " << path_delay << " ns > " << clock_period_ns_ << " ns";
            result.violations.push_back(ss.str());
        }
    }
    if (!result.violations.empty()) {
        result.passed = false;
        result.reason = result.violations.front();
    }
    return result;
}

ConstraintResult ConstraintSolver::checkRoutingFeasibility(
        const std::vector<OptimalMapping>& mappings) const {
    ConstraintResult result;
    if (routing_graph_.num_nodes > 0 && routing_graph_.maxCongestion() > 90) {
        result.passed = false;
        result.reason = "Routing congestion exceeds 90% — placement infeasible";
        result.violations.push_back(result.reason);
    }
    (void)mappings;
    return result;
}

ConstraintResult ConstraintSolver::checkAreaConstraints(
        const std::vector<OptimalMapping>& mappings,
        const AreaBudget& budget) const {
    ConstraintResult result;
    AreaBudget used = budget;
    used.lut_used = used.ff_used = used.bram_used = used.dsp_used = 0;

    for (const auto& m : mappings) {
        used.lut_used  += m.lut_count;
        used.ff_used   += m.ff_count;
        used.bram_used += m.bram_count;
        used.dsp_used  += m.dsp_count;
    }
    auto check = [&](bool over, const std::string& name) {
        if (over) {
            result.violations.push_back(name + " budget exceeded");
        }
    };
    check(used.isLUToverfill(),  "LUT");
    check(used.isBRAMoverfill(), "BRAM");
    check(used.isDSPoverfill(),  "DSP");

    if (!result.violations.empty()) {
        result.passed = false;
        result.reason = result.violations.front();
    }
    return result;
}

ConstraintResult ConstraintSolver::checkAll(
        const std::vector<OptimalMapping>& mappings,
        const std::vector<std::pair<int,int>>& connections,
        const AreaBudget& budget) const {
    for (auto checker : {
             checkTimingConstraints(mappings, connections),
             checkRoutingFeasibility(mappings),
             checkAreaConstraints(mappings, budget)
         }) {
        if (!checker) return checker;
    }
    return {};  // all passed
}

TimingPath ConstraintSolver::buildTimingPath(
        const std::vector<OptimalMapping>& chain) const {
    TimingPath path;
    double arrival = 0.0;
    for (size_t i = 0; i < chain.size(); ++i) {
        PathNode node;
        node.component_name  = "module_" + std::to_string(i);
        node.arrival_time_ns = arrival;
        node.required_time_ns = clock_period_ns_;
        arrival += chain[i].timing_ns;
        if (i + 1 < chain.size()) {
            arrival += estimateInterconnectDelay(chain[i], chain[i+1]);
        }
        node.slack_ns = node.required_time_ns - arrival;
        path.nodes.push_back(node);
    }
    path.total_delay_ns = arrival;
    path.wns_ns = 0.0;
    for (const auto& n : path.nodes) path.wns_ns = std::min(path.wns_ns, n.slack_ns);
    path.meets_timing = (path.wns_ns >= 0.0);
    return path;
}

} // namespace spartan7
