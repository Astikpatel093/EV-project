// backtracking.hpp - N-Queens (validates the backtracking framework) and subset sum.
#pragma once

#include <cstdint>
#include <vector>

namespace ev {

// Counts all placements of n non-attacking queens (bitmask backtracking).
// first (optional) receives the column of the queen in each row of the first solution found.
// n == 0 -> 1 (the empty board); 1 <= n <= 20 supported; otherwise std::invalid_argument.
std::uint64_t nqueens_count(int n, std::vector<int> *first = nullptr, std::uint64_t *nodes = nullptr);

// Subset sum by backtracking over items sorted in decreasing order, pruning when the remaining
// items cannot reach the target. Items must be >= 0 (std::invalid_argument otherwise).
enum class SearchOutcome { Found, Impossible, NodeLimit };
struct SubsetSumResult {
    SearchOutcome outcome = SearchOutcome::Impossible;
    std::vector<bool> take;      // valid when outcome == Found
    std::uint64_t nodes = 0;
};
SubsetSumResult subset_sum_backtracking(const std::vector<std::int64_t> &items, std::int64_t target,
                                        std::uint64_t node_limit = 0 /* 0 = unlimited */);
// Θ(n·target) dynamic-programming oracle; throws std::length_error if target > 50,000,000.
bool subset_sum_dp(const std::vector<std::int64_t> &items, std::int64_t target);

} // namespace ev
