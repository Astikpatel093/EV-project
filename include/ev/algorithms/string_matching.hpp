// string_matching.hpp - naive, Rabin-Karp, finite-automaton and Knuth-Morris-Pratt matching.
//
// EV use (illustrative): find fault patterns such as "FOR" (fault, offline, reset) in a station
// event log. All four return every occurrence (overlapping matches included) and count their
// work so the benchmarks can compare naive Θ(n·m) worst case with the linear methods.
//   naive        Θ((n-m+1)·m) comparisons worst case
//   Rabin-Karp   rolling hash, expected Θ(n+m); every hash hit is verified (no false matches)
//   automaton    Θ(m·|Σ|) preprocessing, exactly n transitions
//   KMP          Θ(m) prefix function, at most 2n comparisons
// An empty pattern is rejected with std::invalid_argument.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ev {

struct MatchStats {
    std::uint64_t comparisons = 0;   // character comparisons (automaton: transitions taken)
    std::uint64_t preprocessing = 0; // work units spent before scanning
};

std::vector<std::size_t> match_naive(const std::string &text, const std::string &pattern, MatchStats *st = nullptr);
std::vector<std::size_t> match_rabin_karp(const std::string &text, const std::string &pattern, MatchStats *st = nullptr);
std::vector<std::size_t> match_automaton(const std::string &text, const std::string &pattern, MatchStats *st = nullptr);
std::vector<std::size_t> match_kmp(const std::string &text, const std::string &pattern, MatchStats *st = nullptr);

// KMP failure function: pi[i] = length of the longest proper prefix of pattern[0..i] that is
// also a suffix of it.
std::vector<std::size_t> prefix_function(const std::string &pattern);

} // namespace ev
