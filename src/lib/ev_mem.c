#include "ev_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef union { max_align_t align; size_t n; } ev_hdr;

static size_t g_cur = 0, g_peak = 0;

static void ev_oom(size_t n) {
    fprintf(stderr, "fatal: out of memory requesting %zu bytes\n", n);
    exit(2);
}

void *ev_malloc(size_t n) {
    ev_hdr *h = (ev_hdr *)malloc(sizeof(ev_hdr) + (n ? n : 1));
    if (!h) ev_oom(n);
    h->n = n;
    g_cur += n;
    if (g_cur > g_peak) g_peak = g_cur;
    return h + 1;
}

void *ev_calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) ev_oom((size_t)-1);
    size_t n = count * size;
    void *p = ev_malloc(n);
    memset(p, 0, n);
    return p;
}

void *ev_realloc(void *p, size_t n) {
    if (!p) return ev_malloc(n);
    ev_hdr *h = ((ev_hdr *)p) - 1;
    size_t old = h->n;
    ev_hdr *nh = (ev_hdr *)realloc(h, sizeof(ev_hdr) + (n ? n : 1));
    if (!nh) ev_oom(n);
    g_cur = g_cur - old + n;
    if (g_cur > g_peak) g_peak = g_cur;
    nh->n = n;
    return nh + 1;
}

void ev_free(void *p) {
    if (!p) return;
    ev_hdr *h = ((ev_hdr *)p) - 1;
    g_cur -= h->n;
    free(h);
}

size_t ev_mem_current(void) { return g_cur; }
size_t ev_mem_peak(void) { return g_peak; }
void ev_mem_reset_peak(void) { g_peak = g_cur; }

double ev_now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

void ev_rng_seed(ev_rng *r, unsigned long long seed) {
    r->s = seed ? seed : 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 4; i++) ev_rng_next(r);
}

unsigned long long ev_rng_next(ev_rng *r) {
    unsigned long long x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return x * 0x2545F4914F6CDD1DULL;
}

double ev_rng_unit(ev_rng *r) { return (double)(ev_rng_next(r) >> 11) / 9007199254740992.0; }

long ev_rng_range(ev_rng *r, long lo, long hi) {
    if (hi <= lo) return lo;
    unsigned long long span = (unsigned long long)(hi - lo) + 1ULL;
    return lo + (long)(ev_rng_next(r) % span);
}
