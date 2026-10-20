#include "ev_sort.h"
#include "ev_mem.h"

#include <string.h>

unsigned long long ev_sort_comparisons = 0;

#define ELEM(b, i, sz) ((char *)(b) + (size_t)(i) * (sz))

static int cmpc(ev_cmp_fn cmp, const void *a, const void *b) {
    ev_sort_comparisons++;
    return cmp(a, b);
}

static void swap_bytes(void *a, void *b, size_t sz) {
    unsigned char *p = (unsigned char *)a, *q = (unsigned char *)b, t;
    if (p == q) return;
    while (sz--) { t = *p; *p++ = *q; *q++ = t; }
}

/* ---------------------------------------------------------------- merge sort */
static void insertion_sort(char *b, size_t n, size_t sz, ev_cmp_fn cmp, char *tmp) {
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, b + i * sz, sz);
        size_t j = i;
        while (j > 0 && cmpc(cmp, b + (j - 1) * sz, tmp) > 0) { /* '>' keeps it stable */
            memcpy(b + j * sz, b + (j - 1) * sz, sz);
            j--;
        }
        memcpy(b + j * sz, tmp, sz);
    }
}

static void merge_rec(char *b, char *aux, size_t lo, size_t hi, size_t sz, ev_cmp_fn cmp) {
    size_t n = hi - lo;
    if (n <= 16) { insertion_sort(b + lo * sz, n, sz, cmp, aux + lo * sz); return; }
    size_t mid = lo + n / 2;
    merge_rec(b, aux, lo, mid, sz, cmp);
    merge_rec(b, aux, mid, hi, sz, cmp);
    if (cmpc(cmp, b + (mid - 1) * sz, b + mid * sz) <= 0) return; /* already ordered */
    memcpy(aux + lo * sz, b + lo * sz, n * sz);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (cmpc(cmp, aux + j * sz, aux + i * sz) < 0) memcpy(b + (k++) * sz, aux + (j++) * sz, sz);
        else memcpy(b + (k++) * sz, aux + (i++) * sz, sz);
    }
    if (i < mid) memcpy(b + k * sz, aux + i * sz, (mid - i) * sz);
    if (j < hi) memcpy(b + k * sz, aux + j * sz, (hi - j) * sz);
}

void ev_merge_sort(void *base, size_t n, size_t size, ev_cmp_fn cmp) {
    if (n < 2 || size == 0) return;
    char *aux = (char *)ev_malloc(n * size);
    merge_rec((char *)base, aux, 0, n, size, cmp);
    ev_free(aux);
}

/* ---------------------------------------------------------------- quick sort */
static unsigned long long qs_rng = 88172645463325252ULL;
static size_t qs_rand(size_t n) {
    qs_rng ^= qs_rng << 13; qs_rng ^= qs_rng >> 7; qs_rng ^= qs_rng << 17;
    return (size_t)(qs_rng % n);
}

static size_t choose_pivot(char *b, size_t lo, size_t hi, size_t sz, ev_cmp_fn cmp, ev_pivot pv) {
    /* hi is inclusive */
    if (pv == EV_PIVOT_FIRST) return lo;
    if (pv == EV_PIVOT_RANDOM) return lo + qs_rand(hi - lo + 1);
    size_t mid = lo + (hi - lo) / 2;
    char *a = ELEM(b, lo, sz), *m = ELEM(b, mid, sz), *c = ELEM(b, hi, sz);
    if (cmpc(cmp, a, m) < 0) {
        if (cmpc(cmp, m, c) < 0) return mid;
        return cmpc(cmp, a, c) < 0 ? hi : lo;
    }
    if (cmpc(cmp, a, c) < 0) return lo;
    return cmpc(cmp, m, c) < 0 ? hi : mid;
}

void ev_quick_sort(void *base, size_t n, size_t size, ev_cmp_fn cmp, ev_pivot pivot) {
    if (n < 2 || size == 0) return;
    char *b = (char *)base;
    char *pv = (char *)ev_malloc(size);
    size_t lo = 0, hi = n - 1;
    /* explicit stack holds the deferred (larger) partitions: depth <= log2(n)+1 */
    size_t stack_lo[128], stack_hi[128];
    int sp = 0;
    for (;;) {
        while (lo < hi) {
            size_t p = choose_pivot(b, lo, hi, size, cmp, pivot);
            /* Classic Hoare partition with the pivot moved to lo: returns j in [lo, hi-1],
             * so both sides are strictly smaller than the input range. */
            swap_bytes(ELEM(b, p, size), ELEM(b, lo, size), size);
            memcpy(pv, ELEM(b, lo, size), size);
            long i = (long)lo - 1, jj = (long)hi + 1;
            for (;;) {
                do { i++; } while (cmpc(cmp, ELEM(b, i, size), pv) < 0);
                do { jj--; } while (cmpc(cmp, ELEM(b, jj, size), pv) > 0);
                if (i >= jj) break;
                swap_bytes(ELEM(b, i, size), ELEM(b, jj, size), size);
            }
            size_t j = (size_t)jj;
            /* partitions: [lo, j] and [j+1, hi] */
            size_t left_n = j - lo + 1, right_n = hi - j;
            if (left_n < right_n) {
                if (j + 1 < hi) { stack_lo[sp] = j + 1; stack_hi[sp] = hi; sp++; }
                hi = j;
            } else {
                if (lo < j) { stack_lo[sp] = lo; stack_hi[sp] = j; sp++; }
                lo = j + 1;
            }
        }
        if (sp == 0) break;
        sp--; lo = stack_lo[sp]; hi = stack_hi[sp];
    }
    ev_free(pv);
}

/* ---------------------------------------------------------------- binary search */
size_t ev_lower_bound(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmpc(cmp, ELEM(base, mid, size), key) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

size_t ev_upper_bound(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmpc(cmp, ELEM(base, mid, size), key) <= 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

long ev_bsearch_iter(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp) {
    size_t i = ev_lower_bound(key, base, n, size, cmp);
    if (i < n && cmpc(cmp, ELEM(base, i, size), key) == 0) return (long)i;
    return -1;
}

static long bs_rec(const void *key, const char *b, size_t lo, size_t hi, size_t sz, ev_cmp_fn cmp) {
    /* search in [lo, hi) ; T(n) = T(n/2) + O(1) */
    if (lo >= hi) return -1;
    size_t mid = lo + (hi - lo) / 2;
    int c = cmpc(cmp, b + mid * sz, key);
    if (c == 0) return (long)mid;
    if (c < 0) return bs_rec(key, b, mid + 1, hi, sz, cmp);
    return bs_rec(key, b, lo, mid, sz, cmp);
}

long ev_bsearch_rec(const void *key, const void *base, size_t n, size_t size, ev_cmp_fn cmp) {
    return bs_rec(key, (const char *)base, 0, n, size, cmp);
}
