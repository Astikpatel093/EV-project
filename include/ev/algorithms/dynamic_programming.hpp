// dynamic_programming.hpp - 0/1 knapsack, matrix-chain order, LCS, optimal BST, Held-Karp TSP.
// (Bellman-Ford and Floyd-Warshall, also dynamic programming, are in ev/graph/shortest_paths.hpp.)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ev {

// ------------------------------------------------------------------ 0/1 knapsack
// Choose a subset of items maximising total value with total weight <= capacity.
//   best[i][w] = max(best[i-1][w], best[i-1][w - weight_i] + value_i)
// Time Θ(n·W), memory Θ(n·W) bits for the "taken" table plus Θ(W) values.
// Inputs: weights >= 0, values >= 0, capacity >= 0 (all integers). Throws std::invalid_argument on
// negative inputs or size mismatch, std::length_error when n·(W+1) exceeds `max_cells`
// (the caller should then scale the weights or use branch-and-bound).
// Ties: when two subsets have equal value, the one found first by the recurrence (preferring NOT
// taking later items) is returned, so results are deterministic.
struct KnapsackResult {
    std::int64_t value = 0;
    std::int64_t weight = 0;
    std::vector<bool> take;     // take[i] == true when item i is chosen
};
KnapsackResult knapsack_01(const std::vector<std::int64_t> &weights, const std::vector<std::int64_t> &values,
                           std::int64_t capacity, std::int64_t max_cells = 200000000);
// Exhaustive oracle for n <= 25.
KnapsackResult knapsack_brute(const std::vector<std::int64_t> &weights, const std::vector<std::int64_t> &values,
                              std::int64_t capacity);

// ------------------------------------------------------------------ matrix-chain order
// dims has n+1 entries; matrix i (0-based) is dims[i] x dims[i+1]. Θ(n^3) time, Θ(n^2) space.
struct MatrixChainResult {
    std::uint64_t cost = 0;                 // minimum scalar multiplications
    std::string order;                      // e.g. "((A1A2)A3)"
    std::vector<std::vector<int>> split;    // split[i][j] = k: (Ai..Ak)(Ak+1..Aj)
};
// Throws std::invalid_argument when dims has fewer than 2 entries or a dimension is not positive.
MatrixChainResult matrix_chain_order(const std::vector<std::uint64_t> &dims);
std::uint64_t matrix_chain_left_to_right(const std::vector<std::uint64_t> &dims);

// ------------------------------------------------------------------ longest common subsequence
// Θ(|a|·|b|) time and space; returns the length and one LCS (ties prefer skipping from `a`).
template <class T>
struct LcsResult {
    std::size_t length = 0;
    std::vector<T> sequence;
};
LcsResult<int> lcs(const std::vector<int> &a, const std::vector<int> &b);
std::string lcs(const std::string &a, const std::string &b);

// ------------------------------------------------------------------ optimal binary search tree
// CLRS formulation: p[1..n] = probability of searching key i, q[0..n] = probability of a search
// falling between keys. p.size() == n+1 (p[0] unused), q.size() == n+1. Returns the expected
// search cost e[1][n]; root (optional) receives root[i][j]. Θ(n^3).
double optimal_bst(const std::vector<double> &p, const std::vector<double> &q,
                   std::vector<std::vector<int>> *root = nullptr);
// Expected cost of the balanced tree (median split) over the same keys, for comparison.
double balanced_bst_cost(const std::vector<double> &p, const std::vector<double> &q);

// ------------------------------------------------------------------ travelling salesman
// dist is an n x n matrix (asymmetric allowed, infinity = no road). Tour starts and ends at 0.
// Held-Karp: Θ(n^2 2^n) time, Θ(n 2^n) space; n in [1, 20]. Returns infinity when no tour exists.
struct TourResult {
    double length = 0;
    std::vector<int> tour;    // n+1 vertices, first and last are 0 (empty when no tour)
};
TourResult tsp_held_karp(const std::vector<std::vector<double>> &dist);
TourResult tsp_brute(const std::vector<std::vector<double>> &dist);   // oracle, n <= 11

} // namespace ev
