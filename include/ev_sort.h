/* ev_sort.h - student implementations of merge sort, quick sort and binary search.
 * Interfaces mirror qsort/bsearch so they work on any record type.
 */
#ifndef EV_SORT_H
#define EV_SORT_H

#include <stddef.h>

typedef int (*ev_cmp_fn)(const void *a, const void *b);

typedef enum { EV_PIVOT_MEDIAN3 = 0, EV_PIVOT_FIRST = 1, EV_PIVOT_RANDOM = 2 } ev_pivot;

/* Global comparison counter (reset by caller) used by benchmarks. */
extern unsigned long long ev_sort_comparisons;

/* Stable top-down merge sort. T(n) = 2T(n/2) + Θ(n) = Θ(n log n); Θ(n) extra space. */
void ev_merge_sort(void *base, size_t n, size_t size, ev_cmp_fn cmp);

/* In-place quick sort (Hoare-style partition around chosen pivot).
 * Recurse on smaller side, loop on larger: O(log n) stack even in the worst case.
 * Average Θ(n log n); worst Θ(n^2) (e.g. EV_PIVOT_FIRST on sorted input). Not stable. */
void ev_quick_sort(void *base, size_t n, size_t size, ev_cmp_fn cmp, ev_pivot pivot);

/* Binary search on a sorted array. Return index of the LEFTMOST equal element or -1. */
long ev_bsearch_iter(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp);
/* Recursive variant; returns index of some equal element (leftmost not guaranteed) or -1. */
long ev_bsearch_rec(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp);
/* First index i with base[i] >= key (n if none). */
size_t ev_lower_bound(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp);
/* First index i with base[i] > key (n if none). */
size_t ev_upper_bound(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp);

#endif
