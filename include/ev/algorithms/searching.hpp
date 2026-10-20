// searching.hpp - binary search on sorted vectors (iterative and recursive), lower/upper bound.
//
// The vector must be sorted by the same key: `key_of(element)` gives the value compared with the
// searched key using operator<. Examples: search stations sorted by id with
//   binary_search_iterative(stations, 512, [](const ChargingStation &s) { return s.id; });
//
//  binary_search_iterative  returns the index of the LEFTMOST element whose key equals `key`, or -1.
//  binary_search_recursive  returns the index of the leftmost equal element, or -1 (same contract,
//                           written recursively: T(n) = T(n/2) + Θ(1) = Θ(log n), Θ(log n) stack).
//  lower_bound / upper_bound  first index with key >= k / key > k (size() if none).
#pragma once

#include <cstddef>
#include <vector>

namespace ev {

struct Identity {
    template <class T>
    const T &operator()(const T &x) const { return x; }
};

template <class T, class K, class KeyOf = Identity>
std::size_t lower_bound_index(const std::vector<T> &a, const K &key, KeyOf key_of = KeyOf()) {
    std::size_t lo = 0, hi = a.size();
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;   // no overflow, unlike (lo + hi) / 2
        if (key_of(a[mid]) < key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

template <class T, class K, class KeyOf = Identity>
std::size_t upper_bound_index(const std::vector<T> &a, const K &key, KeyOf key_of = KeyOf()) {
    std::size_t lo = 0, hi = a.size();
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        if (key < key_of(a[mid])) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

template <class T, class K, class KeyOf = Identity>
long binary_search_iterative(const std::vector<T> &a, const K &key, KeyOf key_of = KeyOf()) {
    std::size_t i = lower_bound_index(a, key, key_of);
    if (i < a.size() && !(key < key_of(a[i])) && !(key_of(a[i]) < key)) return static_cast<long>(i);
    return -1;
}

namespace detail {
// leftmost match inside [lo, hi), or -1
template <class T, class K, class KeyOf>
long bsearch_rec(const std::vector<T> &a, const K &key, KeyOf &key_of, std::size_t lo, std::size_t hi) {
    if (lo >= hi) return -1;
    std::size_t mid = lo + (hi - lo) / 2;
    const auto &k = key_of(a[mid]);
    if (k < key) return bsearch_rec(a, key, key_of, mid + 1, hi);
    if (key < k) return bsearch_rec(a, key, key_of, lo, mid);
    // equal: a match; an earlier equal element may still exist on the left
    long left = bsearch_rec(a, key, key_of, lo, mid);
    return left >= 0 ? left : static_cast<long>(mid);
}
} // namespace detail

template <class T, class K, class KeyOf = Identity>
long binary_search_recursive(const std::vector<T> &a, const K &key, KeyOf key_of = KeyOf()) {
    return detail::bsearch_rec(a, key, key_of, 0, a.size());
}

} // namespace ev
