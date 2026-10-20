// number_theory.hpp - gcd, extended Euclid, modular inverse and the Chinese Remainder Theorem.
//
// EV use (illustrative): station maintenance runs every m hours with offset a (hour ≡ a mod m).
// The CRT answers "when are several stations down at the same time?".
#pragma once

#include <cstdint>
#include <vector>

namespace ev {

std::int64_t gcd(std::int64_t a, std::int64_t b);                                   // gcd(0,0) == 0, result >= 0
std::int64_t extended_gcd(std::int64_t a, std::int64_t b, std::int64_t &x, std::int64_t &y);   // returns g = a·x + b·y
// inverse of a modulo m (m >= 1); returns -1 when gcd(a, m) != 1
std::int64_t mod_inverse(std::int64_t a, std::int64_t m);

// Solves x ≡ residues[i] (mod moduli[i]) for all i, moduli need NOT be pairwise coprime.
enum class CrtStatus { Solved, Inconsistent, Invalid };
struct CrtResult {
    CrtStatus status = CrtStatus::Invalid;
    std::int64_t x = 0;        // smallest non-negative solution
    std::int64_t modulus = 0;  // lcm of the moduli: all solutions are x + k·modulus
};
// Invalid when sizes differ, the list is empty, a modulus < 1, or the lcm exceeds 2^62.
CrtResult chinese_remainder(const std::vector<std::int64_t> &residues, const std::vector<std::int64_t> &moduli);

} // namespace ev
