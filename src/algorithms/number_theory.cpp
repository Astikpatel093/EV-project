// number_theory.cpp - Euclid, modular inverse and the generalised Chinese Remainder Theorem.
#include "ev/algorithms/number_theory.hpp"

#include "ev/algorithms/divide_conquer.hpp"   // mulmod

namespace ev {

std::int64_t gcd(std::int64_t a, std::int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

std::int64_t extended_gcd(std::int64_t a, std::int64_t b, std::int64_t &x, std::int64_t &y) {
    // iterative extended Euclid: invariant  old_r = a·old_x + b·old_y
    std::int64_t old_r = a, r = b, old_x = 1, nx = 0, old_y = 0, ny = 1;
    while (r != 0) {
        std::int64_t q = old_r / r, t;
        t = old_r - q * r; old_r = r; r = t;
        t = old_x - q * nx; old_x = nx; nx = t;
        t = old_y - q * ny; old_y = ny; ny = t;
    }
    if (old_r < 0) { old_r = -old_r; old_x = -old_x; old_y = -old_y; }
    x = old_x;
    y = old_y;
    return old_r;
}

std::int64_t mod_inverse(std::int64_t a, std::int64_t m) {
    if (m < 1) return -1;
    if (m == 1) return 0;
    std::int64_t x, y;
    std::int64_t g = extended_gcd(((a % m) + m) % m, m, x, y);
    if (g != 1) return -1;
    return ((x % m) + m) % m;
}

CrtResult chinese_remainder(const std::vector<std::int64_t> &a, const std::vector<std::int64_t> &m) {
    CrtResult res;
    if (a.empty() || a.size() != m.size()) return res;
    for (std::int64_t mi : m)
        if (mi < 1) return res;
    const std::int64_t LIMIT = std::int64_t(1) << 62;
    std::int64_t x = ((a[0] % m[0]) + m[0]) % m[0], M = m[0];
    for (std::size_t i = 1; i < a.size(); i++) {
        std::int64_t ai = ((a[i] % m[i]) + m[i]) % m[i], mi = m[i];
        std::int64_t g = gcd(M, mi);
        std::int64_t diff = ai - x;   // |diff| < 2^62, no overflow
        if (diff % g != 0) { res.status = CrtStatus::Inconsistent; return res; }
        std::int64_t mg = mi / g;
        if (M / g > LIMIT / mi) return res;   // lcm too large -> Invalid
        std::int64_t lcm = (M / g) * mi;
        // x' = x + M·t where t ≡ (diff/g)·inv(M/g) (mod mi/g)
        std::int64_t inv = mod_inverse(M / g, mg);
        std::int64_t d = ((diff / g) % mg + mg) % mg;
        std::int64_t t = static_cast<std::int64_t>(mulmod(static_cast<std::uint64_t>(d), static_cast<std::uint64_t>(inv),
                                                          static_cast<std::uint64_t>(mg)));
        // x + M·t < M + M·mg = M·(mg+1) <= 2·lcm <= 2^63: compute modulo lcm without overflow
        std::uint64_t step = mulmod(static_cast<std::uint64_t>(M), static_cast<std::uint64_t>(t), static_cast<std::uint64_t>(lcm));
        std::uint64_t nx = (static_cast<std::uint64_t>(x) + step) % static_cast<std::uint64_t>(lcm);
        x = static_cast<std::int64_t>(nx);
        M = lcm;
    }
    res.status = CrtStatus::Solved;
    res.x = x;
    res.modulus = M;
    return res;
}

} // namespace ev
