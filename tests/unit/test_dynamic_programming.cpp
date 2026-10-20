// Unit tests: 0/1 knapsack, matrix-chain order, LCS, optimal BST, Held-Karp, backtracking,
// branch-and-bound.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "ev/algorithms/backtracking.hpp"
#include "ev/algorithms/branch_and_bound.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/core/rng.hpp"
#include "ev/graph/shortest_paths.hpp"

using namespace ev;

namespace {

std::int64_t total(const std::vector<std::int64_t> &x, const std::vector<bool> &take) {
    std::int64_t s = 0;
    for (std::size_t i = 0; i < x.size(); i++)
        if (take[i]) s += x[i];
    return s;
}

} // namespace

// ------------------------------------------------------------------ knapsack
TEST(Knapsack, KnownOptimalSolution) {
    KnapsackResult r = knapsack_01({10, 20, 30}, {60, 100, 120}, 50);
    EXPECT_EQ(r.value, 220);
    EXPECT_EQ(r.weight, 50);
    EXPECT_EQ(r.take, (std::vector<bool>{false, true, true}));
}

TEST(Knapsack, ZeroBudget) {
    KnapsackResult r = knapsack_01({10, 20}, {5, 6}, 0);
    EXPECT_EQ(r.value, 0);
    EXPECT_EQ(r.take, (std::vector<bool>{false, false}));
}

TEST(Knapsack, NoCandidates) {
    KnapsackResult r = knapsack_01({}, {}, 100);
    EXPECT_EQ(r.value, 0);
    EXPECT_TRUE(r.take.empty());
}

TEST(Knapsack, AllCandidatesAffordable) {
    KnapsackResult r = knapsack_01({1, 2, 3}, {4, 5, 6}, 100);
    EXPECT_EQ(r.value, 15);
    EXPECT_EQ(r.take, (std::vector<bool>{true, true, true}));
}

TEST(Knapsack, NoCandidateAffordable) {
    KnapsackResult r = knapsack_01({60, 70}, {10, 20}, 50);
    EXPECT_EQ(r.value, 0);
    EXPECT_EQ(r.weight, 0);
}

TEST(Knapsack, CandidateCostExactlyEqualsBudget) {
    KnapsackResult r = knapsack_01({50, 49}, {10, 9}, 50);
    EXPECT_EQ(r.value, 10);
    EXPECT_EQ(r.weight, 50);
}

TEST(Knapsack, EqualBenefitsAndEqualCosts) {
    // equal benefits: the cheaper items let more fit
    KnapsackResult a = knapsack_01({5, 3, 3, 4}, {10, 10, 10, 10}, 7);
    EXPECT_EQ(a.value, 20);
    EXPECT_LE(a.weight, 7);
    // equal costs: the best two benefits win, deterministically
    KnapsackResult b = knapsack_01({4, 4, 4, 4}, {7, 9, 9, 3}, 8);
    EXPECT_EQ(b.value, 18);
    KnapsackResult b2 = knapsack_01({4, 4, 4, 4}, {7, 9, 9, 3}, 8);
    EXPECT_EQ(b.take, b2.take);
}

TEST(Knapsack, InvalidInputIsRejected) {
    EXPECT_THROW(knapsack_01({-1}, {5}, 10), std::invalid_argument);
    EXPECT_THROW(knapsack_01({1}, {-5}, 10), std::invalid_argument);
    EXPECT_THROW(knapsack_01({1, 2}, {5}, 10), std::invalid_argument);
    EXPECT_THROW(knapsack_01({1}, {5}, -1), std::invalid_argument);
    EXPECT_THROW(knapsack_01({1}, {5}, 1000000000000LL), std::length_error);   // table far too large
}

TEST(Knapsack, DynamicProgrammingMatchesBruteForce) {
    Rng rng(41);
    for (int trial = 0; trial < 300; trial++) {
        const std::size_t n = static_cast<std::size_t>(rng.range(0, 14));
        std::vector<std::int64_t> w(n), v(n);
        for (std::size_t i = 0; i < n; i++) { w[i] = rng.range(0, 30); v[i] = rng.range(0, 50); }
        const std::int64_t W = rng.range(0, 100);
        KnapsackResult d = knapsack_01(w, v, W), b = knapsack_brute(w, v, W);
        EXPECT_EQ(d.value, b.value);
        EXPECT_LE(total(w, d.take), W);           // budget never exceeded
        EXPECT_EQ(total(v, d.take), d.value);     // reported value matches the chosen set
    }
}

// ------------------------------------------------------------------ matrix chain
TEST(MatrixChain, TextbookExample) {
    MatrixChainResult r = matrix_chain_order({30, 35, 15, 5, 10, 20, 25});
    EXPECT_EQ(r.cost, 15125u);
    EXPECT_EQ(r.order, "((A1(A2A3))((A4A5)A6))");
    EXPECT_GE(matrix_chain_left_to_right({30, 35, 15, 5, 10, 20, 25}), r.cost);
}

TEST(MatrixChain, SingleMatrixAndInvalidDimensions) {
    MatrixChainResult r = matrix_chain_order({4, 7});
    EXPECT_EQ(r.cost, 0u);
    EXPECT_EQ(r.order, "A1");
    EXPECT_THROW(matrix_chain_order({5}), std::invalid_argument);
    EXPECT_THROW(matrix_chain_order({5, 0, 3}), std::invalid_argument);
}

// ------------------------------------------------------------------ LCS
TEST(Lcs, TextbookExample) {
    std::string s = lcs(std::string("ABCBDAB"), std::string("BDCABA"));
    EXPECT_EQ(s.size(), 4u);
    EXPECT_EQ(lcs(std::string("AGGTAB"), std::string("GXTXAYB")), "GTAB");
}

TEST(Lcs, BoundaryCases) {
    EXPECT_EQ(lcs(std::string(""), std::string("ABC")), "");
    EXPECT_EQ(lcs(std::string("ABC"), std::string("ABC")), "ABC");
    EXPECT_EQ(lcs(std::string("ABC"), std::string("XYZ")), "");
    LcsResult<int> r = lcs(std::vector<int>{1, 2, 3, 4, 5}, std::vector<int>{1, 3, 5, 7});
    EXPECT_EQ(r.length, 3u);
    EXPECT_EQ(r.sequence, (std::vector<int>{1, 3, 5}));
}

// ------------------------------------------------------------------ optimal BST
TEST(OptimalBst, TextbookExample) {
    std::vector<std::vector<int>> root;
    double e = optimal_bst({0, 0.15, 0.10, 0.05, 0.10, 0.20}, {0.05, 0.10, 0.05, 0.05, 0.05, 0.10}, &root);
    EXPECT_NEAR(e, 2.75, 1e-9);
    EXPECT_EQ(root[1][5], 2);   // k2 is the root of the optimal tree
    EXPECT_GE(balanced_bst_cost({0, 0.15, 0.10, 0.05, 0.10, 0.20}, {0.05, 0.10, 0.05, 0.05, 0.05, 0.10}), e - 1e-12);
}

TEST(OptimalBst, InvalidProbabilities) {
    EXPECT_THROW(optimal_bst({0, 0.5}, {0.5}), std::invalid_argument);
    EXPECT_THROW(optimal_bst({0, -0.1}, {0.5, 0.6}), std::invalid_argument);
}

// ------------------------------------------------------------------ TSP
TEST(Tsp, HeldKarpKnownAndBruteForce) {
    std::vector<std::vector<double>> d = {{0, 10, 15, 20}, {10, 0, 35, 25}, {15, 35, 0, 30}, {20, 25, 30, 0}};
    TourResult hk = tsp_held_karp(d);
    EXPECT_DOUBLE_EQ(hk.length, 80);
    EXPECT_EQ(hk.tour.front(), 0);
    EXPECT_EQ(hk.tour.back(), 0);
    EXPECT_EQ(hk.tour.size(), 5u);
    Rng rng(43);
    for (int trial = 0; trial < 30; trial++) {
        const std::size_t n = static_cast<std::size_t>(rng.range(1, 8));
        std::vector<std::vector<double>> m(n, std::vector<double>(n, 0));
        for (std::size_t a = 0; a < n; a++)
            for (std::size_t b = 0; b < n; b++)
                if (a != b) m[a][b] = static_cast<double>(rng.range(1, 50));
        EXPECT_DOUBLE_EQ(tsp_held_karp(m).length, tsp_brute(m).length);
    }
}

TEST(Tsp, NoTourAndLimits) {
    std::vector<std::vector<double>> d = {{0, 1, kInfinity}, {kInfinity, 0, kInfinity}, {kInfinity, kInfinity, 0}};
    EXPECT_EQ(tsp_held_karp(d).length, kInfinity);
    EXPECT_TRUE(tsp_held_karp(d).tour.empty());
    EXPECT_THROW(tsp_held_karp({}), std::invalid_argument);
    EXPECT_THROW(tsp_held_karp(std::vector<std::vector<double>>(21, std::vector<double>(21, 1))), std::invalid_argument);
}

// ------------------------------------------------------------------ backtracking
TEST(NQueens, KnownCounts) {
    const std::uint64_t expected[] = {1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724};
    for (int n = 0; n <= 10; n++) EXPECT_EQ(nqueens_count(n), expected[n]) << "n=" << n;
    EXPECT_THROW(nqueens_count(21), std::invalid_argument);
}

TEST(NQueens, FirstSolutionIsValid) {
    std::vector<int> cols;
    nqueens_count(8, &cols);
    ASSERT_EQ(cols.size(), 8u);
    for (int a = 0; a < 8; a++)
        for (int b = a + 1; b < 8; b++) {
            EXPECT_NE(cols[static_cast<std::size_t>(a)], cols[static_cast<std::size_t>(b)]);
            EXPECT_NE(std::abs(cols[static_cast<std::size_t>(a)] - cols[static_cast<std::size_t>(b)]), b - a);
        }
}

TEST(SubsetSum, FoundImpossibleAndAgreesWithDp) {
    SubsetSumResult r = subset_sum_backtracking({3, 34, 4, 12, 5, 2}, 9);
    ASSERT_EQ(r.outcome, SearchOutcome::Found);
    EXPECT_EQ(total({3, 34, 4, 12, 5, 2}, r.take), 9);
    EXPECT_EQ(subset_sum_backtracking({3, 34, 4, 12, 5, 2}, 30).outcome, SearchOutcome::Impossible);
    Rng rng(47);
    for (int trial = 0; trial < 200; trial++) {
        std::vector<std::int64_t> a(static_cast<std::size_t>(rng.range(0, 12)));
        for (auto &x : a) x = rng.range(0, 40);
        const std::int64_t t = rng.range(0, 150);
        EXPECT_EQ(subset_sum_backtracking(a, t).outcome == SearchOutcome::Found, subset_sum_dp(a, t));
    }
    EXPECT_THROW(subset_sum_backtracking({-1}, 3), std::invalid_argument);
}

TEST(SubsetSum, NodeLimitIsReported) {
    std::vector<std::int64_t> a(30, 2);   // only even sums exist, target odd: a large search
    EXPECT_EQ(subset_sum_backtracking(a, 31, 1000).outcome, SearchOutcome::NodeLimit);
}

// ------------------------------------------------------------------ branch and bound
TEST(BranchAndBound, AllFrontiersFindTheDpOptimum) {
    Rng rng(53);
    for (int trial = 0; trial < 150; trial++) {
        const std::size_t n = static_cast<std::size_t>(rng.range(0, 18));
        std::vector<std::int64_t> w(n), v(n);
        for (std::size_t i = 0; i < n; i++) { w[i] = rng.range(1, 40); v[i] = w[i] * 2 + rng.range(0, 20); }
        const std::int64_t W = rng.range(0, 200);
        const std::int64_t opt = knapsack_01(w, v, W).value;
        for (Frontier f : {Frontier::FIFO, Frontier::LIFO, Frontier::LeastCost}) {
            BranchBoundResult r = knapsack_branch_and_bound(w, v, W, f);
            EXPECT_TRUE(r.stats.complete);
            EXPECT_EQ(r.value, opt) << frontier_name(f);
            EXPECT_LE(r.weight, W);
            EXPECT_EQ(total(v, r.take), r.value);
        }
    }
}

TEST(BranchAndBound, ZeroCapacityAndInvalidInput) {
    EXPECT_EQ(knapsack_branch_and_bound({5}, {10}, 0).value, 0);
    EXPECT_THROW(knapsack_branch_and_bound({-5}, {10}, 3), std::invalid_argument);
    EXPECT_THROW(knapsack_branch_and_bound({5}, {10}, -3), std::invalid_argument);
}
