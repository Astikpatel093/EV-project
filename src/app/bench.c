/* ev33_bench - benchmark runner: time, peak heap, solution quality and scalability
 * for every library module on small / medium / large / adversarial inputs.
 *
 *   ev33_bench [--quick] [--out results/bench.csv] [--data-root data] [--only FAMILY]
 *
 * CSV columns: family,algorithm,dataset,n,ms,peak_bytes,quality,check,note
 *   quality = a family-specific score (comparisons, value/optimum, km, ...)
 *   check   = 1 when the result matched the oracle / sibling algorithm, 0 otherwise
 */
#include "ev_dnc.h"
#include "ev_dp.h"
#include "ev_flow.h"
#include "ev_graph.h"
#include "ev_greedy.h"
#include "ev_mem.h"
#include "ev_model.h"
#include "ev_nt.h"
#include "ev_plan.h"
#include "ev_search.h"
#include "ev_sort.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static FILE *g_csv;
static int g_quick = 0;
static const char *g_only = NULL;
static long g_rows = 0, g_fail = 0;

#define MEASURE(ms, peak, ...) do { size_t _b = ev_mem_current(); ev_mem_reset_peak(); double _t = ev_now_ms(); \
        __VA_ARGS__; (ms) = ev_now_ms() - _t; (peak) = ev_mem_peak() - _b; } while (0)

static void emit(const char *fam, const char *alg, const char *ds, long n, double ms, size_t peak, double quality,
                 int check, const char *note) {
    fprintf(g_csv, "%s,%s,%s,%ld,%.4f,%zu,%.6g,%d,%s\n", fam, alg, ds, n, ms, peak, quality, check, note ? note : "");
    printf("  %-11s %-18s %-12s n=%-8ld %10.3f ms %11zu B  q=%-12.6g %s %s\n", fam, alg, ds, n, ms, peak, quality,
           check ? "ok " : "BAD", note ? note : "");
    fflush(g_csv);
    g_rows++;
    if (!check) g_fail++;
}

static int want(const char *fam) { return !g_only || !strcmp(g_only, fam); }

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

/* ======================================================================== sorting */
static void bench_sort(void) {
    if (!want("sort")) return;
    printf("[sort]\n");
    long sizes[3] = { 1000, 100000, 1000000 };
    if (g_quick) { sizes[1] = 10000; sizes[2] = 100000; }
    const char *dn[4] = { "small", "medium", "large", "adversarial" };
    ev_rng r;
    for (int d = 0; d < 4; d++) {
        long n = d < 3 ? sizes[d] : (g_quick ? 5000 : 20000);
        int *src = (int *)ev_malloc(sizeof(int) * (size_t)n), *a = (int *)ev_malloc(sizeof(int) * (size_t)n), *ref = (int *)ev_malloc(sizeof(int) * (size_t)n);
        ev_rng_seed(&r, 100 + d);
        for (long i = 0; i < n; i++) src[i] = d < 3 ? (int)ev_rng_range(&r, 0, 1000000000) : (int)i; /* adversarial: already sorted */
        memcpy(ref, src, sizeof(int) * (size_t)n);
        qsort(ref, (size_t)n, sizeof(int), cmp_int);
        struct { const char *name; int kind; } algs[] = {
            { "merge", 0 }, { "quick-median3", 1 }, { "quick-first", 2 }, { "quick-random", 3 }, { "libc-qsort", 4 } };
        for (int k = 0; k < 5; k++) {
            if (algs[k].kind == 2 && d == 2) continue; /* first-pivot on 1e6 random is fine but slow to show; skip */
            memcpy(a, src, sizeof(int) * (size_t)n);
            ev_sort_comparisons = 0;
            double ms; size_t pk;
            MEASURE(ms, pk, {
                switch (algs[k].kind) {
                case 0: ev_merge_sort(a, (size_t)n, sizeof(int), cmp_int); break;
                case 1: ev_quick_sort(a, (size_t)n, sizeof(int), cmp_int, EV_PIVOT_MEDIAN3); break;
                case 2: ev_quick_sort(a, (size_t)n, sizeof(int), cmp_int, EV_PIVOT_FIRST); break;
                case 3: ev_quick_sort(a, (size_t)n, sizeof(int), cmp_int, EV_PIVOT_RANDOM); break;
                default: qsort(a, (size_t)n, sizeof(int), cmp_int); break;
                }
            });
            int ok = memcmp(a, ref, sizeof(int) * (size_t)n) == 0;
            emit("sort", algs[k].name, dn[d], n, ms, pk, algs[k].kind == 4 ? 0 : (double)ev_sort_comparisons, ok,
                 d == 3 ? "pre-sorted input; quality=comparisons" : "quality=comparisons");
        }
        ev_free(src); ev_free(a); ev_free(ref);
    }
}

/* ======================================================================== searching */
static void bench_search(void) {
    if (!want("search")) return;
    printf("[search]\n");
    long sizes[4] = { 1000, 100000, 1000000, 1000000 };
    const char *dn[4] = { "small", "medium", "large", "adversarial" };
    ev_rng r;
    for (int d = 0; d < 4; d++) {
        long n = sizes[d];
        int *a = (int *)ev_malloc(sizeof(int) * (size_t)n);
        for (long i = 0; i < n; i++) a[i] = d == 3 ? 7 : (int)(i * 3); /* adversarial: all keys equal */
        long q = g_quick ? 10000 : 100000;
        int *keys = (int *)ev_malloc(sizeof(int) * (size_t)q);
        ev_rng_seed(&r, 7 + d);
        for (long i = 0; i < q; i++) keys[i] = d == 3 ? 7 : (int)ev_rng_range(&r, 0, 3 * n);
        long hits[3] = { 0, 0, 0 };
        double ms; size_t pk;
        MEASURE(ms, pk, for (long i = 0; i < q; i++) hits[0] += ev_bsearch_iter(&keys[i], a, (size_t)n, sizeof(int), cmp_int) >= 0);
        emit("search", "binary-iterative", dn[d], n, ms, pk, (double)hits[0], 1, "q queries; quality=hits");
        MEASURE(ms, pk, for (long i = 0; i < q; i++) hits[1] += ev_bsearch_rec(&keys[i], a, (size_t)n, sizeof(int), cmp_int) >= 0);
        emit("search", "binary-recursive", dn[d], n, ms, pk, (double)hits[1], hits[1] == hits[0], "quality=hits");
        long lq = q < 1000 ? q : 1000;
        long lh = 0;
        MEASURE(ms, pk, for (long i = 0; i < lq; i++) { for (long k = 0; k < n; k++) if (a[k] == keys[i]) { lh++; break; } });
        long ih = 0;
        for (long i = 0; i < lq; i++) ih += ev_bsearch_iter(&keys[i], a, (size_t)n, sizeof(int), cmp_int) >= 0;
        emit("search", "linear-scan", dn[d], n, ms * ((double)q / lq), pk, (double)lh, lh == ih, "time extrapolated from 1000 queries");
        ev_free(a); ev_free(keys);
    }
}

/* ======================================================================== divide and conquer */
typedef struct { int n; unsigned char *m; } knows_mat;
static int knows_m(void *c, int a, int b) { knows_mat *k = (knows_mat *)c; return k->m[(size_t)a * k->n + b]; }

static void bench_dnc(void) {
    if (!want("dnc")) return;
    printf("[divide-and-conquer]\n");
    ev_rng r;
    const char *dn[4] = { "small", "medium", "large", "adversarial" };
    long ms_sizes[4] = { 1000, 100000, 1000000, 1000000 };
    if (g_quick) { ms_sizes[2] = ms_sizes[3] = 100000; }
    for (int d = 0; d < 4; d++) {
        long n = ms_sizes[d];
        double *a = (double *)ev_malloc(sizeof(double) * (size_t)n);
        ev_rng_seed(&r, 11 + d);
        for (long i = 0; i < n; i++) a[i] = d == 3 ? -1.0 - ev_rng_unit(&r) : ev_rng_unit(&r) * 200 - 100;
        ev_subarray x, y;
        double ms; size_t pk;
        MEASURE(ms, pk, x = ev_max_subarray_dc(a, n));
        MEASURE(ms, pk, x = ev_max_subarray_dc(a, n));
        double ms2; size_t pk2;
        MEASURE(ms2, pk2, y = ev_max_subarray_kadane(a, n));
        int ok = fabs(x.sum - y.sum) < 1e-6 * (1 + fabs(y.sum));
        emit("maxsubarray", "divide-conquer", dn[d], n, ms, pk, x.sum, ok, d == 3 ? "all negative" : "");
        emit("maxsubarray", "kadane", dn[d], n, ms2, pk2, y.sum, 1, "");
        if (n <= 1000) {
            ev_subarray z;
            MEASURE(ms, pk, z = ev_max_subarray_brute(a, n));
            emit("maxsubarray", "brute-n2", dn[d], n, ms, pk, z.sum, fabs(z.sum - y.sum) < 1e-6, "");
        }
        ev_free(a);
    }
    unsigned long long ex[4] = { 1000ULL, 100000ULL, 10000000ULL, 10000000ULL };
    for (int d = 0; d < 4; d++) {
        double b = d == 3 ? 1.0000001 : 0.9998, f1 = 0, f2 = 0;
        double ms, ms2; size_t pk;
        MEASURE(ms, pk, for (int rep = 0; rep < 1000; rep++) f1 += ev_pow_fast(b, ex[d] + (unsigned)rep % 2));
        MEASURE(ms2, pk, f2 = ev_pow_naive(b, ex[d]));
        double ref = pow(b, (double)ex[d]);
        (void)pk;
        emit("power", "fast-x1000", dn[d], (long)ex[d], ms, 0, f1 / 1000, fabs(ev_pow_fast(b, ex[d]) - ref) <= 1e-9 * fabs(ref) + 1e-300, "time for 1000 calls");
        emit("power", "naive", dn[d], (long)ex[d], ms2, 0, f2, fabs(f2 - ref) <= 1e-6 * fabs(ref) + 1e-300, "");
    }
    int cn[4] = { 200, 1000, 4000, 4000 };
    if (g_quick) cn[2] = cn[3] = 2000;
    for (int d = 0; d < 4; d++) {
        int n = cn[d];
        knows_mat k = { n, (unsigned char *)ev_calloc((size_t)n * n, 1) };
        ev_rng_seed(&r, 31 + d);
        int celeb = (int)ev_rng_range(&r, 0, n - 1);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                if (d < 3) k.m[(size_t)i * n + j] = (j == celeb) ? 1 : (i == celeb ? 0 : ev_rng_unit(&r) < 0.3);
                else k.m[(size_t)i * n + j] = ev_rng_unit(&r) < 0.5;   /* adversarial: (almost surely) no celebrity */
            }
        long q1, q2;
        int c1, c2;
        double ms, ms2; size_t pk, pk2;
        MEASURE(ms, pk, c1 = ev_celebrity_dc(n, knows_m, &k, &q1));
        MEASURE(ms2, pk2, c2 = ev_celebrity_naive(n, knows_m, &k, &q2));
        int ok = (c1 == c2) && (d == 3 || c1 == celeb);
        emit("celebrity", "divide-conquer", dn[d], n, ms, pk, (double)q1, ok, "quality=knows() queries");
        emit("celebrity", "naive-n2", dn[d], n, ms2, pk2, (double)q2, ok, "quality=knows() queries");
        ev_free(k.m);
    }
    int mn[5] = { 64, 128, 256, 512, 257 };
    const char *mdn[5] = { "small", "medium", "large", "xlarge", "adversarial" };
    for (int d = 0; d < 5; d++) {
        int n = mn[d];
        if (g_quick && n > 256) continue;
        size_t nn = (size_t)n * n;
        double *A = (double *)ev_malloc(sizeof(double) * nn), *B = (double *)ev_malloc(sizeof(double) * nn);
        double *C1 = (double *)ev_malloc(sizeof(double) * nn), *C2 = (double *)ev_malloc(sizeof(double) * nn), *C3 = (double *)ev_malloc(sizeof(double) * nn);
        ev_rng_seed(&r, 41 + d);
        for (size_t i = 0; i < nn; i++) { A[i] = ev_rng_unit(&r); B[i] = ev_rng_unit(&r); }
        double ms1, ms2, ms3; size_t p1, p2, p3;
        MEASURE(ms1, p1, ev_matmul_classic(A, B, C1, n));
        MEASURE(ms2, p2, ev_matmul_strassen(A, B, C2, n, 64));
        MEASURE(ms3, p3, ev_matmul_strassen(A, B, C3, n, 16));
        double e2 = 0, e3 = 0;
        for (size_t i = 0; i < nn; i++) { e2 = fmax(e2, fabs(C1[i] - C2[i])); e3 = fmax(e3, fabs(C1[i] - C3[i])); }
        emit("matmul", "classical", mdn[d], n, ms1, p1, 0, 1, "");
        emit("matmul", "strassen-cut64", mdn[d], n, ms2, p2, e2, e2 < 1e-6 * n, d == 4 ? "padded to 512; quality=max|err|" : "quality=max|err|");
        emit("matmul", "strassen-cut16", mdn[d], n, ms3, p3, e3, e3 < 1e-6 * n, "quality=max|err|");
        ev_free(A); ev_free(B); ev_free(C1); ev_free(C2); ev_free(C3);
    }
}

/* ======================================================================== greedy */
static void bench_greedy(void) {
    if (!want("greedy")) return;
    printf("[greedy]\n");
    ev_rng r;
    const char *dn[4] = { "small", "medium", "large", "adversarial" };
    long sizes[4] = { 1000, 100000, 1000000, 100000 };
    if (g_quick) sizes[2] = 100000;
    for (int d = 0; d < 4; d++) {
        long n = sizes[d];
        ev_interval *iv = (ev_interval *)ev_malloc(sizeof(ev_interval) * (size_t)n);
        int *out = (int *)ev_malloc(sizeof(int) * (size_t)n);
        ev_rng_seed(&r, 51 + d);
        for (long i = 0; i < n; i++) {
            double s = d == 3 ? 0 : ev_rng_unit(&r) * 1440.0 * (n / 1000.0);
            double len = d == 3 ? 60 : 10 + ev_rng_unit(&r) * 50;           /* adversarial: all identical */
            iv[i].id = (int)i; iv[i].start = s; iv[i].end = s + len;
        }
        int a, p, dep;
        double ms; size_t pk;
        MEASURE(ms, pk, a = ev_activity_select(iv, (int)n, out));
        emit("intervals", "activity-selection", dn[d], n, ms, pk, a, d != 3 || a == 1, "quality=sessions on 1 charger");
        MEASURE(ms, pk, p = ev_interval_partition(iv, (int)n, out));
        dep = ev_interval_depth(iv, (int)n);
        emit("intervals", "interval-partition", dn[d], n, ms, pk, p, p == dep, "quality=chargers; check=depth bound");
        ev_free(iv); ev_free(out);
    }
    long hs[3] = { 10000, 1000000, 1000000 };
    if (g_quick) hs[1] = hs[2] = 200000;
    const char *hn[3] = { "skewed-small", "skewed-large", "adversarial" };
    for (int d = 0; d < 3; d++) {
        long n = hs[d];
        unsigned char *t = (unsigned char *)ev_malloc((size_t)n), *dec = (unsigned char *)ev_malloc((size_t)n);
        ev_rng_seed(&r, 61 + d);
        for (long i = 0; i < n; i++) {
            if (d == 2) t[i] = (unsigned char)ev_rng_range(&r, 0, 255);       /* uniform bytes: incompressible */
            else { double u = ev_rng_unit(&r); t[i] = (unsigned char)(u < 0.5 ? 'C' : u < 0.8 ? 'E' : u < 0.93 ? 'I' : u < 0.97 ? 'F' : u < 0.99 ? 'O' : 'R'); }
        }
        ev_huff *h = NULL;
        unsigned char *bits = NULL;
        size_t nb = 0, nd = 0;
        double ms; size_t pk;
        MEASURE(ms, pk, { h = ev_huff_build(t, (size_t)n); nb = ev_huff_encode(h, t, (size_t)n, &bits); nd = ev_huff_decode(h, bits, nb, dec, (size_t)n); });
        int ok = nd == (size_t)n && memcmp(t, dec, (size_t)n) == 0;
        emit("huffman", "build+encode+decode", hn[d], n, ms, pk, ev_huff_avg_bits(h), ok, "quality=bits/symbol");
        ev_free(bits); ev_huff_free(h); ev_free(t); ev_free(dec);
    }
}

/* ======================================================================== graphs */
static ev_graph *grid_graph(int side, ev_rng *r, int negative, int **psrc, int **pdst, double **pw, int *pm) {
    int n = side * side, cap = n * 4, m = 0;
    int *s = (int *)ev_malloc(sizeof(int) * (size_t)cap), *d = (int *)ev_malloc(sizeof(int) * (size_t)cap);
    double *w = (double *)ev_malloc(sizeof(double) * (size_t)cap);
    double *h = (double *)ev_malloc(sizeof(double) * (size_t)n);
    for (int i = 0; i < n; i++) h[i] = ev_rng_unit(r) * 3;
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
            int u = y * side + x;
            int nb[2] = { x + 1 < side ? u + 1 : -1, y + 1 < side ? u + side : -1 };
            for (int k = 0; k < 2; k++) {
                int v = nb[k];
                if (v < 0) continue;
                double base = 1 + ev_rng_unit(r) * 4;
                /* "energy" weights: climbing costs, descending refunds 60% (potential-like: no negative cycles) */
                double dz = h[v] - h[u];
                double wuv = negative ? base * 0.3 + (dz > 0 ? dz : 0.6 * dz) : base;
                double wvu = negative ? base * 0.3 + (dz < 0 ? -dz : -0.6 * dz) : base;
                s[m] = u; d[m] = v; w[m] = wuv; m++;
                s[m] = v; d[m] = u; w[m] = wvu; m++;
            }
        }
    ev_free(h);
    ev_graph *g = ev_graph_build(n, m, s, d, w);
    if (psrc) { *psrc = s; *pdst = d; *pw = w; *pm = m; } else { ev_free(s); ev_free(d); ev_free(w); }
    return g;
}

static void bench_graphs(void) {
    if (!want("graph")) return;
    printf("[shortest paths]\n");
    ev_rng r;
    int sides[3] = { 10, 30, 60 };
    const char *dn[3] = { "small", "medium", "large" };
    for (int d = 0; d < 3; d++) {
        int side = sides[d], n = side * side;
        for (int neg = 0; neg <= 1; neg++) {
            ev_rng_seed(&r, 71 + d);
            ev_graph *g = grid_graph(side, &r, neg, NULL, NULL, NULL, NULL);
            double *dist = (double *)ev_malloc(sizeof(double) * (size_t)n), *dist2 = (double *)ev_malloc(sizeof(double) * (size_t)n);
            int *pa = (int *)ev_malloc(sizeof(int) * (size_t)n);
            double ms; size_t pk;
            char ds[32];
            snprintf(ds, sizeof ds, "%s%s", dn[d], neg ? "-neg" : "");
            int passes = 0;
            MEASURE(ms, pk, ev_bellman_ford(g, 0, dist2, pa, &passes));
            char note[64];
            snprintf(note, sizeof note, "single source; %d passes", passes);
            emit("sssp", "bellman-ford", ds, n, ms, pk, dist2[n - 1], 1, note);
            if (!neg) {
                MEASURE(ms, pk, ev_dijkstra(g, 0, dist, pa));
                emit("sssp", "dijkstra", ds, n, ms, pk, dist[n - 1], fabs(dist[n - 1] - dist2[n - 1]) < 1e-9, "single source");
                MEASURE(ms, pk, for (int s = 0; s < n; s++) ev_dijkstra(g, s, dist, pa));
                emit("apsp", "dijkstra-x-n", ds, n, ms, pk, 0, 1, "all sources");
            } else {
                ev_dijkstra(g, 0, dist, pa);
                emit("sssp", "dijkstra(on-negative)", ds, n, 0, 0, dist[n - 1], 1,
                     fabs(dist[n - 1] - dist2[n - 1]) < 1e-9 ? "same answer here (no luck guaranteed)" : "WRONG answer: negative arcs break Dijkstra");
            }
            if (n <= 900 || !g_quick) {
                if (n <= 3600) {
                    double *D = NULL;
                    int *nx = NULL;
                    MEASURE(ms, pk, {
                        D = (double *)ev_malloc(sizeof(double) * (size_t)n * n);
                        nx = (int *)ev_malloc(sizeof(int) * (size_t)n * n);
                        ev_floyd_warshall(g, D, nx);
                    });
                    emit("apsp", "floyd-warshall", ds, n, ms, pk, D[n - 1], fabs(D[n - 1] - dist2[n - 1]) < 1e-9, "all pairs incl. n^2 tables");
                    ev_free(D); ev_free(nx);
                }
            }
            ev_free(dist); ev_free(dist2); ev_free(pa);
            ev_graph_free(g);
        }
    }
    /* adversarial for Bellman-Ford: a path whose arcs are scanned in reverse order -> V-1 passes */
    {
        int n = g_quick ? 1000 : 3000, m = n - 1;
        int *s = (int *)ev_malloc(sizeof(int) * (size_t)m), *dd = (int *)ev_malloc(sizeof(int) * (size_t)m);
        double *w = (double *)ev_malloc(sizeof(double) * (size_t)m);
        /* vertex ids reversed so that CSR scan order visits the far end first */
        for (int k = 0; k < m; k++) { s[k] = n - 1 - k; dd[k] = n - 2 - k; w[k] = 1; }
        ev_graph *g = ev_graph_build(n, m, s, dd, w);
        double *dist = (double *)ev_malloc(sizeof(double) * (size_t)n);
        int passes;
        double ms; size_t pk;
        MEASURE(ms, pk, ev_bellman_ford(g, n - 1, dist, NULL, &passes));
        char note[64];
        snprintf(note, sizeof note, "reverse-ordered path; %d passes", passes);
        emit("sssp", "bellman-ford", "adversarial", n, ms, pk, dist[0], dist[0] == n - 1, note);
        MEASURE(ms, pk, ev_dijkstra(g, n - 1, dist, NULL));
        emit("sssp", "dijkstra", "adversarial", n, ms, pk, dist[0], dist[0] == n - 1, "");
        ev_free(dist); ev_free(s); ev_free(dd); ev_free(w); ev_graph_free(g);
    }
    printf("[minimum spanning tree]\n");
    int mn[4] = { 100, 500, 1500, 1500 };
    const char *mdn[4] = { "small", "medium", "large", "adversarial" };
    if (g_quick) mn[2] = mn[3] = 800;
    for (int d = 0; d < 4; d++) {
        int n = mn[d];
        double *X = (double *)ev_malloc(sizeof(double) * (size_t)n), *Y = (double *)ev_malloc(sizeof(double) * (size_t)n);
        ev_rng_seed(&r, 81 + d);
        for (int i = 0; i < n; i++) { X[i] = ev_rng_unit(&r) * 100; Y[i] = d == 3 ? 0 : ev_rng_unit(&r) * 100; }
        double *W = (double *)ev_malloc(sizeof(double) * (size_t)n * n);
        int m = n * (n - 1) / 2, e = 0;
        int *eu = (int *)ev_malloc(sizeof(int) * (size_t)m), *ev = (int *)ev_malloc(sizeof(int) * (size_t)m);
        double *ew = (double *)ev_malloc(sizeof(double) * (size_t)m);
        for (int a = 0; a < n; a++)
            for (int b = 0; b < n; b++) {
                double w = d == 3 ? 1.0 : hypot(X[a] - X[b], Y[a] - Y[b]); /* adversarial: all weights tie */
                W[(size_t)a * n + b] = a == b ? 0 : w;
                if (a < b) { eu[e] = a; ev[e] = b; ew[e] = w; e++; }
            }
        int *par = (int *)ev_malloc(sizeof(int) * (size_t)n), *ch = (int *)ev_malloc(sizeof(int) * (size_t)n), nch;
        double wp, wk, ms1, ms2; size_t p1, p2;
        MEASURE(ms1, p1, wp = ev_prim_dense(n, W, par));
        MEASURE(ms2, p2, wk = ev_kruskal(n, m, eu, ev, ew, ch, &nch));
        int ok = fabs(wp - wk) < 1e-6 * (1 + wk);
        emit("mst", "prim-dense", mdn[d], n, ms1, p1, wp, ok, "complete graph; quality=weight");
        emit("mst", "kruskal", mdn[d], n, ms2, p2, wk, ok, d == 3 ? "all weights equal" : "");
        ev_free(X); ev_free(Y); ev_free(W); ev_free(eu); ev_free(ev); ev_free(ew); ev_free(par); ev_free(ch);
    }
}

/* ======================================================================== DP + search */
static void bench_knapsack(void) {
    if (!want("knapsack")) return;
    printf("[knapsack: DP vs branch-and-bound vs greedy]\n");
    struct { const char *name; int n; int corr; } cases[] = {
        { "small", 20, 0 }, { "medium", 60, 0 }, { "large", 200, 0 }, { "weak-corr", 60, 1 }, { "adversarial", 40, 2 } };
    ev_rng r;
    for (int c = 0; c < 5; c++) {
        int n = cases[c].n;
        int *w = (int *)ev_malloc(sizeof(int) * (size_t)n);
        long *v = (long *)ev_malloc(sizeof(long) * (size_t)n);
        ev_rng_seed(&r, 91 + c);
        long tw = 0;
        for (int i = 0; i < n; i++) {
            w[i] = (int)ev_rng_range(&r, 10, 100);
            if (cases[c].corr == 0) v[i] = ev_rng_range(&r, 10, 100);
            else if (cases[c].corr == 1) v[i] = w[i] + ev_rng_range(&r, -10, 10) + 10;
            else v[i] = w[i] + 10;                                  /* strongly correlated: hardest for B&B */
            tw += w[i];
        }
        int W = (int)(tw / 2);
        int *take = (int *)ev_malloc(sizeof(int) * (size_t)n);
        long opt, val;
        double ms; size_t pk;
        MEASURE(ms, pk, opt = ev_knapsack01(n, w, v, W, take));
        int ok = 1;
        if (n <= 20) ok = ev_knapsack_brute(n, w, v, W, NULL) == opt;
        emit("knapsack", "dp-table", cases[c].name, n, ms, pk, 1.0, ok, ok && n <= 20 ? "verified by brute force" : "reference optimum");
        for (int md = 0; md < 3; md++) {
            ev_bnb_stats st;
            long lim = g_quick ? 500000 : 4000000;
            MEASURE(ms, pk, val = ev_knapsack_bnb(n, w, v, W, (ev_bnb_mode)md, lim, take, &st));
            char alg[32], note[96];
            snprintf(alg, sizeof alg, "bnb-%s", ev_bnb_mode_name((ev_bnb_mode)md));
            snprintf(note, sizeof note, "generated=%ld frontier=%ld%s", st.generated, st.max_frontier, st.complete ? "" : " NODE-LIMIT");
            emit("knapsack", alg, cases[c].name, n, ms, pk, (double)val / opt, st.complete ? val == opt : val <= opt, note);
        }
        /* greedy by ratio */
        int *ord = (int *)ev_malloc(sizeof(int) * (size_t)n);
        long gv = 0, gw = 0;
        MEASURE(ms, pk, {
            for (int i = 0; i < n; i++) ord[i] = i;
            for (int i = 1; i < n; i++) { /* insertion sort by ratio desc (n small) */
                int x = ord[i], j = i - 1;
                while (j >= 0 && (double)v[ord[j]] * w[x] < (double)v[x] * w[ord[j]]) { ord[j + 1] = ord[j]; j--; }
                ord[j + 1] = x;
            }
            for (int k = 0; k < n; k++) if (gw + w[ord[k]] <= W) { gw += w[ord[k]]; gv += v[ord[k]]; }
        });
        emit("knapsack", "greedy-ratio", cases[c].name, n, ms, pk, (double)gv / opt, gv <= opt, "quality=value/optimum");
        ev_free(w); ev_free(v); ev_free(take); ev_free(ord);
    }
}

static void bench_dp_misc(void) {
    if (!want("dp")) return;
    printf("[matrix chain, LCS, optimal BST, TSP]\n");
    ev_rng r;
    int mc[3] = { 10, 100, 300 };
    const char *dn[3] = { "small", "medium", "large" };
    for (int d = 0; d < 3; d++) {
        int n = mc[d];
        long *dims = (long *)ev_malloc(sizeof(long) * (size_t)(n + 1));
        ev_rng_seed(&r, 101 + d);
        for (int i = 0; i <= n; i++) dims[i] = ev_rng_range(&r, 5, 100);
        int *split = (int *)ev_malloc(sizeof(int) * (size_t)n * n);
        long long best;
        double ms; size_t pk;
        MEASURE(ms, pk, best = ev_matrix_chain(n, dims, split));
        long long left = ev_matrix_chain_left(n, dims);
        emit("matrixchain", "dp-n3", dn[d], n, ms, pk, (double)left / (double)best, best <= left, "quality=left-to-right/optimal");
        ev_free(dims); ev_free(split);
    }
    int ls[4] = { 1000, 3000, 8000, 3000 };
    const char *ldn[4] = { "small", "medium", "large", "adversarial" };
    if (g_quick) ls[2] = 4000;
    for (int d = 0; d < 4; d++) {
        int n = ls[d];
        int *a = (int *)ev_malloc(sizeof(int) * (size_t)n), *b = (int *)ev_malloc(sizeof(int) * (size_t)n), *out = (int *)ev_malloc(sizeof(int) * (size_t)n);
        ev_rng_seed(&r, 111 + d);
        for (int i = 0; i < n; i++) {
            a[i] = d == 3 ? 1 : (int)ev_rng_range(&r, 0, 50);
            b[i] = d == 3 ? 2 : (int)ev_rng_range(&r, 0, 50);   /* adversarial: disjoint alphabets */
        }
        int len;
        double ms; size_t pk;
        MEASURE(ms, pk, len = ev_lcs(a, n, b, n, out));
        int ok = d != 3 || len == 0;
        for (int i = 0, ia = 0, ib = 0; i < len && ok; i++) { /* verify subsequence of both */
            while (ia < n && a[ia] != out[i]) ia++;
            while (ib < n && b[ib] != out[i]) ib++;
            if (ia >= n || ib >= n) ok = 0;
            ia++; ib++;
        }
        emit("lcs", "dp-table", ldn[d], n, ms, pk, len, ok, "quality=LCS length");
        ev_free(a); ev_free(b); ev_free(out);
    }
    int ob[4] = { 50, 200, 400, 400 };
    const char *odn[4] = { "small", "medium", "large", "adversarial" };
    if (g_quick) ob[2] = ob[3] = 200;
    for (int d = 0; d < 4; d++) {
        int n = ob[d];
        double *p = (double *)ev_calloc((size_t)n + 1, sizeof(double)), *q = (double *)ev_calloc((size_t)n + 1, sizeof(double));
        ev_rng_seed(&r, 121 + d);
        double s = 0;
        for (int i = 1; i <= n; i++) { p[i] = d == 3 ? pow(0.7, i) : ev_rng_unit(&r); s += p[i]; } /* adversarial: geometric skew */
        for (int i = 1; i <= n; i++) p[i] /= s;
        double e, b;
        double ms; size_t pk;
        MEASURE(ms, pk, e = ev_optimal_bst(n, p, q, NULL));
        b = ev_balanced_bst_cost(n, p, q);
        emit("obst", "dp-n3", odn[d], n, ms, pk, b / e, e <= b + 1e-9, "quality=balanced/optimal probes");
        ev_free(p); ev_free(q);
    }
    int tn[6] = { 6, 8, 10, 13, 15, 16 };
    for (int d = 0; d < 6; d++) {
        int n = tn[d];
        if (g_quick && n > 13) continue;
        double *X = (double *)ev_malloc(sizeof(double) * (size_t)n), *Y = (double *)ev_malloc(sizeof(double) * (size_t)n);
        double *D = (double *)ev_malloc(sizeof(double) * (size_t)n * n);
        ev_rng_seed(&r, 131 + d);
        for (int i = 0; i < n; i++) { X[i] = ev_rng_unit(&r) * 50; Y[i] = ev_rng_unit(&r) * 50; }
        for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) D[(size_t)a * n + b] = hypot(X[a] - X[b], Y[a] - Y[b]);
        int tour[32];
        double hk, bf = -1;
        double ms; size_t pk;
        char ds[16];
        snprintf(ds, sizeof ds, "n%d", n);
        MEASURE(ms, pk, hk = ev_tsp_held_karp(n, D, tour));
        int ok = 1;
        if (n <= 10) { bf = ev_tsp_brute(n, D, tour); ok = fabs(hk - bf) < 1e-9; }
        emit("tsp", "held-karp", ds, n, ms, pk, hk, ok, n <= 10 ? "verified by brute force" : "");
        if (n <= 10) {
            MEASURE(ms, pk, bf = ev_tsp_brute(n, D, tour));
            emit("tsp", "brute-force", ds, n, ms, pk, bf, ok, "");
        }
        ev_free(X); ev_free(Y); ev_free(D);
    }
}

static void bench_backtracking(void) {
    if (!want("backtrack")) return;
    printf("[backtracking]\n");
    static const long known[13] = { 1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200 };
    int qn[4] = { 6, 8, 10, 12 };
    for (int d = 0; d < 4; d++) {
        int n = qn[d];
        long nodes, cnt;
        double ms; size_t pk;
        char ds[16];
        snprintf(ds, sizeof ds, "n%d", n);
        MEASURE(ms, pk, cnt = ev_nqueens_count(n, NULL, &nodes));
        char note[64];
        snprintf(note, sizeof note, "nodes=%ld", nodes);
        emit("nqueens", "backtracking", ds, n, ms, pk, cnt, cnt == known[n], note);
    }
    ev_rng r;
    struct { const char *name; int n; int adv; } cs[] = { { "small", 20, 0 }, { "medium", 30, 0 }, { "large", 60, 0 }, { "adversarial", 26, 1 } };
    for (int c = 0; c < 4; c++) {
        int n = cs[c].n;
        int *a = (int *)ev_malloc(sizeof(int) * (size_t)n), *take = (int *)ev_malloc(sizeof(int) * (size_t)n);
        ev_rng_seed(&r, 141 + c);
        long target = 0;
        for (int i = 0; i < n; i++) {
            a[i] = cs[c].adv ? 2 * (int)ev_rng_range(&r, 50, 500) : (int)ev_rng_range(&r, 20, 200);
            if (!cs[c].adv && ev_rng_unit(&r) < 0.5) target += a[i];
        }
        if (cs[c].adv) { long s = 0; for (int i = 0; i < n; i++) s += a[i]; target = s / 2 + 1; if (target % 2 == 0) target++; } /* odd: impossible */
        long nodes;
        int res, dp;
        double ms; size_t pk;
        MEASURE(ms, pk, res = ev_subset_sum(a, n, target, g_quick ? 2000000 : 50000000, take, &nodes));
        dp = ev_subset_sum_dp(a, n, target);
        long s = 0;
        for (int i = 0; i < n; i++) if (take[i]) s += a[i];
        int ok = res == -1 ? 1 : (res == dp) && (res == 0 || s == target);
        char note[80];
        snprintf(note, sizeof note, "nodes=%ld result=%s", nodes, res == 1 ? "found" : res == 0 ? "none" : "aborted");
        emit("subsetsum", "backtracking", cs[c].name, n, ms, pk, res, ok, note);
        MEASURE(ms, pk, dp = ev_subset_sum_dp(a, n, target));
        emit("subsetsum", "dp-oracle", cs[c].name, n, ms, pk, dp, 1, "");
        ev_free(a); ev_free(take);
    }
}

/* ======================================================================== flow */
static void bench_flow(void) {
    if (!want("flow")) return;
    printf("[max-flow and matching]\n");
    int sizes[3] = { 100, 500, 2000 };
    const char *dn[3] = { "small", "medium", "large" };
    ev_rng r;
    for (int d = 0; d < 3; d++) {
        int L = sizes[d], R = sizes[d], deg = 5;
        int *head = (int *)ev_calloc((size_t)L + 1, sizeof(int)), *adj = (int *)ev_malloc(sizeof(int) * (size_t)L * deg);
        ev_rng_seed(&r, 151 + d);
        int m = 0;
        for (int i = 0; i < L; i++) {
            for (int k = 0; k < deg; k++) adj[m++] = (int)ev_rng_range(&r, 0, R - 1);
            head[i + 1] = m;
        }
        int *ml = (int *)ev_malloc(sizeof(int) * (size_t)L), *mr = (int *)ev_malloc(sizeof(int) * (size_t)R);
        int match;
        double ms; size_t pk;
        MEASURE(ms, pk, match = ev_bipartite_match(L, R, head, adj, ml, mr));
        long fl[2], aug[2];
        double fms[2]; size_t fpk[2];
        for (int mode = 0; mode < 2; mode++) {
            ev_flow *f = ev_flow_create(L + R + 2);
            for (int i = 0; i < L; i++) ev_flow_add_edge(f, 0, 1 + i, 1);
            for (int i = 0; i < L; i++) for (int k = head[i]; k < head[i + 1]; k++) ev_flow_add_edge(f, 1 + i, 1 + L + adj[k], 1);
            for (int j = 0; j < R; j++) ev_flow_add_edge(f, 1 + L + j, L + R + 1, 1);
            MEASURE(fms[mode], fpk[mode], fl[mode] = ev_flow_maxflow(f, 0, L + R + 1, (ev_flow_mode)mode, &aug[mode]));
            ev_flow_free(f);
        }
        int ok = fl[0] == fl[1] && fl[1] == match;
        emit("flow", "bipartite-kuhn", dn[d], L, ms, pk, match, ok, "unit-capacity assignment");
        emit("flow", "ford-fulkerson-dfs", dn[d], L, fms[0], fpk[0], (double)fl[0], ok, "same network as matching");
        emit("flow", "edmonds-karp-bfs", dn[d], L, fms[1], fpk[1], (double)fl[1], ok, "");
        ev_free(head); ev_free(adj); ev_free(ml); ev_free(mr);
    }
    /* high-capacity layered network: augmentation counts now depend on capacities (FF) vs |V||E| (EK) */
    {
        int L = g_quick ? 200 : 600, deg = 6;
        long fl[2], aug[2];
        double ms[2]; size_t pk[2];
        for (int mode = 0; mode < 2; mode++) {
            ev_rng_seed(&r, 999);
            ev_flow *f = ev_flow_create(2 * L + 2);
            for (int i = 0; i < L; i++) ev_flow_add_edge(f, 0, 1 + i, ev_rng_range(&r, 1, 1000));
            for (int i = 0; i < L; i++)
                for (int k = 0; k < deg; k++) ev_flow_add_edge(f, 1 + i, 1 + L + (int)ev_rng_range(&r, 0, L - 1), ev_rng_range(&r, 1, 400));
            for (int j = 0; j < L; j++) ev_flow_add_edge(f, 1 + L + j, 2 * L + 1, ev_rng_range(&r, 1, 1000));
            MEASURE(ms[mode], pk[mode], fl[mode] = ev_flow_maxflow(f, 0, 2 * L + 1, (ev_flow_mode)mode, &aug[mode]));
            ev_flow_free(f);
        }
        char note[80];
        snprintf(note, sizeof note, "capacities 1..1000; augmenting paths=%ld", aug[0]);
        emit("flow", "ford-fulkerson-dfs", "adversarial", L, ms[0], pk[0], (double)fl[0], fl[0] == fl[1], note);
        snprintf(note, sizeof note, "capacities 1..1000; augmenting paths=%ld", aug[1]);
        emit("flow", "edmonds-karp-bfs", "adversarial", L, ms[1], pk[1], (double)fl[1], fl[0] == fl[1], note);
    }
}

/* ======================================================================== strings + CRT */
static void bench_strings(void) {
    if (!want("strings")) return;
    printf("[string matching]\n");
    ev_rng r;
    struct { const char *name; long n; int m; int adv; } cs[] = {
        { "small", 10000, 6, 0 }, { "medium", 1000000, 8, 0 }, { "large", 10000000, 12, 0 }, { "adversarial", 1000000, 200, 1 } };
    for (int c = 0; c < 4; c++) {
        long n = cs[c].n;
        if (g_quick && n > 1000000) n = 1000000;
        int m = cs[c].m;
        char *t = (char *)ev_malloc((size_t)n + 1), *p = (char *)ev_malloc((size_t)m + 1);
        ev_rng_seed(&r, 161 + c);
        for (long i = 0; i < n; i++) t[i] = cs[c].adv ? 'F' : "CEIFOR"[ev_rng_range(&r, 0, 5)];
        for (int i = 0; i < m; i++) p[i] = cs[c].adv ? 'F' : t[n / 2 + i];
        if (cs[c].adv) p[m - 1] = 'R';                      /* "FFF...FR": naive does ~n*m comparisons */
        t[n] = p[m] = '\0';
        ev_match_stats st[4];
        double ms[4]; size_t pk[4];
        MEASURE(ms[0], pk[0], ev_match_naive(t, n, p, m, NULL, 0, &st[0]));
        MEASURE(ms[1], pk[1], ev_match_rabin_karp(t, n, p, m, NULL, 0, &st[1]));
        MEASURE(ms[2], pk[2], ev_match_fa(t, n, p, m, NULL, 0, &st[2]));
        MEASURE(ms[3], pk[3], ev_match_kmp(t, n, p, m, NULL, 0, &st[3]));
        const char *names[4] = { "naive", "rabin-karp", "finite-automaton", "kmp" };
        int ok = st[0].matches == st[1].matches && st[1].matches == st[2].matches && st[2].matches == st[3].matches;
        for (int k = 0; k < 4; k++) {
            char note[96];
            snprintf(note, sizeof note, "matches=%ld preprocess=%lld m=%d", st[k].matches, st[k].preprocess, m);
            emit("strings", names[k], cs[c].name, n, ms[k], pk[k], (double)st[k].comparisons, ok, note);
        }
        ev_free(t); ev_free(p);
    }
    printf("[chinese remainder theorem]\n");
    struct { const char *name; int k; long long a[5], m[5]; int expect; } cr[] = {
        { "coprime", 4, { 2, 3, 2, 6 }, { 3, 5, 7, 11 }, 1 },
        { "non-coprime", 3, { 10, 22, 34 }, { 24, 36, 48 }, 1 },
        { "inconsistent", 2, { 1, 2 }, { 4, 6 }, 0 },
        { "large", 4, { 123456, 7890, 1000, 42 }, { 1000003, 999983, 1009, 97 }, 1 },
        { "overflow", 3, { 1, 2, 3 }, { 1000000007, 998244353, 1000003 }, -1 } };
    for (int c = 0; c < 5; c++) {
        long long x = 0, M = 0;
        int res = 0;
        double ms; size_t pk;
        MEASURE(ms, pk, for (int rep = 0; rep < 10000; rep++) res = ev_crt(cr[c].k, cr[c].a, cr[c].m, &x, &M));
        int ok = res == cr[c].expect;
        for (int i = 0; ok && res == 1 && i < cr[c].k; i++) ok = ((x % cr[c].m[i]) == (cr[c].a[i] % cr[c].m[i]));
        char note[96];
        snprintf(note, sizeof note, "x=%lld M=%lld (x10000 calls)", res == 1 ? x : -1, res == 1 ? M : -1);
        emit("crt", "generalized-crt", cr[c].name, cr[c].k, ms, pk, res, ok, note);
    }
}

/* ======================================================================== end-to-end pipeline */
static void bench_pipeline(const char *root) {
    if (!want("pipeline")) return;
    printf("[end-to-end EV pipeline]\n");
    const char *sets[5] = { "small", "medium", "shock", "large", "adversarial" };
    for (int s = 0; s < 5; s++) {
        if (g_quick && !strcmp(sets[s], "large")) continue;
        char dir[512], err[512];
        snprintf(dir, sizeof dir, "%s/%s", root, sets[s]);
        ev_scenario sc;
        if (ev_scenario_load(dir, &sc, err, sizeof err) != 0) { printf("  (skip %s: %s)\n", sets[s], err); continue; }
        ev_selection opt;
        ev_select(&sc, EV_SEL_DP, &opt);
        for (int sm = 0; sm < EV_SEL_COUNT; sm++) {
            ev_selection sel;
            ev_select(&sc, (ev_sel_mode)sm, &sel);
            char note[96];
            snprintf(note, sizeof note, "stations=%d cost=%ld nodes=%ld", sel.n, sel.cost, sel.search_nodes);
            emit("select", ev_sel_name((ev_sel_mode)sm), sets[s], sc.ns, sel.ms, sel.peak_bytes,
                 opt.value ? (double)sel.value / opt.value : 1.0, sel.value <= opt.value && (!sel.optimal || sel.value == opt.value), note);
            ev_selection_free(&sel);
        }
        double base_kwh = 0;
        int base_feas = 0;
        for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) {
            if (rm == EV_ROUTE_FLOYD && g_quick && sc.nn > 500) continue;
            ev_run run;
            ev_run_pipeline(&sc, EV_SEL_DP, (ev_route_mode)rm, &run);
            if (rm == 0) { base_kwh = run.kpi.avg_kwh; base_feas = run.kpi.feasible; }
            char note[128];
            snprintf(note, sizeof note, "feasible=%d/%d avg_km=%.2f avg_kwh=%.3f sssp=%ld", run.kpi.feasible, run.kpi.trips,
                     run.kpi.avg_km, run.kpi.avg_kwh, ev_router_sssp_runs(run.router));
            int ok = rm != EV_ROUTE_FLOYD || (run.kpi.feasible == base_feas && fabs(run.kpi.avg_kwh - base_kwh) < 1e-3);
            emit("route", ev_route_name((ev_route_mode)rm), sets[s], sc.nn, run.ms_router + run.ms_plan, run.peak_route,
                 run.kpi.feasible_pct, ok, note);
            ev_run_free(&run);
        }
        ev_selection_free(&opt);
        ev_scenario_free(&sc);
    }
}

int main(int argc, char **argv) {
    const char *out = "results/bench.csv", *root = "data";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) g_quick = 1;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--data-root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) g_only = argv[++i];
        else {
            fprintf(stderr, "usage: ev33_bench [--quick] [--out FILE] [--data-root DIR] [--only FAMILY]\n"
                            "families: sort search dnc greedy graph knapsack dp backtrack flow strings pipeline\n");
            return 1;
        }
    }
    char dir[512];
    snprintf(dir, sizeof dir, "%s", out);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; MKDIR(dir); }
    g_csv = fopen(out, "w");
    if (!g_csv) { perror(out); return 1; }
    fprintf(g_csv, "family,algorithm,dataset,n,ms,peak_bytes,quality,check,note\n");
    double t0 = ev_now_ms();
    bench_sort();
    bench_search();
    bench_dnc();
    bench_greedy();
    bench_graphs();
    bench_knapsack();
    bench_dp_misc();
    bench_backtracking();
    bench_flow();
    bench_strings();
    bench_pipeline(root);
    fclose(g_csv);
    printf("\n%ld rows, %ld failed checks, %.1f s -> %s\n", g_rows, g_fail, (ev_now_ms() - t0) / 1000, out);
    return g_fail ? 2 : 0;
}
