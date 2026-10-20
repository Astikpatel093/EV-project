// Unit tests: maximum subarray, fast exponentiation, celebrity, classical and Strassen products.
#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "ev/algorithms/divide_conquer.hpp"
#include "ev/core/rng.hpp"

using namespace ev;

TEST(MaxSubarray, TextbookExample) {
    SubarrayResult r = max_subarray_dc({-2, 1, -3, 4, -1, 2, 1, -5, 4});
    EXPECT_DOUBLE_EQ(r.sum, 6);
    EXPECT_EQ(r.lo, 3u);
    EXPECT_EQ(r.hi, 6u);
}

TEST(MaxSubarray, AllNegativePicksTheLargestElement) {
    SubarrayResult r = max_subarray_dc({-8, -3, -6, -2, -5});
    EXPECT_DOUBLE_EQ(r.sum, -2);
    EXPECT_EQ(r.lo, 3u);
    EXPECT_EQ(r.hi, 3u);
}

TEST(MaxSubarray, AllPositiveTakesEverything) {
    SubarrayResult r = max_subarray_dc({1, 2, 3, 4});
    EXPECT_DOUBLE_EQ(r.sum, 10);
    EXPECT_EQ(r.lo, 0u);
    EXPECT_EQ(r.hi, 3u);
}

TEST(MaxSubarray, SingleElementAndEmpty) {
    EXPECT_DOUBLE_EQ(max_subarray_dc({-7}).sum, -7);
    EXPECT_THROW(max_subarray_dc({}), std::invalid_argument);
    EXPECT_THROW(max_subarray_kadane({}), std::invalid_argument);
}

TEST(MaxSubarray, TiesPreferTheEarliestRun) {
    SubarrayResult r = max_subarray_dc({5, -5, 5});
    EXPECT_EQ(r.lo, 0u);
    EXPECT_EQ(r.hi, 0u);
}

TEST(MaxSubarray, DivideAndConquerMatchesKadaneAndBruteForce) {
    Rng rng(11);
    for (int trial = 0; trial < 300; trial++) {
        std::vector<double> a(static_cast<std::size_t>(rng.range(1, 60)));
        for (double &x : a) x = static_cast<double>(rng.range(-20, 20));   // integers: sums are exact
        SubarrayResult d = max_subarray_dc(a), k = max_subarray_kadane(a), b = max_subarray_brute(a);
        EXPECT_EQ(d.sum, b.sum);
        EXPECT_EQ(k.sum, b.sum);
        EXPECT_EQ(d.lo, b.lo);
        EXPECT_EQ(d.hi, b.hi);
        EXPECT_EQ(k.lo, b.lo);
        EXPECT_EQ(k.hi, b.hi);
    }
}

TEST(FastPower, SmallCases) {
    EXPECT_DOUBLE_EQ(pow_fast(2.0, 0), 1.0);
    EXPECT_DOUBLE_EQ(pow_fast(2.0, 10), 1024.0);
    EXPECT_DOUBLE_EQ(pow_fast(-3.0, 3), -27.0);
    EXPECT_DOUBLE_EQ(pow_fast(0.0, 5), 0.0);
    EXPECT_DOUBLE_EQ(pow_fast(0.0, 0), 1.0);
}

TEST(FastPower, MatchesNaiveAndStdPow) {
    for (std::uint64_t e : {1ull, 7ull, 64ull, 1500ull}) {
        EXPECT_NEAR(pow_fast(0.9998, e), pow_naive(0.9998, e), 1e-12);
        EXPECT_NEAR(pow_fast(1.0001, e), std::pow(1.0001, static_cast<double>(e)), 1e-9);
    }
}

TEST(FastPower, ModularExponentiation) {
    EXPECT_EQ(powmod(3, 13, 1000), 323u);
    EXPECT_EQ(powmod(5, 0, 7), 1u);
    EXPECT_EQ(powmod(5, 3, 1), 0u);   // everything is 0 modulo 1
    EXPECT_THROW(powmod(2, 3, 0), std::invalid_argument);
    // Fermat's little theorem with the Mersenne prime 2^61 - 1: products would overflow 64 bits
    const std::uint64_t p = (1ULL << 61) - 1;
    EXPECT_EQ(powmod(123456789, p - 1, p), 1u);
    EXPECT_EQ(mulmod(p - 1, p - 1, p), 1u);   // (-1)(-1) = 1
}

TEST(Celebrity, FindsTheCelebrity) {
    // 4 people; everybody knows 2; 2 knows nobody
    std::vector<std::vector<int>> k = {{0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}, {1, 0, 1, 0}};
    auto knows = [&](int a, int b) { return k[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] == 1; };
    long q1 = 0, q2 = 0;
    EXPECT_EQ(find_celebrity(4, knows, &q1), 2);
    EXPECT_EQ(find_celebrity_naive(4, knows, &q2), 2);
    EXPECT_LE(q1, 3 * (4 - 1));   // at most 3(n-1) questions
}

TEST(Celebrity, NoCelebrityAndEdgeSizes) {
    auto everyone = [](int a, int b) { return a != b; };   // everybody knows everybody
    EXPECT_EQ(find_celebrity(5, everyone, nullptr), -1);
    auto nobody = [](int, int) { return false; };           // nobody knows anybody
    EXPECT_EQ(find_celebrity(3, nobody, nullptr), -1);
    EXPECT_EQ(find_celebrity(0, nobody, nullptr), -1);
    EXPECT_EQ(find_celebrity(1, nobody, nullptr), 0);
}

TEST(Celebrity, LinearQueriesVersusQuadratic) {
    const int n = 200, celeb = 137;
    auto knows = [&](int a, int b) { return b == celeb && a != celeb; };
    long fast = 0, slow = 0;
    EXPECT_EQ(find_celebrity(n, knows, &fast), celeb);
    EXPECT_EQ(find_celebrity_naive(n, knows, &slow), celeb);
    EXPECT_LE(fast, 3L * (n - 1));
    EXPECT_GT(slow, fast);
}

TEST(Matrix, ClassicalKnownProductAndRectangular) {
    Matrix a(2, 3), b(3, 2);
    double v = 1;
    for (std::size_t i = 0; i < 2; i++)
        for (std::size_t j = 0; j < 3; j++) a(i, j) = v++;
    for (std::size_t i = 0; i < 3; i++)
        for (std::size_t j = 0; j < 2; j++) b(i, j) = v++;
    Matrix c = multiply_classical(a, b);   // [[1,2,3],[4,5,6]] x [[7,8],[9,10],[11,12]]
    EXPECT_EQ(c.rows(), 2u);
    EXPECT_EQ(c.cols(), 2u);
    EXPECT_DOUBLE_EQ(c(0, 0), 58);
    EXPECT_DOUBLE_EQ(c(0, 1), 64);
    EXPECT_DOUBLE_EQ(c(1, 0), 139);
    EXPECT_DOUBLE_EQ(c(1, 1), 154);
    Matrix s = multiply_strassen(a, b, 1);
    EXPECT_LT(c.max_abs_diff(s), 1e-9);
}

TEST(Matrix, InvalidDimensionsAreRejected) {
    Matrix a(2, 3), b(2, 3), z(0, 3);
    EXPECT_THROW(multiply_classical(a, b), std::invalid_argument);   // 3 != 2
    EXPECT_THROW(multiply_strassen(a, b), std::invalid_argument);
    EXPECT_THROW(multiply_classical(z, Matrix(3, 3)), std::invalid_argument);   // zero rows
    EXPECT_THROW(multiply_strassen(Matrix(2, 2), Matrix(2, 2), 0), std::invalid_argument);   // cutoff 0
    EXPECT_THROW(a.at(2, 0), std::out_of_range);
    EXPECT_THROW(a.max_abs_diff(Matrix(3, 2)), std::invalid_argument);
}

TEST(Matrix, StrassenEqualsClassicalOnManySizes) {
    Rng rng(5);
    for (std::size_t n : {1u, 2u, 3u, 5u, 8u, 13u, 16u, 31u, 64u, 70u}) {
        Matrix a(n, n), b(n, n);
        for (std::size_t i = 0; i < n; i++)
            for (std::size_t j = 0; j < n; j++) {
                a(i, j) = static_cast<double>(rng.range(-9, 9));
                b(i, j) = static_cast<double>(rng.range(-9, 9));
            }
        Matrix c = multiply_classical(a, b);
        for (std::size_t cutoff : {1u, 4u, 32u}) EXPECT_EQ(c.max_abs_diff(multiply_strassen(a, b, cutoff)), 0.0) << n;   // integers: exact
    }
}

TEST(Matrix, IdentityIsNeutral) {
    Matrix a(3, 3);
    for (std::size_t i = 0; i < 3; i++)
        for (std::size_t j = 0; j < 3; j++) a(i, j) = static_cast<double>(i * 3 + j);
    EXPECT_EQ(multiply_classical(a, Matrix::identity(3)).max_abs_diff(a), 0.0);
    EXPECT_EQ(multiply_strassen(Matrix::identity(3), a, 1).max_abs_diff(a), 0.0);
}
