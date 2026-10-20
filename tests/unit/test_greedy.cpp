// Unit tests: activity selection, interval partitioning, Huffman coding.
#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "ev/algorithms/greedy.hpp"
#include "ev/core/rng.hpp"

using namespace ev;

namespace {

std::vector<Interval> random_intervals(Rng &rng, int n) {
    std::vector<Interval> iv;
    for (int i = 0; i < n; i++) {
        double s = static_cast<double>(rng.range(0, 50));
        iv.push_back({i, s, s + static_cast<double>(rng.range(1, 15))});
    }
    return iv;
}

bool compatible(const Interval &a, const Interval &b) { return a.end <= b.start || b.end <= a.start; }

} // namespace

TEST(ActivitySelection, TextbookExample) {
    std::vector<Interval> iv = {{1, 1, 4}, {2, 3, 5}, {3, 0, 6}, {4, 5, 7}, {5, 3, 9}, {6, 5, 9},
                                {7, 6, 10}, {8, 8, 11}, {9, 8, 12}, {10, 2, 14}, {11, 12, 16}};
    std::vector<std::size_t> pick = activity_selection(iv);
    std::vector<int> ids;
    for (std::size_t k : pick) ids.push_back(iv[k].id);
    EXPECT_EQ(ids, (std::vector<int>{1, 4, 8, 11}));
}

TEST(ActivitySelection, EmptySingleTouchingAndAllOverlapping) {
    EXPECT_TRUE(activity_selection({}).empty());
    EXPECT_EQ(activity_selection({{1, 0, 1}}).size(), 1u);
    // half-open sessions: [0,10) and [10,20) can share one bay
    EXPECT_EQ(activity_selection({{1, 0, 10}, {2, 10, 20}, {3, 20, 30}}).size(), 3u);
    EXPECT_EQ(activity_selection({{1, 0, 10}, {2, 1, 9}, {3, 2, 8}}).size(), 1u);
}

TEST(ActivitySelection, MatchesExhaustiveSearchAndIsCompatible) {
    Rng rng(3);
    for (int trial = 0; trial < 200; trial++) {
        std::vector<Interval> iv = random_intervals(rng, static_cast<int>(rng.range(1, 14)));
        std::vector<std::size_t> pick = activity_selection(iv);
        EXPECT_EQ(pick.size(), activity_selection_brute(iv));
        for (std::size_t a = 0; a < pick.size(); a++)
            for (std::size_t b = a + 1; b < pick.size(); b++) EXPECT_TRUE(compatible(iv[pick[a]], iv[pick[b]]));
    }
}

TEST(ActivitySelection, RejectsInvalidSessions) {
    EXPECT_THROW(activity_selection({{1, 10, 5}}), std::invalid_argument);    // ends before it starts
    EXPECT_THROW(activity_selection({{1, 10, 10}}), std::invalid_argument);   // zero length
}

TEST(IntervalPartitioning, TextbookExampleNeedsThreeBays) {
    std::vector<Interval> lec = {{1, 9, 10.5}, {2, 9, 12.5}, {3, 9, 10.5}, {4, 11, 12.5}, {5, 11, 14},
                                 {6, 13, 14.5}, {7, 13, 14.5}, {8, 14, 16.5}, {9, 15, 16.5}, {10, 15, 16.5}};
    PartitionResult r = interval_partitioning(lec);
    EXPECT_EQ(r.resources, 3);
    EXPECT_EQ(max_overlap(lec), 3);
}

TEST(IntervalPartitioning, EqualsDepthAndAssignmentIsValid) {
    Rng rng(9);
    for (int trial = 0; trial < 200; trial++) {
        std::vector<Interval> iv = random_intervals(rng, static_cast<int>(rng.range(0, 40)));
        PartitionResult r = interval_partitioning(iv);
        EXPECT_EQ(r.resources, max_overlap(iv));   // greedy is optimal: bays == depth
        for (std::size_t a = 0; a < iv.size(); a++) {
            ASSERT_GE(r.assignment[a], 0);
            ASSERT_LT(r.assignment[a], r.resources);
            for (std::size_t b = a + 1; b < iv.size(); b++)
                if (r.assignment[a] == r.assignment[b]) EXPECT_TRUE(compatible(iv[a], iv[b]));
        }
    }
}

TEST(IntervalPartitioning, SelectionAndPartitioningAnswerDifferentQuestions) {
    // three sessions overlap: one bay serves only one of them, serving all needs three bays
    std::vector<Interval> iv = {{1, 0, 60}, {2, 10, 70}, {3, 20, 80}};
    EXPECT_EQ(activity_selection(iv).size(), 1u);
    EXPECT_EQ(interval_partitioning(iv).resources, 3);
    EXPECT_EQ(interval_partitioning({}).resources, 0);
}

TEST(Huffman, TextbookFrequenciesGiveTwoHundredTwentyFourBits) {
    HuffmanCode h(std::map<unsigned char, std::uint64_t>{{'a', 45}, {'b', 13}, {'c', 12}, {'d', 16}, {'e', 9}, {'f', 5}});
    EXPECT_EQ(h.total_bits(), 224u);
    EXPECT_EQ(h.code('a').size(), 1u);   // the most frequent symbol gets the shortest code
    EXPECT_NEAR(h.average_bits(), 2.24, 1e-12);
}

TEST(Huffman, CodesArePrefixFree) {
    HuffmanCode h(std::string("this is an example of a huffman tree"));
    for (const auto &x : h.codes())
        for (const auto &y : h.codes())
            if (x.first != y.first) EXPECT_NE(y.second.compare(0, x.second.size(), x.second), 0) << x.second << " prefixes " << y.second;
}

TEST(Huffman, RoundTripOnRandomText) {
    Rng rng(21);
    for (int trial = 0; trial < 50; trial++) {
        std::string s;
        const int n = static_cast<int>(rng.range(1, 500));
        for (int i = 0; i < n; i++) s.push_back(static_cast<char>('A' + rng.range(0, 7)));
        HuffmanCode h(s);
        EXPECT_EQ(h.decode(h.encode(s)), s);
        EXPECT_EQ(h.encode(s).size(), h.total_bits());
    }
}

TEST(Huffman, SingleSymbolInput) {
    HuffmanCode h(std::string("FFFFFF"));
    EXPECT_EQ(h.symbols(), 1u);
    EXPECT_EQ(h.code('F'), "0");
    EXPECT_EQ(h.decode(h.encode("FFF")), "FFF");
}

TEST(Huffman, InvalidInputIsRejected) {
    EXPECT_THROW(HuffmanCode(std::string("")), std::invalid_argument);
    EXPECT_THROW(HuffmanCode(std::map<unsigned char, std::uint64_t>{{'x', 0}}), std::invalid_argument);
    HuffmanCode h(std::string("aab"));
    EXPECT_THROW(h.encode("abc"), std::invalid_argument);   // 'c' is not in the code
    std::vector<bool> bits = h.encode("b");
    bits.pop_back();                                        // cut a code in half
    if (!bits.empty()) EXPECT_THROW(h.decode(bits), std::invalid_argument);
    EXPECT_THROW(h.code('z'), std::invalid_argument);
}
