// placement.hpp - choose which candidate sites to build under the installation budget.
//
// Objective: maximise the total benefit score of the chosen candidates.
// Constraint: total installation cost <= budget (exact integer paise; never exceeded).
// This is the 0/1 knapsack problem: item = candidate, weight = cost, value = benefit.
//   DynamicProgramming   exact, Θ(n·W) with W = budget / g, where g is the greatest common divisor
//                        of the budget and all costs (costs in whole lakhs give W = budget in lakhs).
//                        If the table would be too large it switches to least-cost branch-and-bound
//                        (also exact) and says so in `note`.
//   BranchBound*         exact (FIFO / LIFO / least-cost frontiers), see branch_and_bound.hpp
//   Greedy               benefit/cost ratio order - fast, NOT guaranteed optimal
// The model optimises the benefit scores it is given. It does NOT solve the full geographic
// facility-location problem: benefits are additive, so two neighbouring sites that serve the same
// drivers are both credited. The trip planner re-evaluates every selection to expose this.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ev/core/money.hpp"
#include "ev/models/scenario.hpp"

namespace ev {

class EnergyModel;

enum class PlacementMethod { Greedy, DynamicProgramming, BranchBoundLC, BranchBoundFIFO, BranchBoundLIFO };
constexpr int kPlacementMethods = 5;
const char *to_string(PlacementMethod m);            // "greedy", "dp", "bnb-lc", "bnb-fifo", "bnb-lifo"
bool parse_placement(const std::string &s, PlacementMethod &out);

// Benefit of every candidate: the value from candidates.csv when given; otherwise an estimate
// from the trips: every trip adds 1 to each node on its shortest road route (4 if the vehicle
// cannot drive that route on its starting charge), and a candidate gets its node's total,
// multiplied by its suitability factor when one is given.
struct BenefitTable {
    std::vector<std::int64_t> benefit;   // per candidate
    std::vector<bool> estimated;         // true when computed from trips
};
BenefitTable candidate_benefits(const Scenario &s, const EnergyModel &model);

struct PlacementConstraints {
    std::vector<int> locked;   // candidate indices that must be built (cost comes off the budget first)
    std::vector<int> banned;   // candidate indices that must not be built
};

struct PlacementResult {
    PlacementMethod method = PlacementMethod::DynamicProgramming;
    std::vector<int> chosen;          // candidate indices, ascending
    Paise budget = 0;
    Paise cost = 0;
    std::int64_t benefit = 0;
    bool optimal = false;             // proven optimal for the knapsack objective
    bool over_budget = false;         // locked sites alone cost more than the budget
    std::uint64_t search_nodes = 0;   // branch-and-bound nodes generated
    double ms = 0;
    std::string note;
};

// Throws std::invalid_argument when benefits.size() != candidates, a cost is not positive, a
// benefit is negative, the budget is negative, or a constraint index is out of range.
PlacementResult place_stations(const Scenario &s, const std::vector<std::int64_t> &benefits, PlacementMethod method,
                               const PlacementConstraints &constraints = {}, std::optional<Paise> budget = std::nullopt);

} // namespace ev
