// divide_conquer.cpp - maximum subarray, fast power, celebrity, classical and Strassen products.
#include "ev/algorithms/divide_conquer.hpp"

#include <cmath>
#include <stdexcept>

namespace ev {

// ======================================================================== maximum subarray
namespace {

// a "better" result: larger sum, then earlier lo, then earlier hi (deterministic tie rule)
bool better(const SubarrayResult &x, const SubarrayResult &y) {
    if (x.sum != y.sum) return x.sum > y.sum;
    if (x.lo != y.lo) return x.lo < y.lo;
    return x.hi < y.hi;
}

SubarrayResult best_crossing(const std::vector<double> &a, std::size_t lo, std::size_t mid, std::size_t hi) {
    // best run ending at mid (going left) + best run starting at mid+1 (going right)
    double s = 0, left_best = 0;
    std::size_t left_at = mid;
    bool have = false;
    for (std::size_t i = mid + 1; i-- > lo;) {
        s += a[i];
        if (!have || s >= left_best) { left_best = s; left_at = i; have = true; }   // >= keeps the earliest lo
    }
    s = 0;
    double right_best = 0;
    std::size_t right_at = mid + 1;
    have = false;
    for (std::size_t j = mid + 1; j <= hi; j++) {
        s += a[j];
        if (!have || s > right_best) { right_best = s; right_at = j; have = true; }  // > keeps the earliest hi
    }
    return {left_at, right_at, left_best + right_best};
}

SubarrayResult dc(const std::vector<double> &a, std::size_t lo, std::size_t hi) {
    if (lo == hi) return {lo, hi, a[lo]};
    std::size_t mid = lo + (hi - lo) / 2;
    SubarrayResult l = dc(a, lo, mid), r = dc(a, mid + 1, hi), c = best_crossing(a, lo, mid, hi);
    SubarrayResult best = l;
    if (better(c, best)) best = c;
    if (better(r, best)) best = r;
    return best;
}

void require_nonempty(const std::vector<double> &a) {
    if (a.empty()) throw std::invalid_argument("maximum subarray of an empty sequence is undefined");
}

} // namespace

SubarrayResult max_subarray_dc(const std::vector<double> &a) {
    require_nonempty(a);
    return dc(a, 0, a.size() - 1);
}

SubarrayResult max_subarray_kadane(const std::vector<double> &a) {
    require_nonempty(a);
    SubarrayResult best{0, 0, a[0]};
    double run = a[0];
    std::size_t start = 0;
    for (std::size_t i = 1; i < a.size(); i++) {
        // restart only when the running sum is negative (a zero prefix keeps the earlier start)
        if (run < 0) { run = a[i]; start = i; }
        else run += a[i];
        SubarrayResult cand{start, i, run};
        if (better(cand, best)) best = cand;
    }
    return best;
}

SubarrayResult max_subarray_brute(const std::vector<double> &a) {
    require_nonempty(a);
    SubarrayResult best{0, 0, a[0]};
    for (std::size_t i = 0; i < a.size(); i++) {
        double s = 0;
        for (std::size_t j = i; j < a.size(); j++) {
            s += a[j];
            SubarrayResult cand{i, j, s};
            if (better(cand, best)) best = cand;
        }
    }
    return best;
}

// ======================================================================== exponentiation
double pow_fast(double base, std::uint64_t e) {
    double result = 1.0;
    while (e > 0) {
        if (e & 1ULL) result *= base;
        base *= base;
        e >>= 1;
    }
    return result;
}

double pow_naive(double base, std::uint64_t e) {
    double r = 1.0;
    for (std::uint64_t i = 0; i < e; i++) r *= base;
    return r;
}

std::uint64_t mulmod(std::uint64_t a, std::uint64_t b, std::uint64_t m) {
    if (m == 0) throw std::invalid_argument("modulus must be positive");
    // Russian-peasant multiplication: never forms a*b, so no 64-bit overflow for m < 2^63
    a %= m;
    b %= m;
    std::uint64_t r = 0;
    while (b > 0) {
        if (b & 1ULL) {
            r += a;
            if (r >= m) r -= m;
        }
        a += a;
        if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}

std::uint64_t powmod(std::uint64_t base, std::uint64_t e, std::uint64_t m) {
    if (m == 0) throw std::invalid_argument("modulus must be positive");
    if (m >= (1ULL << 63)) throw std::invalid_argument("modulus must be below 2^63");
    std::uint64_t r = 1 % m;
    base %= m;
    while (e > 0) {
        if (e & 1ULL) r = mulmod(r, base, m);
        base = mulmod(base, base, m);
        e >>= 1;
    }
    return r;
}

// ======================================================================== celebrity
int find_celebrity(int n, const std::function<bool(int, int)> &knows, long *queries) {
    long q = 0;
    auto ask = [&](int a, int b) { q++; return knows(a, b); };
    int result = -1;
    if (n == 1) result = 0;
    if (n > 1) {
        // elimination: if a knows b, a is not a celebrity; otherwise b is not. One survivor.
        int cand = 0;
        for (int i = 1; i < n; i++)
            if (ask(cand, i)) cand = i;
        result = cand;
        for (int i = 0; i < n && result >= 0; i++) {
            if (i == cand) continue;
            if (ask(cand, i) || !ask(i, cand)) result = -1;
        }
    }
    if (queries) *queries = q;
    return result;
}

int find_celebrity_naive(int n, const std::function<bool(int, int)> &knows, long *queries) {
    long q = 0;
    int result = -1;
    for (int c = 0; c < n && result < 0; c++) {
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            if (i == c) continue;
            q += 2;
            if (knows(c, i) || !knows(i, c)) ok = false;
        }
        if (ok) result = c;
    }
    if (queries) *queries = q;
    return result;
}

// ======================================================================== matrices
Matrix::Matrix(std::size_t rows, std::size_t cols, double fill) : rows_(rows), cols_(cols), data_(rows * cols, fill) {}

Matrix Matrix::identity(std::size_t n) {
    Matrix m(n, n);
    for (std::size_t i = 0; i < n; i++) m(i, i) = 1.0;
    return m;
}

double Matrix::at(std::size_t r, std::size_t c) const {
    if (r >= rows_ || c >= cols_) throw std::out_of_range("matrix index out of range");
    return (*this)(r, c);
}

double Matrix::max_abs_diff(const Matrix &o) const {
    if (rows_ != o.rows_ || cols_ != o.cols_) throw std::invalid_argument("matrix shapes differ");
    double d = 0;
    for (std::size_t i = 0; i < data_.size(); i++) d = std::fmax(d, std::fabs(data_[i] - o.data_[i]));
    return d;
}

static void check_product(const Matrix &a, const Matrix &b) {
    if (a.rows() == 0 || a.cols() == 0 || b.rows() == 0 || b.cols() == 0)
        throw std::invalid_argument("matrix dimensions must be positive");
    if (a.cols() != b.rows())
        throw std::invalid_argument("cannot multiply " + std::to_string(a.rows()) + "x" + std::to_string(a.cols()) +
                                    " by " + std::to_string(b.rows()) + "x" + std::to_string(b.cols()));
}

Matrix multiply_classical(const Matrix &a, const Matrix &b) {
    check_product(a, b);
    Matrix c(a.rows(), b.cols());
    for (std::size_t i = 0; i < a.rows(); i++)
        for (std::size_t k = 0; k < a.cols(); k++) {   // i-k-j order walks rows of b contiguously
            double aik = a(i, k);
            if (aik == 0.0) continue;
            for (std::size_t j = 0; j < b.cols(); j++) c(i, j) += aik * b(k, j);
        }
    return c;
}

namespace {

// square n x n blocks stored row-major in a flat vector, n a power of two
using Block = std::vector<double>;

void add(const Block &x, const Block &y, Block &z) { for (std::size_t i = 0; i < z.size(); i++) z[i] = x[i] + y[i]; }
void sub(const Block &x, const Block &y, Block &z) { for (std::size_t i = 0; i < z.size(); i++) z[i] = x[i] - y[i]; }

void classical_block(const Block &x, const Block &y, Block &z, std::size_t n) {
    for (std::size_t i = 0; i < n * n; i++) z[i] = 0;
    for (std::size_t i = 0; i < n; i++)
        for (std::size_t k = 0; k < n; k++) {
            double v = x[i * n + k];
            if (v == 0.0) continue;
            for (std::size_t j = 0; j < n; j++) z[i * n + j] += v * y[k * n + j];
        }
}

void strassen_rec(const Block &A, const Block &B, Block &C, std::size_t n, std::size_t cutoff) {
    if (n <= cutoff) { classical_block(A, B, C, n); return; }
    const std::size_t h = n / 2, hh = h * h;
    Block a11(hh), a12(hh), a21(hh), a22(hh), b11(hh), b12(hh), b21(hh), b22(hh);
    for (std::size_t i = 0; i < h; i++)
        for (std::size_t j = 0; j < h; j++) {
            a11[i * h + j] = A[i * n + j];       a12[i * h + j] = A[i * n + j + h];
            a21[i * h + j] = A[(i + h) * n + j]; a22[i * h + j] = A[(i + h) * n + j + h];
            b11[i * h + j] = B[i * n + j];       b12[i * h + j] = B[i * n + j + h];
            b21[i * h + j] = B[(i + h) * n + j]; b22[i * h + j] = B[(i + h) * n + j + h];
        }
    Block t1(hh), t2(hh), m1(hh), m2(hh), m3(hh), m4(hh), m5(hh), m6(hh), m7(hh);
    add(a11, a22, t1); add(b11, b22, t2); strassen_rec(t1, t2, m1, h, cutoff);   // M1 = (A11+A22)(B11+B22)
    add(a21, a22, t1);                    strassen_rec(t1, b11, m2, h, cutoff);   // M2 = (A21+A22)B11
    sub(b12, b22, t2);                    strassen_rec(a11, t2, m3, h, cutoff);   // M3 = A11(B12-B22)
    sub(b21, b11, t2);                    strassen_rec(a22, t2, m4, h, cutoff);   // M4 = A22(B21-B11)
    add(a11, a12, t1);                    strassen_rec(t1, b22, m5, h, cutoff);   // M5 = (A11+A12)B22
    sub(a21, a11, t1); add(b11, b12, t2); strassen_rec(t1, t2, m6, h, cutoff);   // M6 = (A21-A11)(B11+B12)
    sub(a12, a22, t1); add(b21, b22, t2); strassen_rec(t1, t2, m7, h, cutoff);   // M7 = (A12-A22)(B21+B22)
    for (std::size_t i = 0; i < h; i++)
        for (std::size_t j = 0; j < h; j++) {
            std::size_t k = i * h + j;
            C[i * n + j] = m1[k] + m4[k] - m5[k] + m7[k];           // C11
            C[i * n + j + h] = m3[k] + m5[k];                       // C12
            C[(i + h) * n + j] = m2[k] + m4[k];                     // C21
            C[(i + h) * n + j + h] = m1[k] - m2[k] + m3[k] + m6[k]; // C22
        }
}

} // namespace

Matrix multiply_strassen(const Matrix &a, const Matrix &b, std::size_t cutoff) {
    check_product(a, b);
    if (cutoff < 1) throw std::invalid_argument("Strassen cutoff must be at least 1");
    std::size_t big = a.rows();
    if (a.cols() > big) big = a.cols();
    if (b.cols() > big) big = b.cols();
    std::size_t n = 1;
    while (n < big) n <<= 1;
    Block A(n * n, 0.0), B(n * n, 0.0), C(n * n, 0.0);
    for (std::size_t i = 0; i < a.rows(); i++)
        for (std::size_t j = 0; j < a.cols(); j++) A[i * n + j] = a(i, j);
    for (std::size_t i = 0; i < b.rows(); i++)
        for (std::size_t j = 0; j < b.cols(); j++) B[i * n + j] = b(i, j);
    strassen_rec(A, B, C, n, cutoff);
    Matrix c(a.rows(), b.cols());
    for (std::size_t i = 0; i < c.rows(); i++)
        for (std::size_t j = 0; j < c.cols(); j++) c(i, j) = C[i * n + j];
    return c;
}

} // namespace ev
