// branch_and_bound.hpp - exact 0/1 knapsack by branch-and-bound (the station-placement solver
// used when the dynamic-programming table would be too large).
//
// Items are considered in decreasing value/weight order. Each node fixes the first `level`
// items; its bound is the fractional (Dantzig) relaxation of the rest, an upper bound on any
// completion. A node is pruned when floor(bound) <= best value found so far (values are integers).
// The incumbent starts from the greedy-by-ratio solution. Three frontier disciplines:
//   FIFO  breadth-first queue
//   LIFO  depth-first stack (include-branch explored first)
//   LC    least-cost = best-bound-first (max-heap on bound); stops as soon as the best remaining
//         bound cannot beat the incumbent.
// All three return the same optimal value when the search completes; they differ in how many
// nodes they generate and how large the frontier grows. Worst case Θ(2^n) nodes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ev {

enum class Frontier { FIFO, LIFO, LeastCost };
const char *frontier_name(Frontier f);   // "FIFO", "LIFO", "LC"

struct BranchBoundStats {
    std::uint64_t generated = 0;      // nodes created
    std::uint64_t expanded = 0;       // nodes branched on
    std::uint64_t max_frontier = 0;   // peak frontier size
    bool complete = true;             // false when node_limit stopped the search (result may be suboptimal)
};

struct BranchBoundResult {
    std::int64_t value = 0;
    std::int64_t weight = 0;
    std::vector<bool> take;
    BranchBoundStats stats;
};

// Same input rules as knapsack_01 (non-negative integers). node_limit 0 means 4,000,000.
BranchBoundResult knapsack_branch_and_bound(const std::vector<std::int64_t> &weights,
                                            const std::vector<std::int64_t> &values, std::int64_t capacity,
                                            Frontier frontier = Frontier::LeastCost, std::uint64_t node_limit = 0);

} // namespace ev
