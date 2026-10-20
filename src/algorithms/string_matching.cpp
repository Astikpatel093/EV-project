// string_matching.cpp - four exact pattern matchers.
#include "ev/algorithms/string_matching.hpp"

#include <stdexcept>

namespace ev {

static void require_pattern(const std::string &p) {
    if (p.empty()) throw std::invalid_argument("pattern must not be empty");
}

std::vector<std::size_t> match_naive(const std::string &t, const std::string &p, MatchStats *st) {
    require_pattern(p);
    std::vector<std::size_t> out;
    std::uint64_t cmp = 0;
    const std::size_t n = t.size(), m = p.size();
    for (std::size_t s = 0; s + m <= n; s++) {
        std::size_t j = 0;
        while (j < m) {
            cmp++;
            if (t[s + j] != p[j]) break;
            j++;
        }
        if (j == m) out.push_back(s);
    }
    if (st) { st->comparisons = cmp; st->preprocessing = 0; }
    return out;
}

std::vector<std::size_t> match_rabin_karp(const std::string &t, const std::string &p, MatchStats *st) {
    require_pattern(p);
    std::vector<std::size_t> out;
    const std::size_t n = t.size(), m = p.size();
    if (st) *st = MatchStats();
    if (m > n) return out;
    const std::uint64_t B = 256, Q = 1000000007ULL;   // base and prime modulus; products < 2^63
    std::uint64_t h = 1;                               // B^(m-1) mod Q
    for (std::size_t i = 1; i < m; i++) h = h * B % Q;
    std::uint64_t hp = 0, ht = 0;
    for (std::size_t i = 0; i < m; i++) {
        hp = (hp * B + static_cast<unsigned char>(p[i])) % Q;
        ht = (ht * B + static_cast<unsigned char>(t[i])) % Q;
    }
    std::uint64_t cmp = 0;
    for (std::size_t s = 0;; s++) {
        if (hp == ht) {   // verify: equal hashes can still be different strings
            std::size_t j = 0;
            while (j < m) {
                cmp++;
                if (t[s + j] != p[j]) break;
                j++;
            }
            if (j == m) out.push_back(s);
        }
        if (s + m >= n) break;
        // roll: remove t[s], add t[s+m]
        std::uint64_t lead = static_cast<unsigned char>(t[s]) * h % Q;
        ht = (ht + Q - lead) % Q;
        ht = (ht * B + static_cast<unsigned char>(t[s + m])) % Q;
    }
    if (st) { st->comparisons = cmp; st->preprocessing = 2 * m; }
    return out;
}

std::vector<std::size_t> prefix_function(const std::string &p) {
    std::vector<std::size_t> pi(p.size(), 0);
    std::size_t k = 0;
    for (std::size_t i = 1; i < p.size(); i++) {
        while (k > 0 && p[i] != p[k]) k = pi[k - 1];
        if (p[i] == p[k]) k++;
        pi[i] = k;
    }
    return pi;
}

std::vector<std::size_t> match_automaton(const std::string &t, const std::string &p, MatchStats *st) {
    require_pattern(p);
    const std::size_t m = p.size();
    // delta[q][c] = next state after reading byte c in state q (q = matched prefix length)
    std::vector<std::vector<std::size_t>> delta(m + 1, std::vector<std::size_t>(256, 0));
    std::vector<std::size_t> pi = prefix_function(p);
    std::uint64_t pre = 0;
    for (std::size_t q = 0; q <= m; q++)
        for (int c = 0; c < 256; c++) {
            pre++;
            if (q < m && static_cast<unsigned char>(p[q]) == c) delta[q][static_cast<std::size_t>(c)] = q + 1;
            else if (q == 0) delta[q][static_cast<std::size_t>(c)] = 0;
            else delta[q][static_cast<std::size_t>(c)] = delta[pi[q - 1]][static_cast<std::size_t>(c)];
        }
    std::vector<std::size_t> out;
    std::size_t q = 0;
    std::uint64_t steps = 0;
    for (std::size_t i = 0; i < t.size(); i++) {
        q = delta[q][static_cast<unsigned char>(t[i])];
        steps++;
        if (q == m) out.push_back(i + 1 - m);
    }
    if (st) { st->comparisons = steps; st->preprocessing = pre; }
    return out;
}

std::vector<std::size_t> match_kmp(const std::string &t, const std::string &p, MatchStats *st) {
    require_pattern(p);
    std::vector<std::size_t> pi = prefix_function(p), out;
    std::uint64_t cmp = 0;
    std::size_t q = 0;
    for (std::size_t i = 0; i < t.size(); i++) {
        while (q > 0) {
            cmp++;
            if (t[i] == p[q]) break;
            q = pi[q - 1];
        }
        if (q == 0) cmp++;
        if (t[i] == p[q]) q++;
        if (q == p.size()) {
            out.push_back(i + 1 - p.size());
            q = pi[q - 1];
        }
    }
    if (st) { st->comparisons = cmp; st->preprocessing = p.size(); }
    return out;
}

} // namespace ev
