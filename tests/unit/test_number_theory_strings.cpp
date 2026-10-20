// Unit tests: gcd, extended Euclid, modular inverse, CRT, and the four string matchers.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "ev/algorithms/number_theory.hpp"
#include "ev/algorithms/string_matching.hpp"
#include "ev/core/rng.hpp"

using namespace ev;

TEST(NumberTheory, GcdAndExtendedEuclid) {
    EXPECT_EQ(gcd(48, 18), 6);
    EXPECT_EQ(gcd(0, 7), 7);
    EXPECT_EQ(gcd(0, 0), 0);
    EXPECT_EQ(gcd(-12, 8), 4);
    std::int64_t x, y;
    const std::int64_t g = extended_gcd(240, 46, x, y);
    EXPECT_EQ(g, 2);
    EXPECT_EQ(240 * x + 46 * y, 2);
}

TEST(NumberTheory, ModularInverse) {
    EXPECT_EQ(mod_inverse(3, 11), 4);   // 3 * 4 = 12 = 1 (mod 11)
    EXPECT_EQ(mod_inverse(10, 17), 12);
    EXPECT_EQ(mod_inverse(6, 9), -1);   // not coprime
    EXPECT_EQ(mod_inverse(5, 0), -1);
}

TEST(Crt, CoprimeModuli) {
    CrtResult r = chinese_remainder({2, 3, 2}, {3, 5, 7});
    EXPECT_EQ(r.status, CrtStatus::Solved);
    EXPECT_EQ(r.x, 23);
    EXPECT_EQ(r.modulus, 105);
}

TEST(Crt, NonCoprimeConsistentAndInconsistent) {
    CrtResult a = chinese_remainder({2, 4}, {4, 6});   // x = 10 (mod 12)
    EXPECT_EQ(a.status, CrtStatus::Solved);
    EXPECT_EQ(a.x, 10);
    EXPECT_EQ(a.modulus, 12);
    EXPECT_EQ(chinese_remainder({1, 2}, {2, 4}).status, CrtStatus::Inconsistent);
}

TEST(Crt, InvalidInputAndOverflow) {
    EXPECT_EQ(chinese_remainder({}, {}).status, CrtStatus::Invalid);
    EXPECT_EQ(chinese_remainder({1}, {0}).status, CrtStatus::Invalid);
    EXPECT_EQ(chinese_remainder({1, 2}, {3}).status, CrtStatus::Invalid);
    // two large coprime moduli whose lcm exceeds 2^62
    EXPECT_EQ(chinese_remainder({1, 1}, {(1LL << 40) + 15, (1LL << 40) + 33}).status, CrtStatus::Invalid);
}

TEST(Crt, RandomSystemsAreSatisfied) {
    Rng rng(59);
    for (int trial = 0; trial < 300; trial++) {
        const std::size_t k = static_cast<std::size_t>(rng.range(1, 4));
        std::vector<std::int64_t> m(k), a(k);
        const std::int64_t x0 = rng.range(0, 100000);
        for (std::size_t i = 0; i < k; i++) {
            m[i] = rng.range(1, 200);
            a[i] = x0 % m[i];   // consistent by construction
        }
        CrtResult r = chinese_remainder(a, m);
        ASSERT_EQ(r.status, CrtStatus::Solved);
        for (std::size_t i = 0; i < k; i++) EXPECT_EQ(r.x % m[i], a[i]);
        EXPECT_LT(r.x, r.modulus);
        EXPECT_EQ(r.x, x0 % r.modulus);   // the smallest solution
    }
}

TEST(StringMatching, KnownPatterns) {
    EXPECT_EQ(match_kmp("ABABDABACDABABCABAB", "ABABCABAB"), std::vector<std::size_t>{10});
    EXPECT_EQ(match_rabin_karp("AABAACAADAABAABA", "AABA"), (std::vector<std::size_t>{0, 9, 12}));
    EXPECT_EQ(match_automaton("AAAA", "AA"), (std::vector<std::size_t>{0, 1, 2}));   // overlapping
    EXPECT_EQ(match_naive("abc", "abcd"), std::vector<std::size_t>{});               // pattern longer than text
    EXPECT_EQ(prefix_function("ABABACA"), (std::vector<std::size_t>{0, 0, 1, 2, 3, 0, 1}));
}

TEST(StringMatching, EmptyPatternIsRejected) {
    EXPECT_THROW(match_naive("abc", ""), std::invalid_argument);
    EXPECT_THROW(match_kmp("abc", ""), std::invalid_argument);
    EXPECT_THROW(match_rabin_karp("abc", ""), std::invalid_argument);
    EXPECT_THROW(match_automaton("abc", ""), std::invalid_argument);
    EXPECT_TRUE(match_kmp("", "a").empty());
}

TEST(StringMatching, AllFourAgreeOnRandomLogs) {
    Rng rng(61);
    for (int trial = 0; trial < 200; trial++) {
        std::string t, p;
        const int n = static_cast<int>(rng.range(0, 300)), m = static_cast<int>(rng.range(1, 5));
        for (int i = 0; i < n; i++) t.push_back("FORCEI"[rng.range(0, 2)]);   // small alphabet: many matches
        for (int i = 0; i < m; i++) p.push_back("FORCEI"[rng.range(0, 2)]);
        std::vector<std::size_t> a = match_naive(t, p);
        EXPECT_EQ(match_rabin_karp(t, p), a);
        EXPECT_EQ(match_automaton(t, p), a);
        EXPECT_EQ(match_kmp(t, p), a);
    }
}

TEST(StringMatching, KmpIsLinearOnAFaultBurst) {
    std::string t(100000, 'F');
    MatchStats naive, kmp;
    match_naive(t, "FFFFR", &naive);
    match_kmp(t, "FFFFR", &kmp);
    EXPECT_LE(kmp.comparisons, 2 * t.size());
    EXPECT_GT(naive.comparisons, 4 * t.size());   // the naive matcher re-reads every position
}
