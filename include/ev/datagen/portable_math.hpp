// portable_math.hpp - exp, log, sin and cos written with + - * / only.
//
// The C library's std::sin/std::exp may differ in the last bit between compilers and operating
// systems, which would make "the same seed gives the same file" true only on one platform. The data
// generators therefore use these small series implementations: IEEE-754 basic arithmetic is
// correctly rounded everywhere, so the results are bit-identical on any x86-64 / ARM64 compiler that
// does not fuse multiply-adds (GCC/Clang/MSVC defaults for these targets). Accuracy ~1e-15 relative,
// far more than the generators need.
#pragma once

#include <cmath>

namespace ev {
namespace pmath {

constexpr double kLn2 = 0.6931471805599453094;
constexpr double kPi = 3.14159265358979323846;

inline double exp(double x) {
    if (x > 700) x = 700;
    if (x < -700) return 0.0;
    // x = k ln2 + r, |r| <= ln2/2
    double kd = x / kLn2;
    long k = static_cast<long>(kd < 0 ? kd - 0.5 : kd + 0.5);
    double r = x - static_cast<double>(k) * kLn2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 25; i++) {
        term = term * r / i;
        sum += term;
    }
    return std::ldexp(sum, static_cast<int>(k));   // exact scaling by 2^k
}

inline double log(double x) {
    if (!(x > 0)) return -1e308;
    int e = 0;
    double m = std::frexp(x, &e);   // exact: x = m * 2^e, m in [0.5, 1)
    if (m < 0.70710678118654752440) { m *= 2; e--; }
    // log(m) = 2 atanh(z), z = (m-1)/(m+1), |z| < 0.172
    double z = (m - 1) / (m + 1), z2 = z * z, term = z, sum = 0;
    for (int i = 1; i < 60; i += 2) {
        sum += term / i;
        term *= z2;
    }
    return 2 * sum + e * kLn2;
}

// sin and cos after reducing the argument to [-pi, pi]
inline double reduce(double x) {
    const double two_pi = 2 * kPi;
    double k = std::floor((x + kPi) / two_pi);   // floor is exact
    return x - k * two_pi;
}

inline double sin(double x) {
    x = reduce(x);
    double term = x, sum = x, x2 = x * x;
    for (int i = 1; i < 30; i++) {
        term = -term * x2 / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

inline double cos(double x) {
    x = reduce(x);
    double term = 1, sum = 1, x2 = x * x;
    for (int i = 1; i < 30; i++) {
        term = -term * x2 / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

} // namespace pmath
} // namespace ev
