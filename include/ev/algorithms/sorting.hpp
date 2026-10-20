// sorting.hpp - merge sort and quick sort on std::vector with a comparator.
//
// Both take `less(a, b)` (true when a must come before b) so they sort any record type: trips by
// departure time, stations by cost, benefit/cost ratios, MST edges by weight.
//
//  merge_sort  stable; T(n) = 2T(n/2) + Θ(n) = Θ(n log n) in every case; Θ(n) extra space.
//  quick_sort  in place, not stable; average Θ(n log n); worst Θ(n^2) (e.g. PivotRule::First on
//              already-sorted input). It uses an explicit stack: after each partition it
//              continues with the smaller part and saves the larger one, so at most O(log n)
//              ranges are pending even in the worst case (no deep recursion on bad inputs).
//
// An optional SortStats* counts comparisons (used by the benchmarks to show the n log n shape).
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "ev/core/rng.hpp"

namespace ev {

struct SortStats {
    std::uint64_t comparisons = 0;
};

enum class PivotRule { MedianOfThree, First, Random };

namespace detail {

template <class T, class Less>
bool counted_less(Less &less, const T &a, const T &b, SortStats *st) {
    if (st) st->comparisons++;
    return less(a, b);
}

template <class T, class Less>
void merge_sort_rec(std::vector<T> &a, std::vector<T> &buf, std::size_t lo, std::size_t hi, Less &less,
                    SortStats *st) {
    if (hi - lo < 2) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    merge_sort_rec(a, buf, lo, mid, less, st);
    merge_sort_rec(a, buf, mid, hi, less, st);
    // already in order: the two halves concatenate (keeps sorted input at Θ(n) comparisons)
    if (!counted_less(less, a[mid], a[mid - 1], st)) return;
    std::size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        // take from the right only when strictly smaller: equal keys keep their order (stable)
        if (counted_less(less, a[j], a[i], st)) buf[k++] = std::move(a[j++]);
        else buf[k++] = std::move(a[i++]);
    }
    while (i < mid) buf[k++] = std::move(a[i++]);
    while (j < hi) buf[k++] = std::move(a[j++]);
    for (std::size_t q = lo; q < hi; q++) a[q] = std::move(buf[q]);
}

// Hoare partition of a[lo..hi] (inclusive) around the value a[lo]; returns p such that
// every element of a[lo..p] <= pivot <= every element of a[p+1..hi].
template <class T, class Less>
std::size_t hoare_partition(std::vector<T> &a, std::size_t lo, std::size_t hi, Less &less, SortStats *st) {
    T pivot = a[lo];
    std::size_t i = lo, j = hi;
    bool first = true;
    for (;;) {
        if (!first) i++;
        while (counted_less(less, a[i], pivot, st)) i++;
        if (!first) j--;
        while (counted_less(less, pivot, a[j], st)) j--;
        first = false;
        if (i >= j) return j;
        std::swap(a[i], a[j]);
    }
}

} // namespace detail

template <class T, class Less>
void merge_sort(std::vector<T> &a, Less less, SortStats *stats = nullptr) {
    if (a.size() < 2) return;
    std::vector<T> buf(a.size());
    detail::merge_sort_rec(a, buf, 0, a.size(), less, stats);
}

template <class T, class Less>
void quick_sort(std::vector<T> &a, Less less, PivotRule rule = PivotRule::MedianOfThree,
                SortStats *stats = nullptr, std::uint64_t seed = 12345) {
    if (a.size() < 2) return;
    Rng rng(seed);
    std::size_t lo = 0, hi = a.size() - 1;   // inclusive bounds of the current range
    // explicit stack of pending ranges: always push the larger part, loop on the smaller one
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    for (;;) {
        while (lo < hi) {
            // choose a pivot and move it to a[lo]
            std::size_t p = lo;
            if (rule == PivotRule::Random) {
                p = lo + static_cast<std::size_t>(rng.range(0, static_cast<std::int64_t>(hi - lo)));
            } else if (rule == PivotRule::MedianOfThree && hi - lo >= 2) {
                std::size_t m = lo + (hi - lo) / 2;
                // median of a[lo], a[m], a[hi]
                const bool lm = detail::counted_less(less, a[lo], a[m], stats);
                const bool mh = detail::counted_less(less, a[m], a[hi], stats);
                const bool lh = detail::counted_less(less, a[lo], a[hi], stats);
                if (lm == mh) p = m;              // lo < m < hi  or  hi < m < lo
                else if (lm != lh) p = lo;        // m < lo < hi  or  hi < lo < m
                else p = hi;
            }
            std::swap(a[lo], a[p]);
            std::size_t cut = detail::hoare_partition(a, lo, hi, less, stats);
            // parts are [lo, cut] and [cut+1, hi]
            if (cut - lo < hi - cut - 1) {
                pending.emplace_back(cut + 1, hi);
                hi = cut;
            } else {
                pending.emplace_back(lo, cut);
                lo = cut + 1;
            }
        }
        if (pending.empty()) break;
        lo = pending.back().first;
        hi = pending.back().second;
        pending.pop_back();
    }
}

// Convenience overloads using operator<.
template <class T>
void merge_sort(std::vector<T> &a) { merge_sort(a, [](const T &x, const T &y) { return x < y; }); }
template <class T>
void quick_sort(std::vector<T> &a, PivotRule rule = PivotRule::MedianOfThree) {
    quick_sort(a, [](const T &x, const T &y) { return x < y; }, rule);
}

template <class T, class Less>
bool is_sorted_by(const std::vector<T> &a, Less less) {
    for (std::size_t i = 1; i < a.size(); i++)
        if (less(a[i], a[i - 1])) return false;
    return true;
}

} // namespace ev
