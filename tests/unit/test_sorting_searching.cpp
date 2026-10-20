// Unit tests: merge sort, quick sort, iterative and recursive binary search.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ev/algorithms/searching.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/core/rng.hpp"

using namespace ev;

namespace {

std::vector<int> random_ints(std::size_t n, std::uint64_t seed, int lo, int hi) {
    Rng r(seed);
    std::vector<int> v(n);
    for (int &x : v) x = static_cast<int>(r.range(lo, hi));
    return v;
}

// std::sort is used only as an independent oracle in tests, never in the project code.
std::vector<int> oracle_sorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return v;
}

const PivotRule kRules[] = {PivotRule::MedianOfThree, PivotRule::First, PivotRule::Random};

} // namespace

TEST(Sorting, EmptyArray) {
    std::vector<int> a;
    merge_sort(a);
    EXPECT_TRUE(a.empty());
    for (PivotRule r : kRules) {
        quick_sort(a, r);
        EXPECT_TRUE(a.empty());
    }
}

TEST(Sorting, SingleElement) {
    std::vector<int> a = {42};
    merge_sort(a);
    EXPECT_EQ(a, std::vector<int>{42});
    for (PivotRule r : kRules) {
        quick_sort(a, r);
        EXPECT_EQ(a, std::vector<int>{42});
    }
}

TEST(Sorting, AlreadySortedAndReverseSorted) {
    std::vector<int> up(1000), down(1000);
    for (int i = 0; i < 1000; i++) { up[static_cast<std::size_t>(i)] = i; down[static_cast<std::size_t>(i)] = 999 - i; }
    for (const auto &input : {up, down}) {
        std::vector<int> m = input;
        merge_sort(m);
        EXPECT_EQ(m, up);
        for (PivotRule r : kRules) {
            std::vector<int> q = input;
            quick_sort(q, r);
            EXPECT_EQ(q, up);
        }
    }
}

TEST(Sorting, MergeSortOnSortedInputUsesLinearComparisons) {
    std::vector<int> a(1024);
    for (int i = 0; i < 1024; i++) a[static_cast<std::size_t>(i)] = i;
    SortStats st;
    merge_sort(a, [](int x, int y) { return x < y; }, &st);
    EXPECT_EQ(st.comparisons, 1023u);   // one "already in order" check per merge
}

TEST(Sorting, DuplicatesAndNegativeValues) {
    std::vector<int> a = {5, -3, 5, 0, -3, -3, 7, 0, -100, 5};
    const std::vector<int> want = {-100, -3, -3, -3, 0, 0, 5, 5, 5, 7};
    std::vector<int> m = a;
    merge_sort(m);
    EXPECT_EQ(m, want);
    for (PivotRule r : kRules) {
        std::vector<int> q = a;
        quick_sort(q, r);
        EXPECT_EQ(q, want);
    }
}

TEST(Sorting, AllEqualKeys) {
    std::vector<int> a(5000, 7);
    for (PivotRule r : kRules) {
        std::vector<int> q = a;
        quick_sort(q, r);
        EXPECT_EQ(q, a);
    }
}

TEST(Sorting, LargeRandomArraysMatchOracle) {
    for (std::uint64_t seed : {1u, 2u, 3u}) {
        std::vector<int> a = random_ints(200000, seed, -1000000, 1000000);
        std::vector<int> want = oracle_sorted(a);
        std::vector<int> m = a;
        merge_sort(m);
        EXPECT_EQ(m, want);
        for (PivotRule r : kRules) {
            std::vector<int> q = a;
            quick_sort(q, r);
            EXPECT_EQ(q, want);
        }
    }
}

TEST(Sorting, QuickSortWorstCaseDoesNotOverflowTheStack) {
    // first-element pivot on sorted input is the Θ(n^2) case; the explicit stack keeps depth O(log n)
    std::vector<int> a(20000);
    for (int i = 0; i < 20000; i++) a[static_cast<std::size_t>(i)] = i;
    SortStats st;
    quick_sort(a, [](int x, int y) { return x < y; }, PivotRule::First, &st);
    EXPECT_TRUE(is_sorted_by(a, [](int x, int y) { return x < y; }));
    EXPECT_GT(st.comparisons, 20000ull * 20000ull / 4);   // quadratic behaviour is visible in the count
}

TEST(Sorting, MergeSortIsStableOnStationRecords) {
    struct Station { int id; int cost; };
    std::vector<Station> s = {{1, 50}, {2, 30}, {3, 50}, {4, 30}, {5, 10}, {6, 50}};
    merge_sort(s, [](const Station &a, const Station &b) { return a.cost < b.cost; });
    std::vector<int> ids;
    for (const Station &x : s) ids.push_back(x.id);
    EXPECT_EQ(ids, (std::vector<int>{5, 2, 4, 1, 3, 6}));   // equal costs keep their input order
}

TEST(Sorting, CustomComparatorDescending) {
    std::vector<int> a = {3, 1, 2};
    merge_sort(a, [](int x, int y) { return x > y; });
    EXPECT_EQ(a, (std::vector<int>{3, 2, 1}));
    std::vector<std::string> w = {"pear", "apple", "fig"};
    quick_sort(w, [](const std::string &x, const std::string &y) { return x < y; });
    EXPECT_EQ(w, (std::vector<std::string>{"apple", "fig", "pear"}));
}

// ------------------------------------------------------------------ searching
TEST(BinarySearch, EmptyArray) {
    std::vector<int> a;
    EXPECT_EQ(binary_search_iterative(a, 5), -1);
    EXPECT_EQ(binary_search_recursive(a, 5), -1);
    EXPECT_EQ(lower_bound_index(a, 5), 0u);
}

TEST(BinarySearch, SingleElement) {
    std::vector<int> a = {9};
    EXPECT_EQ(binary_search_iterative(a, 9), 0);
    EXPECT_EQ(binary_search_recursive(a, 9), 0);
    EXPECT_EQ(binary_search_iterative(a, 8), -1);
    EXPECT_EQ(binary_search_recursive(a, 10), -1);
}

TEST(BinarySearch, SuccessAndFailure) {
    std::vector<int> a = {-40, -7, 0, 3, 3, 3, 12, 99};
    EXPECT_EQ(binary_search_iterative(a, -40), 0);
    EXPECT_EQ(binary_search_iterative(a, 99), 7);
    EXPECT_EQ(binary_search_iterative(a, 3), 3);   // leftmost of the duplicates
    EXPECT_EQ(binary_search_recursive(a, 3), 3);
    EXPECT_EQ(binary_search_iterative(a, -41), -1);   // below the minimum
    EXPECT_EQ(binary_search_iterative(a, 100), -1);   // above the maximum
    EXPECT_EQ(binary_search_recursive(a, 5), -1);     // between two keys
    EXPECT_EQ(lower_bound_index(a, 3), 3u);
    EXPECT_EQ(upper_bound_index(a, 3), 6u);
    EXPECT_EQ(upper_bound_index(a, 1000), 8u);
}

TEST(BinarySearch, IterativeAndRecursiveAgreeOnEveryKey) {
    std::vector<int> a = random_ints(5000, 7, 0, 2000);
    merge_sort(a);
    for (int key = -5; key <= 2005; key++) {
        long it = binary_search_iterative(a, key), rc = binary_search_recursive(a, key);
        EXPECT_EQ(it, rc) << "key " << key;
        const bool present = std::binary_search(a.begin(), a.end(), key);   // oracle
        EXPECT_EQ(it >= 0, present);
        if (it >= 0) {
            EXPECT_EQ(a[static_cast<std::size_t>(it)], key);
            if (it > 0) {
                EXPECT_LT(a[static_cast<std::size_t>(it) - 1], key);   // leftmost occurrence
            }
        }
    }
}

TEST(BinarySearch, LargeArray) {
    std::vector<long long> a(1000000);
    for (std::size_t i = 0; i < a.size(); i++) a[i] = static_cast<long long>(i) * 3;
    EXPECT_EQ(binary_search_iterative(a, 2999997LL), 999999);
    EXPECT_EQ(binary_search_recursive(a, 1500000LL), 500000);
    EXPECT_EQ(binary_search_iterative(a, 1500001LL), -1);
}

TEST(BinarySearch, RecordsByKeyProjection) {
    struct Station { int id; const char *name; };
    std::vector<Station> s = {{101, "A"}, {205, "B"}, {310, "C"}, {512, "D"}};
    auto id = [](const Station &x) { return x.id; };
    EXPECT_EQ(binary_search_iterative(s, 310, id), 2);
    EXPECT_EQ(binary_search_recursive(s, 512, id), 3);
    EXPECT_EQ(binary_search_iterative(s, 300, id), -1);
}
