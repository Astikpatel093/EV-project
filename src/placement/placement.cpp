// placement.cpp - benefit estimation and budgeted site selection.
#include "ev/placement/placement.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

#include "ev/algorithms/branch_and_bound.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/number_theory.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/core/timer.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/planner/energy_model.hpp"

namespace ev {

const char *to_string(PlacementMethod m) {
    switch (m) {
    case PlacementMethod::Greedy: return "greedy";
    case PlacementMethod::DynamicProgramming: return "dp";
    case PlacementMethod::BranchBoundLC: return "bnb-lc";
    case PlacementMethod::BranchBoundFIFO: return "bnb-fifo";
    default: return "bnb-lifo";
    }
}

bool parse_placement(const std::string &s, PlacementMethod &out) {
    for (int i = 0; i < kPlacementMethods; i++) {
        PlacementMethod m = static_cast<PlacementMethod>(i);
        if (s == to_string(m)) { out = m; return true; }
    }
    return false;
}

BenefitTable candidate_benefits(const Scenario &s, const EnergyModel &model) {
    BenefitTable t;
    const std::size_t nc = s.candidates.size();
    t.benefit.assign(nc, 0);
    t.estimated.assign(nc, false);
    bool any_missing = false;
    for (std::size_t i = 0; i < nc; i++) {
        if (s.candidates[i].benefit) t.benefit[i] = *s.candidates[i].benefit;
        else any_missing = true;
    }
    if (!any_missing) return t;
    // pass[node] = weighted number of trips whose shortest road route passes the node
    const int n = model.node_count();
    Graph g(n, true);
    for (const RoadArc &a : model.arcs()) g.add_edge(a.from, a.to, a.km);
    std::vector<std::int64_t> pass(static_cast<std::size_t>(n), 0);
    std::vector<std::unique_ptr<ShortestPathTree>> trees(static_cast<std::size_t>(n));
    for (const TripRequest &tr : s.trips) {
        const int a = s.network.index_of(tr.from), b = s.network.index_of(tr.to);
        const int vi = s.vehicle_index(tr.vehicle_id);
        if (a < 0 || b < 0 || vi < 0) continue;
        auto &tree = trees[static_cast<std::size_t>(a)];
        if (!tree) tree = std::make_unique<ShortestPathTree>(dijkstra(g, a));
        if (!tree->reachable(b)) continue;
        const ElectricVehicle &v = s.vehicles[static_cast<std::size_t>(vi)];
        std::vector<int> arcs = tree->edges_to(b);
        const bool direct_ok = model.simulate(arcs, tr.start_charge_kwh.value_or(v.charge_kwh), v).ok;
        const std::int64_t w = direct_ok ? 1 : 4;
        for (int node : tree->path_to(b)) pass[static_cast<std::size_t>(node)] += w;
    }
    for (std::size_t i = 0; i < nc; i++) {
        if (s.candidates[i].benefit) continue;
        const int pos = s.network.index_of(s.candidates[i].node);
        double b = pos >= 0 ? static_cast<double>(pass[static_cast<std::size_t>(pos)]) : 0.0;
        if (s.candidates[i].suitability) b *= *s.candidates[i].suitability;
        t.benefit[i] = static_cast<std::int64_t>(std::llround(b));
        t.estimated[i] = true;
    }
    return t;
}

PlacementResult place_stations(const Scenario &s, const std::vector<std::int64_t> &benefits, PlacementMethod method,
                               const PlacementConstraints &con, std::optional<Paise> budget_opt) {
    Stopwatch clock;
    const std::size_t n = s.candidates.size();
    if (benefits.size() != n) throw std::invalid_argument("one benefit value is needed per candidate");
    const Paise budget = budget_opt.value_or(s.config.budget);
    if (budget < 0) throw std::invalid_argument("budget must not be negative");
    for (std::size_t i = 0; i < n; i++) {
        if (s.candidates[i].install_cost <= 0) throw std::invalid_argument("candidate " + std::to_string(s.candidates[i].id) + " has a non-positive cost");
        if (benefits[i] < 0) throw std::invalid_argument("candidate " + std::to_string(s.candidates[i].id) + " has a negative benefit");
        if (benefits[i] > 1000000000000LL)   // keeps every sum of benefits far from int64 overflow
            throw std::invalid_argument("candidate " + std::to_string(s.candidates[i].id) + " has an unrealistically large benefit");
    }
    std::vector<char> locked(n, 0), banned(n, 0);
    for (int i : con.locked) {
        if (i < 0 || static_cast<std::size_t>(i) >= n) throw std::invalid_argument("locked candidate index out of range");
        locked[static_cast<std::size_t>(i)] = 1;
    }
    for (int i : con.banned) {
        if (i < 0 || static_cast<std::size_t>(i) >= n) throw std::invalid_argument("banned candidate index out of range");
        if (!locked[static_cast<std::size_t>(i)]) banned[static_cast<std::size_t>(i)] = 1;
    }

    PlacementResult r;
    r.method = method;
    r.budget = budget;
    Paise lock_cost = 0;
    for (std::size_t i = 0; i < n; i++)
        if (locked[i]) {
            // saturate instead of overflowing: anything above the budget is "over budget" anyway
            const Paise c = s.candidates[i].install_cost;
            lock_cost = lock_cost > budget ? lock_cost : (c > budget - lock_cost + 1 ? budget + 1 : lock_cost + c);
        }
    const Paise left = budget - lock_cost;
    r.over_budget = left < 0;

    // items offered to the solver: not locked, not banned, positive benefit, affordable
    std::vector<std::size_t> items;
    for (std::size_t i = 0; i < n; i++)
        if (!locked[i] && !banned[i] && benefits[i] > 0 && left >= 0 && s.candidates[i].install_cost <= left) items.push_back(i);

    std::vector<bool> take(items.size(), false);
    if (!items.empty()) {
        // scale costs by their gcd with the budget: exact, and keeps the DP table small
        std::int64_t g = left;
        for (std::size_t i : items) g = gcd(g, s.candidates[i].install_cost);
        if (g <= 0) g = 1;
        std::vector<std::int64_t> w, v;
        for (std::size_t i : items) {
            w.push_back(s.candidates[i].install_cost / g);
            v.push_back(benefits[i]);
        }
        const std::int64_t W = left / g;
        PlacementMethod m = method;
        if (m == PlacementMethod::DynamicProgramming) {
            try {
                KnapsackResult k = knapsack_01(w, v, W, 50000000);
                take = k.take;
                r.optimal = true;
            } catch (const std::length_error &) {
                m = PlacementMethod::BranchBoundLC;
                r.note = "dynamic-programming table too large (" + std::to_string(items.size()) + " items x " +
                         std::to_string(W) + " cost units); solved exactly by least-cost branch-and-bound instead";
            }
        }
        if (m == PlacementMethod::Greedy) {
            std::vector<std::size_t> order(items.size());
            for (std::size_t k = 0; k < order.size(); k++) order[k] = k;
            merge_sort(order, [&](std::size_t a, std::size_t b) {
                long double la = static_cast<long double>(v[a]) * w[b], lb = static_cast<long double>(v[b]) * w[a];
                return la > lb || (la == lb && a < b);
            });
            std::int64_t used = 0;
            for (std::size_t k : order)
                if (used + w[k] <= W) { take[k] = true; used += w[k]; }
            r.optimal = false;
        } else if (m != PlacementMethod::DynamicProgramming) {
            Frontier f = m == PlacementMethod::BranchBoundFIFO ? Frontier::FIFO
                       : m == PlacementMethod::BranchBoundLIFO ? Frontier::LIFO : Frontier::LeastCost;
            BranchBoundResult b = knapsack_branch_and_bound(w, v, W, f);
            take = b.take;
            r.optimal = b.stats.complete;
            r.search_nodes = b.stats.generated;
            if (!b.stats.complete) r.note += (r.note.empty() ? "" : "; ") + std::string("node limit reached: result may not be optimal");
        }
    }
    std::vector<char> chosen(n, 0);
    for (std::size_t i = 0; i < n; i++)
        if (locked[i]) chosen[i] = 1;
    for (std::size_t k = 0; k < items.size(); k++)
        if (take[k]) chosen[items[k]] = 1;
    const Paise kCap = static_cast<Paise>(4000000000000000000LL);   // reported cost saturates here (only reachable via locks)
    for (std::size_t i = 0; i < n; i++)
        if (chosen[i]) {
            r.chosen.push_back(static_cast<int>(i));
            const Paise c = s.candidates[i].install_cost;
            r.cost = c > kCap - r.cost ? kCap : r.cost + c;
            r.benefit += benefits[i];
        }
    if (!r.over_budget && r.cost > budget) throw std::logic_error("placement exceeded the budget");   // must never happen
    if (r.over_budget) r.note += (r.note.empty() ? "" : "; ") + std::string("locked sites alone exceed the budget");
    r.ms = clock.elapsed_ms();
    return r;
}

} // namespace ev
