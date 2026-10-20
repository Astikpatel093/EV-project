// divide_conquer.hpp - divide-and-conquer and numerical algorithms.
//
//  max_subarray_dc       maximum-sum contiguous run, T(n) = 2T(n/2) + Θ(n) = Θ(n log n)
//                        (EV use: the worst consecutive hours of charging load above grid supply)
//  pow_fast / powmod     exponentiation by squaring, T(e) = T(e/2) + Θ(1) = Θ(log e)
//  find_celebrity        elimination: n-1 queries find the only candidate, 2(n-1) verify it, Θ(n)
//  multiply_classical    Θ(n·m·p) triple loop
//  multiply_strassen     7 recursive products, T(n) = 7T(n/2) + Θ(n^2) = Θ(n^log2(7)) ≈ Θ(n^2.807)
//
// Kadane's Θ(n) algorithm and the Θ(n^2) brute force are provided as test oracles.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ev {

// ------------------------------------------------------------------ maximum subarray
struct SubarrayResult {
    std::size_t lo = 0, hi = 0;   // inclusive bounds of the best run
    double sum = 0;
};

// All three throw std::invalid_argument on an empty input (there is no non-empty subarray).
// When several runs share the maximum sum, the D&C version returns the one with the smallest lo,
// then the smallest hi (the oracles follow the same rule so results can be compared exactly).
SubarrayResult max_subarray_dc(const std::vector<double> &a);
SubarrayResult max_subarray_kadane(const std::vector<double> &a);
SubarrayResult max_subarray_brute(const std::vector<double> &a);

// ------------------------------------------------------------------ exponentiation
double pow_fast(double base, std::uint64_t exponent);   // pow_fast(x, 0) == 1
double pow_naive(double base, std::uint64_t exponent);  // Θ(e) oracle
// (base^exponent) mod m for 1 <= m < 2^63, overflow-free (products use mulmod). Throws on m == 0.
std::uint64_t powmod(std::uint64_t base, std::uint64_t exponent, std::uint64_t m);
std::uint64_t mulmod(std::uint64_t a, std::uint64_t b, std::uint64_t m);

// ------------------------------------------------------------------ celebrity
// knows(a, b) == true when person a knows person b. A celebrity is known by everybody else and
// knows nobody. Returns the celebrity's index or -1. `queries` (optional) counts calls to knows.
// n == 0 -> -1; n == 1 -> 0 (a single person trivially satisfies the definition).
int find_celebrity(int n, const std::function<bool(int, int)> &knows, long *queries = nullptr);
int find_celebrity_naive(int n, const std::function<bool(int, int)> &knows, long *queries = nullptr);

// ------------------------------------------------------------------ matrices
class Matrix {
public:
    Matrix() = default;
    Matrix(std::size_t rows, std::size_t cols, double fill = 0.0);
    static Matrix identity(std::size_t n);

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    double &operator()(std::size_t r, std::size_t c) { return data_[r * cols_ + c]; }
    double operator()(std::size_t r, std::size_t c) const { return data_[r * cols_ + c]; }
    // bounds-checked access (throws std::out_of_range)
    double at(std::size_t r, std::size_t c) const;

    double max_abs_diff(const Matrix &other) const;   // throws on shape mismatch

private:
    std::size_t rows_ = 0, cols_ = 0;
    std::vector<double> data_;
};

// Both throw std::invalid_argument when a.cols() != b.rows() or a dimension is zero.
Matrix multiply_classical(const Matrix &a, const Matrix &b);
// Pads to the next power of two of the largest dimension and switches to the classical method
// below `cutoff` (>= 1). Results equal the classical product up to floating-point rounding.
Matrix multiply_strassen(const Matrix &a, const Matrix &b, std::size_t cutoff = 32);

} // namespace ev
