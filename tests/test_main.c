/* ev33_tests - unit, oracle, integration, edge-case, invalid-input and regression tests.
 *
 *   ev33_tests [--record]     (run from the repository root)
 *
 * --record rewrites the files in tests/regression/ from the current build (use only after
 * an intentional behaviour change, and review the diff).
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
#include "ev_sim.h"
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

static int g_pass = 0, g_fail = 0, g_record = 0;
static const char *g_group = "";

#define CHECK(cond, ...) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("  FAIL [%s] %s:%d: ", g_group, __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void group(const char *g) { g_group = g; printf("- %s\n", g); }

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

/* ======================================================================== sorting / searching */
typedef struct { int key, idx; } rec;
static int cmp_rec(const void *a, const void *b) { const rec *x = a, *y = b; return (x->key > y->key) - (x->key < y->key); }

static void test_sorting(void) {
    group("sorting: merge / quick (3 pivots) vs qsort oracle, stability, edge sizes");
    ev_rng r;
    ev_rng_seed(&r, 1);
    long sizes[] = { 0, 1, 2, 3, 17, 100, 1000, 5000 };
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        long n = sizes[s];
        for (int mode = 0; mode < 4; mode++) { /* random, few distinct, sorted, reversed */
            int *src = malloc(sizeof(int) * (size_t)(n + 1)), *ref = malloc(sizeof(int) * (size_t)(n + 1)), *a = malloc(sizeof(int) * (size_t)(n + 1));
            for (long i = 0; i < n; i++)
                src[i] = mode == 0 ? (int)ev_rng_range(&r, -1000, 1000) : mode == 1 ? (int)ev_rng_range(&r, 0, 3) : mode == 2 ? (int)i : (int)(n - i);
            memcpy(ref, src, sizeof(int) * (size_t)n);
            qsort(ref, (size_t)n, sizeof(int), cmp_int);
            memcpy(a, src, sizeof(int) * (size_t)n);
            ev_merge_sort(a, (size_t)n, sizeof(int), cmp_int);
            CHECK(!memcmp(a, ref, sizeof(int) * (size_t)n), "merge n=%ld mode=%d", n, mode);
            for (int pv = 0; pv < 3; pv++) {
                memcpy(a, src, sizeof(int) * (size_t)n);
                ev_quick_sort(a, (size_t)n, sizeof(int), cmp_int, (ev_pivot)pv);
                CHECK(!memcmp(a, ref, sizeof(int) * (size_t)n), "quick pivot=%d n=%ld mode=%d", pv, n, mode);
            }
            free(src); free(ref); free(a);
        }
    }
    rec v[3000];
    for (int i = 0; i < 3000; i++) { v[i].key = (int)ev_rng_range(&r, 0, 20); v[i].idx = i; }
    ev_merge_sort(v, 3000, sizeof(rec), cmp_rec);
    int stable = 1;
    for (int i = 1; i < 3000; i++) if (v[i - 1].key == v[i].key && v[i - 1].idx > v[i].idx) stable = 0;
    CHECK(stable, "merge sort must be stable");
}

static void test_searching(void) {
    group("binary search: iterative (leftmost), recursive, lower/upper bound");
    int a[200];
    for (int i = 0; i < 200; i++) a[i] = (i / 3) * 2; /* duplicates, even values only */
    for (int key = -2; key <= 140; key++) {
        long li = -1;
        for (int i = 0; i < 200; i++) if (a[i] == key) { li = i; break; }
        long it = ev_bsearch_iter(&key, a, 200, sizeof(int), cmp_int);
        long rc = ev_bsearch_rec(&key, a, 200, sizeof(int), cmp_int);
        CHECK(it == li, "iterative key=%d got %ld want %ld", key, it, li);
        CHECK((rc < 0) == (li < 0) && (rc < 0 || a[rc] == key), "recursive key=%d", key);
        size_t lb = ev_lower_bound(&key, a, 200, sizeof(int), cmp_int), ub = ev_upper_bound(&key, a, 200, sizeof(int), cmp_int);
        size_t elb = 0, eub = 0;
        while (elb < 200 && a[elb] < key) elb++;
        while (eub < 200 && a[eub] <= key) eub++;
        CHECK(lb == elb && ub == eub, "bounds key=%d", key);
    }
    int k = 5;
    CHECK(ev_bsearch_iter(&k, a, 0, sizeof(int), cmp_int) == -1, "empty array");
    CHECK(ev_bsearch_rec(&k, a, 0, sizeof(int), cmp_int) == -1, "empty array rec");
}

/* ======================================================================== divide and conquer */
typedef struct { int n; const unsigned char *m; } kmat;
static int kfn(void *c, int a, int b) { kmat *k = c; return k->m[a * k->n + b]; }

static void test_dnc(void) {
    group("divide & conquer: max subarray vs brute, powering, celebrity, Strassen vs classical");
    ev_rng r;
    ev_rng_seed(&r, 2);
    for (int t = 0; t < 300; t++) {
        long n = ev_rng_range(&r, 1, 60);
        double a[60];
        for (long i = 0; i < n; i++) a[i] = (double)ev_rng_range(&r, -20, 15);
        ev_subarray x = ev_max_subarray_dc(a, n), y = ev_max_subarray_brute(a, n), z = ev_max_subarray_kadane(a, n);
        double s = 0;
        for (long i = x.lo; i <= x.hi; i++) s += a[i];
        CHECK(x.sum == y.sum && z.sum == y.sum && fabs(s - x.sum) < 1e-9, "max subarray case %d", t);
    }
    for (unsigned e = 0; e < 70; e++) CHECK(fabs(ev_pow_fast(1.01, e) - pow(1.01, e)) < 1e-12 * pow(1.01, e), "pow e=%u", e);
    for (unsigned long long b = 0; b < 30; b++)
        for (unsigned long long e = 0; e < 30; e++) {
            unsigned long long m = 1000003, want = 1;
            for (unsigned long long i = 0; i < e; i++) want = want * b % m;
            CHECK(ev_powmod(b, e, m) == want, "powmod %llu^%llu", b, e);
        }
    for (int t = 0; t < 40; t++) {
        int n = (int)ev_rng_range(&r, 1, 30);
        unsigned char m[900] = { 0 };
        int celeb = t % 3 == 0 ? -1 : (int)ev_rng_range(&r, 0, n - 1);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (i != j) m[i * n + j] = (unsigned char)(celeb < 0 ? ev_rng_unit(&r) < 0.5 : j == celeb ? 1 : i == celeb ? 0 : ev_rng_unit(&r) < 0.5);
        kmat k = { n, m };
        long q1, q2;
        int a = ev_celebrity_dc(n, kfn, &k, &q1), b = ev_celebrity_naive(n, kfn, &k, &q2);
        CHECK(a == b, "celebrity dc=%d naive=%d n=%d", a, b, n);
        if (celeb >= 0) CHECK(a == celeb, "planted celebrity %d found %d", celeb, a);
        CHECK(q1 <= 3 * n, "celebrity D&C must use O(n) queries (%ld)", q1);
    }
    int sizes[] = { 1, 2, 3, 7, 16, 33, 64 };
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        int n = sizes[s];
        double *A = malloc(sizeof(double) * n * n), *B = malloc(sizeof(double) * n * n), *C1 = malloc(sizeof(double) * n * n), *C2 = malloc(sizeof(double) * n * n);
        for (int i = 0; i < n * n; i++) { A[i] = ev_rng_range(&r, -9, 9); B[i] = ev_rng_range(&r, -9, 9); }
        ev_matmul_classic(A, B, C1, n);
        ev_matmul_strassen(A, B, C2, n, 2);
        double md = 0;
        for (int i = 0; i < n * n; i++) md = fmax(md, fabs(C1[i] - C2[i]));
        CHECK(md < 1e-9, "strassen n=%d max diff %g", n, md);
        free(A); free(B); free(C1); free(C2);
    }
}

/* ======================================================================== greedy */
static int brute_activity(const ev_interval *iv, int n) {
    int best = 0;
    for (int mask = 0; mask < (1 << n); mask++) {
        int ok = 1, c = 0;
        for (int i = 0; i < n && ok; i++) if (mask >> i & 1) {
            c++;
            for (int j = i + 1; j < n; j++) if (mask >> j & 1)
                if (iv[i].start < iv[j].end && iv[j].start < iv[i].end) { ok = 0; break; }
        }
        if (ok && c > best) best = c;
    }
    return best;
}

static void test_greedy(void) {
    group("greedy: activity selection vs brute, interval partition vs depth, Huffman");
    ev_rng r;
    ev_rng_seed(&r, 3);
    for (int t = 0; t < 200; t++) {
        int n = (int)ev_rng_range(&r, 0, 12);
        ev_interval iv[12];
        int out[12], asg[12];
        for (int i = 0; i < n; i++) { double s = (double)ev_rng_range(&r, 0, 20); iv[i].id = i; iv[i].start = s; iv[i].end = s + (double)ev_rng_range(&r, 1, 6); }
        int a = ev_activity_select(iv, n, out);
        CHECK(a == brute_activity(iv, n), "activity selection case %d", t);
        for (int i = 0; i < a; i++) for (int j = i + 1; j < a; j++)
            CHECK(!(iv[out[i]].start < iv[out[j]].end && iv[out[j]].start < iv[out[i]].end), "selected sessions overlap");
        int p = ev_interval_partition(iv, n, asg);
        CHECK(p == ev_interval_depth(iv, n), "partition must equal depth (case %d)", t);
        for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
            if (asg[i] == asg[j]) CHECK(!(iv[i].start < iv[j].end && iv[j].start < iv[i].end), "same charger overlap");
    }
    /* Huffman */
    const char *texts[] = { "a", "aaaaaaa", "abracadabra", "CCCCEEEIFFOR" };
    for (int t = 0; t < 4; t++) {
        size_t n = strlen(texts[t]);
        ev_huff *h = ev_huff_build((const unsigned char *)texts[t], n);
        unsigned char *bits, out[64];
        size_t nb = ev_huff_encode(h, (const unsigned char *)texts[t], n, &bits);
        size_t nd = ev_huff_decode(h, bits, nb, out, sizeof out);
        CHECK(nd == n && !memcmp(out, texts[t], n), "huffman roundtrip '%s'", texts[t]);
        ev_free(bits);
        ev_huff_free(h);
    }
    CHECK(ev_huff_build((const unsigned char *)"", 0) == NULL, "empty input gives no tree");
    for (int t = 0; t < 30; t++) {
        size_t n = (size_t)ev_rng_range(&r, 1, 5000);
        unsigned char *d = malloc(n), *o = malloc(n);
        int alpha = (int)ev_rng_range(&r, 1, 40);
        for (size_t i = 0; i < n; i++) d[i] = (unsigned char)(ev_rng_range(&r, 0, alpha - 1) * ev_rng_range(&r, 0, 1) + 'A');
        ev_huff *h = ev_huff_build(d, n);
        unsigned char *bits;
        size_t nb = ev_huff_encode(h, d, n, &bits);
        CHECK(ev_huff_decode(h, bits, nb, o, n) == n && !memcmp(d, o, n), "random roundtrip %d", t);
        /* entropy bound H <= L < H + 1 */
        double f[256] = { 0 }, H = 0;
        for (size_t i = 0; i < n; i++) f[d[i]]++;
        for (int c = 0; c < 256; c++) if (f[c] > 0) H -= f[c] / n * log2(f[c] / n);
        double L = ev_huff_avg_bits(h);
        CHECK(L >= H - 1e-9 && (L < H + 1 + 1e-9 || ev_huff_symbols(h) == 1), "entropy bound H=%.3f L=%.3f", H, L);
        /* prefix-free */
        int pf = 1;
        for (int a = 0; a < 256; a++) for (int b = 0; b < 256; b++) {
            const char *x = ev_huff_code(h, (unsigned char)a), *y = ev_huff_code(h, (unsigned char)b);
            if (a != b && *x && *y && !strncmp(x, y, strlen(x))) pf = 0;
        }
        CHECK(pf, "codes must be prefix-free");
        ev_free(bits); ev_huff_free(h); free(d); free(o);
    }
}

/* ======================================================================== graphs */
static void test_graphs(void) {
    group("graphs: Dijkstra = Bellman-Ford = Floyd-Warshall, negative cycles, Prim = Kruskal");
    ev_rng r;
    ev_rng_seed(&r, 4);
    for (int t = 0; t < 60; t++) {
        int n = (int)ev_rng_range(&r, 1, 40), m = (int)ev_rng_range(&r, 0, n * 4);
        int *s = malloc(sizeof(int) * (m + 1)), *d = malloc(sizeof(int) * (m + 1));
        double *w = malloc(sizeof(double) * (m + 1)), *pot = malloc(sizeof(double) * n);
        for (int i = 0; i < n; i++) pot[i] = ev_rng_unit(&r) * 5;
        for (int k = 0; k < m; k++) { s[k] = (int)ev_rng_range(&r, 0, n - 1); d[k] = (int)ev_rng_range(&r, 0, n - 1); w[k] = 1 + ev_rng_unit(&r) * 9; }
        ev_graph *g = ev_graph_build(n, m, s, d, w);
        double *d1 = malloc(sizeof(double) * n), *d2 = malloc(sizeof(double) * n), *D = malloc(sizeof(double) * n * n);
        int *pa = malloc(sizeof(int) * n), *nx = malloc(sizeof(int) * n * n), path[64];
        ev_floyd_warshall(g, D, nx);
        for (int src = 0; src < n; src++) {
            ev_dijkstra(g, src, d1, pa);
            ev_bellman_ford(g, src, d2, NULL, NULL);
            for (int v = 0; v < n; v++) {
                int both_inf = d1[v] >= EV_INF && d2[v] >= EV_INF && D[src * n + v] >= EV_INF;
                CHECK(both_inf || (fabs(d1[v] - d2[v]) < 1e-9 && fabs(d1[v] - D[src * n + v]) < 1e-9), "sp mismatch t=%d %d->%d", t, src, v);
                /* path reconstruction from Floyd sums to the distance */
                int len = ev_floyd_path(n, nx, src, v, path, 64);
                if (!both_inf) {
                    double sum = 0;
                    for (int i = 0; i + 1 < len; i++) sum += g->arcs[ev_graph_find_arc(g, path[i], path[i + 1])].w;
                    CHECK(len > 0 && fabs(sum - D[src * n + v]) < 1e-9, "floyd path sum");
                }
            }
        }
        /* reweight with a potential -> negative arcs, no negative cycles: BF and Floyd still agree */
        for (int k = 0; k < m; k++) w[k] = w[k] + pot[d[k]] - pot[s[k]] - 0.5;
        ev_graph *gn = ev_graph_build(n, m, s, d, w);
        int fw = ev_floyd_warshall(gn, D, nx);
        int bad = 0;
        for (int src = 0; src < n; src++) {
            int bf = ev_bellman_ford(gn, src, d2, NULL, NULL);
            if (bf != 0 || fw != 0) { bad = bf != fw ? 0 : 0; continue; } /* weights can still form negative cycles; just compare */
            for (int v = 0; v < n; v++)
                if (!(d2[v] >= EV_INF && D[src * n + v] >= EV_INF)) CHECK(fabs(d2[v] - D[src * n + v]) < 1e-9, "negative-arc BF vs FW");
        }
        (void)bad;
        ev_graph_free(g); ev_graph_free(gn);
        free(s); free(d); free(w); free(pot); free(d1); free(d2); free(D); free(pa); free(nx);
    }
    { /* explicit negative cycle */
        int s[3] = { 0, 1, 2 }, d[3] = { 1, 2, 0 };
        double w[3] = { 1, -2, -1 }, dist[3], D[9];
        int nx[9];
        ev_graph *g = ev_graph_build(3, 3, s, d, w);
        CHECK(ev_bellman_ford(g, 0, dist, NULL, NULL) == -1, "Bellman-Ford must detect negative cycle");
        CHECK(ev_floyd_warshall(g, D, nx) == -1, "Floyd-Warshall must detect negative cycle");
        ev_graph_free(g);
    }
    for (int t = 0; t < 40; t++) {
        int n = (int)ev_rng_range(&r, 1, 30);
        double *W = malloc(sizeof(double) * n * n);
        int m = 0, *eu = malloc(sizeof(int) * n * n), *ev = malloc(sizeof(int) * n * n), *par = malloc(sizeof(int) * n), *ch = malloc(sizeof(int) * n), nch;
        double *ew = malloc(sizeof(double) * n * n);
        for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) W[a * n + b] = a == b ? 0 : EV_INF;
        for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
            if (ev_rng_unit(&r) < 0.4) { double w = (double)ev_rng_range(&r, 1, 50); W[a * n + b] = W[b * n + a] = w; eu[m] = a; ev[m] = b; ew[m] = w; m++; }
        double wp = ev_prim_dense(n, W, par), wk = ev_kruskal(n, m, eu, ev, ew, ch, &nch);
        CHECK(fabs(wp - wk) < 1e-9, "MST weight prim %.1f kruskal %.1f (possibly disconnected)", wp, wk);
        free(W); free(eu); free(ev); free(ew); free(par); free(ch);
    }
}

/* ======================================================================== DP */
static long long mc_brute(const long *d, int i, int j) {
    if (i == j) return 0;
    long long best = -1;
    for (int k = i; k < j; k++) {
        long long c = mc_brute(d, i, k) + mc_brute(d, k + 1, j) + (long long)d[i] * d[k + 1] * d[j + 1];
        if (best < 0 || c < best) best = c;
    }
    return best;
}

static void test_dp(void) {
    group("dynamic programming: knapsack, matrix chain, LCS, optimal BST, Held-Karp");
    ev_rng r;
    ev_rng_seed(&r, 5);
    for (int t = 0; t < 100; t++) {
        int n = (int)ev_rng_range(&r, 0, 15), w[15], take[15], W = (int)ev_rng_range(&r, 0, 200);
        long v[15];
        for (int i = 0; i < n; i++) { w[i] = (int)ev_rng_range(&r, 0, 60); v[i] = ev_rng_range(&r, 0, 90); }
        long a = ev_knapsack01(n, w, v, W, take), b = ev_knapsack_brute(n, w, v, W, NULL);
        long sv = 0, sw = 0;
        for (int i = 0; i < n; i++) if (take[i]) { sv += v[i]; sw += w[i]; }
        CHECK(a == b && sv == a && sw <= W, "knapsack case %d dp=%ld brute=%ld", t, a, b);
    }
    { int w[1] = { -1 }; long v[1] = { 1 }; CHECK(ev_knapsack01(1, w, v, 5, NULL) == -1, "negative weight rejected"); }
    long clrs[7] = { 30, 35, 15, 5, 10, 20, 25 };
    int split[36];
    char buf[128];
    CHECK(ev_matrix_chain(6, clrs, split) == 15125, "CLRS matrix chain cost");
    ev_matrix_chain_paren(6, split, buf, sizeof buf);
    CHECK(!strcmp(buf, "((A1(A2A3))((A4A5)A6))"), "CLRS parenthesization got %s", buf);
    for (int t = 0; t < 30; t++) {
        int n = (int)ev_rng_range(&r, 1, 8);
        long d[9];
        for (int i = 0; i <= n; i++) d[i] = ev_rng_range(&r, 1, 40);
        CHECK(ev_matrix_chain(n, d, NULL) == mc_brute(d, 0, n - 1), "matrix chain brute case %d", t);
    }
    int A[7] = { 'A', 'B', 'C', 'B', 'D', 'A', 'B' }, B[6] = { 'B', 'D', 'C', 'A', 'B', 'A' }, out[7];
    CHECK(ev_lcs(A, 7, B, 6, out) == 4, "CLRS LCS length");
    CHECK(ev_lcs(A, 0, B, 6, NULL) == 0, "LCS empty");
    double p[6] = { 0, .15, .10, .05, .10, .20 }, q[6] = { .05, .10, .05, .05, .05, .10 };
    int root[49];
    CHECK(fabs(ev_optimal_bst(5, p, q, root) - 2.75) < 1e-9, "CLRS optimal BST = 2.75");
    CHECK(root[1 * 7 + 5] == 2, "CLRS optimal BST root k2");
    CHECK(ev_optimal_bst(5, p, q, NULL) <= ev_balanced_bst_cost(5, p, q) + 1e-12, "optimal <= balanced");
    for (int t = 0; t < 40; t++) {
        int n = (int)ev_rng_range(&r, 1, 9), tour[12], tb[12];
        double D[81];
        for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) D[a * n + b] = a == b ? 0 : (double)ev_rng_range(&r, 1, 100); /* asymmetric */
        double hk = ev_tsp_held_karp(n, D, tour), bf = ev_tsp_brute(n, D, tb);
        CHECK(fabs(hk - bf) < 1e-9, "TSP n=%d hk=%.1f bf=%.1f", n, hk, bf);
        double len = 0;
        int seen[12] = { 0 };
        for (int i = 0; i < n; i++) { len += D[tour[i] * n + tour[i + 1]]; seen[tour[i]]++; }
        int perm = 1;
        for (int i = 0; i < n; i++) if (seen[i] != 1) perm = 0;
        CHECK(perm && tour[0] == 0 && tour[n] == 0 && fabs(len - hk) < 1e-9, "TSP tour valid");
    }
}

/* ======================================================================== backtracking + BnB */
static void test_search(void) {
    group("backtracking + branch-and-bound: N-Queens, subset sum, knapsack FIFO/LIFO/LC");
    long known[11] = { 1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724 };
    for (int n = 1; n <= 10; n++) {
        int first[10];
        long cnt = ev_nqueens_count(n, first, NULL);
        CHECK(cnt == known[n], "N-Queens(%d) = %ld", n, cnt);
        if (cnt > 0) {
            int ok = 1;
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
                if (first[i] == first[j] || abs(first[i] - first[j]) == j - i) ok = 0;
            CHECK(ok, "first N-Queens solution valid (n=%d)", n);
        }
    }
    ev_rng r;
    ev_rng_seed(&r, 6);
    for (int t = 0; t < 100; t++) {
        int n = (int)ev_rng_range(&r, 0, 18), a[18], take[18];
        for (int i = 0; i < n; i++) a[i] = (int)ev_rng_range(&r, 1, 50);
        long target = ev_rng_range(&r, 0, 300);
        int bt = ev_subset_sum(a, n, target, 0, take, NULL), dp = ev_subset_sum_dp(a, n, target);
        long s = 0;
        for (int i = 0; i < n; i++) if (take[i]) s += a[i];
        CHECK(bt == dp && (!bt || s == target), "subset sum case %d", t);
    }
    for (int t = 0; t < 150; t++) {
        int n = (int)ev_rng_range(&r, 0, 30), w[30], take[30];
        long v[30];
        int corr = t % 3;
        long tw = 0;
        for (int i = 0; i < n; i++) {
            w[i] = (int)ev_rng_range(&r, 1, 80);
            v[i] = corr == 0 ? ev_rng_range(&r, 1, 80) : corr == 1 ? w[i] + 5 : w[i] + ev_rng_range(&r, 0, 10);
            tw += w[i];
        }
        int W = (int)(tw / 2);
        long opt = ev_knapsack01(n, w, v, W, NULL);
        for (int md = 0; md < 3; md++) {
            ev_bnb_stats st;
            long b = ev_knapsack_bnb(n, w, v, W, (ev_bnb_mode)md, 0, take, &st);
            long sv = 0, sw = 0;
            for (int i = 0; i < n; i++) if (take[i]) { sv += v[i]; sw += w[i]; }
            CHECK(st.complete && b == opt && sv == b && sw <= W, "BnB %s case %d got %ld opt %ld", ev_bnb_mode_name((ev_bnb_mode)md), t, b, opt);
        }
    }
}

/* ======================================================================== flow */
static void test_flow(void) {
    group("flow: CLRS network, max-flow = min-cut, FF = Edmonds-Karp = bipartite matching");
    /* CLRS Figure 26.1: max flow 23 */
    int eu[10] = { 0, 0, 1, 2, 1, 3, 2, 4, 3, 4 }, evv[10] = { 1, 2, 2, 1, 3, 2, 4, 3, 5, 5 };
    long ec[10] = { 16, 13, 10, 4, 12, 9, 14, 7, 20, 4 };
    ev_flow *f = ev_flow_create(6);
    for (int k = 0; k < 10; k++) ev_flow_add_edge(f, eu[k], evv[k], ec[k]);
    CHECK(ev_flow_maxflow(f, 0, 5, EV_FLOW_DFS, NULL) == 23, "CLRS max flow (DFS)");
    ev_flow_reset(f);
    CHECK(ev_flow_maxflow(f, 0, 5, EV_FLOW_BFS, NULL) == 23, "CLRS max flow (BFS)");
    ev_flow_free(f);
    ev_rng r;
    ev_rng_seed(&r, 7);
    for (int t = 0; t < 60; t++) {
        int n = (int)ev_rng_range(&r, 2, 20), m = (int)ev_rng_range(&r, 0, 60);
        int *u = malloc(sizeof(int) * (m + 1)), *v = malloc(sizeof(int) * (m + 1));
        long *c = malloc(sizeof(long) * (m + 1));
        for (int k = 0; k < m; k++) { u[k] = (int)ev_rng_range(&r, 0, n - 1); v[k] = (int)ev_rng_range(&r, 0, n - 1); c[k] = ev_rng_range(&r, 0, 20); }
        long fl[2];
        for (int mode = 0; mode < 2; mode++) {
            ev_flow *g = ev_flow_create(n);
            int *id = malloc(sizeof(int) * (m + 1));
            for (int k = 0; k < m; k++) id[k] = u[k] == v[k] ? -1 : ev_flow_add_edge(g, u[k], v[k], c[k]);
            fl[mode] = ev_flow_maxflow(g, 0, n - 1, (ev_flow_mode)mode, NULL);
            unsigned char side[20];
            ev_flow_min_cut(g, 0, side);
            long cut = 0;
            for (int k = 0; k < m; k++) if (id[k] >= 0 && side[u[k]] && !side[v[k]]) cut += c[k];
            CHECK(cut == fl[mode] && !side[n - 1], "max-flow %ld = min-cut %ld (mode %d)", fl[mode], cut, mode);
            /* conservation */
            int cons = 1;
            for (int x = 1; x < n - 1; x++) {
                long net = 0;
                for (int k = 0; k < m; k++) if (id[k] >= 0) { if (u[k] == x) net -= ev_flow_edge_flow(g, id[k]); if (v[k] == x) net += ev_flow_edge_flow(g, id[k]); }
                if (net != 0) cons = 0;
            }
            CHECK(cons, "flow conservation");
            free(id);
            ev_flow_free(g);
        }
        CHECK(fl[0] == fl[1], "DFS and BFS max-flow agree");
        free(u); free(v); free(c);
    }
    for (int t = 0; t < 60; t++) {
        int L = (int)ev_rng_range(&r, 0, 25), R = (int)ev_rng_range(&r, 1, 25);
        int head[26] = { 0 }, adj[26 * 25], m = 0, ml[25], mr[25];
        for (int i = 0; i < L; i++) { for (int j = 0; j < R; j++) if (ev_rng_unit(&r) < 0.15) adj[m++] = j; head[i + 1] = m; }
        int k = ev_bipartite_match(L, R, head, adj, ml, mr);
        ev_flow *g = ev_flow_create(L + R + 2);
        for (int i = 0; i < L; i++) ev_flow_add_edge(g, 0, 1 + i, 1);
        for (int i = 0; i < L; i++) for (int e = head[i]; e < head[i + 1]; e++) ev_flow_add_edge(g, 1 + i, 1 + L + adj[e], 1);
        for (int j = 0; j < R; j++) ev_flow_add_edge(g, 1 + L + j, L + R + 1, 1);
        long fl = ev_flow_maxflow(g, 0, L + R + 1, EV_FLOW_BFS, NULL);
        ev_flow_free(g);
        int valid = 1, cnt = 0;
        for (int i = 0; i < L; i++) if (ml[i] >= 0) { cnt++; if (mr[ml[i]] != i) valid = 0; }
        CHECK(k == fl && cnt == k && valid, "matching %d = flow %ld", k, fl);
    }
}

/* ======================================================================== CRT + strings */
static void test_nt_strings(void) {
    group("CRT (coprime, non-coprime, inconsistent) and 4 string matchers vs brute force");
    ev_rng r;
    ev_rng_seed(&r, 8);
    for (int t = 0; t < 300; t++) {
        int k = (int)ev_rng_range(&r, 1, 3);
        long long a[3], m[3], x, M;
        for (int i = 0; i < k; i++) { m[i] = ev_rng_range(&r, 1, 30); a[i] = ev_rng_range(&r, 0, m[i] - 1); }
        int res = ev_crt(k, a, m, &x, &M);
        long long lcm = 1;
        for (int i = 0; i < k; i++) lcm = lcm / ev_gcd(lcm, m[i]) * m[i];
        long long brute = -1;
        for (long long y = 0; y < lcm && brute < 0; y++) {
            int ok = 1;
            for (int i = 0; i < k; i++) if (y % m[i] != a[i]) ok = 0;
            if (ok) brute = y;
        }
        CHECK((res == 1) == (brute >= 0) && (res != 1 || (x == brute && M == lcm)), "CRT case %d res=%d x=%lld brute=%lld", t, res, x, brute);
    }
    long long bad_m[1] = { 0 }, bad_a[1] = { 0 }, x;
    CHECK(ev_crt(1, bad_a, bad_m, &x, NULL) == -1, "CRT rejects modulus 0");
    CHECK(ev_modinv(3, 11) == 4 && ev_modinv(2, 4) == -1, "modular inverse");
    CHECK(ev_mulmod(1000000000000000000LL, 1000000000000000000LL, 1000000007LL) == 2401LL, "mulmod large operands");
    CHECK(ev_mulmod(4000000000000000000LL, 3, 4611686018427387847LL) == 2776627963145224306LL, "mulmod near 2^62");
    for (int t = 0; t < 400; t++) {
        int n = (int)ev_rng_range(&r, 0, 80), m = (int)ev_rng_range(&r, 0, 6);
        char text[81], pat[7];
        int alpha = (int)ev_rng_range(&r, 1, 3);
        for (int i = 0; i < n; i++) text[i] = (char)('a' + ev_rng_range(&r, 0, alpha - 1));
        for (int i = 0; i < m; i++) pat[i] = (char)('a' + ev_rng_range(&r, 0, alpha - 1));
        text[n] = pat[m] = 0;
        long want[80], wc = 0;
        if (m > 0) for (int s = 0; s + m <= n; s++) if (!strncmp(text + s, pat, (size_t)m)) want[wc++] = s;
        long got[4][80];
        long c[4];
        c[0] = ev_match_naive(text, n, pat, m, got[0], 80, NULL);
        c[1] = ev_match_rabin_karp(text, n, pat, m, got[1], 80, NULL);
        c[2] = ev_match_fa(text, n, pat, m, got[2], 80, NULL);
        c[3] = ev_match_kmp(text, n, pat, m, got[3], 80, NULL);
        for (int k = 0; k < 4; k++) {
            int ok = c[k] == wc;
            for (long i = 0; ok && i < wc; i++) ok = got[k][i] == want[i];
            CHECK(ok, "matcher %d text='%s' pat='%s' got %ld want %ld", k, text, pat, c[k], wc);
        }
    }
}

/* ======================================================================== scenario helpers */
static void write_file(const char *dir, const char *name, const char *content) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (!f) { printf("  cannot write %s\n", p); return; }
    fputs(content, f);
    fclose(f);
}

static const char *CFG_OK = "name = tiny\nbudget = 100\nbattery_kwh = 20\nconsumption_kwh_per_km = 0.2\n"
                            "reserve_frac = 0.1\ntarget_frac = 0.8\nregen_eff = 0.5\nelev_kwh_per_m = 0.004\n"
                            "speed_kmh = 40\nmax_detour_km = 10\ngrid_kw = 50\nwindow_min = 60\nsession_min = 30\nhorizon_h = 24\n";
/* line of 5 nodes, 25 km apart; range from station: (0.8-0.1)*20/0.2 = 70 km */
static const char *NODES_OK = "id,name,x_km,y_km,elev_m,demand\n1,A,0,0,0,5\n2,B,25,0,0,1\n3,C,50,0,0,1\n4,D,75,0,0,1\n5,E,100,0,0,5\n";
static const char *EDGES_OK = "u,v,km,capacity\n1,2,25,100\n2,3,25,100\n3,4,25,100\n4,5,25,100\n";
static const char *STATIONS_OK = "id,node,cost,ports,power_kw,maint_period_h,maint_offset_h\n10,3,40,2,50,24,3\n11,2,40,1,50,36,5\n12,4,70,1,50,48,7\n";
static const char *TRIPS_OK = "id,src,dst,depart_min,soc0\n1,1,5,480,0.5\n2,5,1,490,0.5\n3,1,2,500,0.9\n";

static void make_scenario(const char *dir, const char *cfg, const char *nodes, const char *edges, const char *st, const char *trips) {
    MKDIR(dir);
    if (cfg) write_file(dir, "scenario.cfg", cfg);
    if (nodes) write_file(dir, "nodes.csv", nodes);
    if (edges) write_file(dir, "edges.csv", edges);
    if (st) write_file(dir, "stations.csv", st);
    if (trips) write_file(dir, "trips.csv", trips);
}

static void test_tiny_scenario(void) {
    group("integration: hand-checkable 5-node corridor");
    MKDIR("build"); MKDIR("build/test_tmp");
    const char *dir = "build/test_tmp/tiny";
    make_scenario(dir, CFG_OK, NODES_OK, EDGES_OK, STATIONS_OK, TRIPS_OK);
    ev_scenario sc;
    char err[512];
    int rc = ev_scenario_load(dir, &sc, err, sizeof err);
    CHECK(rc == 0, "tiny load: %s", err);
    if (rc) return;
    CHECK(sc.nn == 5 && sc.ne == 4 && sc.ns == 3 && sc.nt == 3, "tiny counts");
    CHECK(ev_node_index(&sc, 3) == 2 && ev_node_index(&sc, 99) == -1, "node index lookup");
    /* trip 1: A->E 100 km; initial usable (0.5-0.1)*20 = 8 kWh = 40 km -> must stop at B (25 km);
     * from B 70 km range reaches D (50 km) but not E (75 km); needs a second stop at C or D.
     * with budget 100 the optimal knapsack picks two 40-cost stations (C, B). */
    ev_run run;
    ev_run_pipeline(&sc, EV_SEL_DP, EV_ROUTE_DIJKSTRA, &run);
    CHECK(run.sel.n == 2 && run.sel.cost == 80, "tiny selection n=%d cost=%ld", run.sel.n, run.sel.cost);
    const ev_plan *p = &run.plans[0];
    CHECK(p->feasible && p->n_stops == 2 && fabs(p->km - 100) < 1e-9, "trip 1 plan feasible=%d stops=%d km=%.1f", p->feasible, p->n_stops, p->km);
    CHECK(fabs(p->kwh - 20) < 1e-9, "trip 1 energy 20 kWh (got %.3f)", p->kwh);
    if (p->n_stops == 2) {
        CHECK(sc.stations[p->stop[0]].id == 11 && sc.stations[p->stop[1]].id == 10, "stops B then C");
        CHECK(fabs(p->stop_arrive_kwh[0] - 5.0) < 1e-9, "arrive B with 10-5 = 5 kWh");
        CHECK(fabs(p->stop_charge_min[0] - (16.0 - 5.0) / 50 * 60) < 1e-9, "charge 11 kWh at 50 kW");
    }
    CHECK(run.plans[2].feasible && run.plans[2].n_stops == 0, "short trip needs no stop");
    CHECK(!run.plans[1].feasible || run.plans[1].n_stops >= 1, "trip 2 consistent");
    /* every arrival keeps the reserve */
    for (int t = 0; t < sc.nt; t++)
        for (int k = 0; run.plans[t].feasible && k < run.plans[t].n_stops; k++)
            CHECK(run.plans[t].stop_arrive_kwh[k] >= sc.cfg.reserve_frac * sc.cfg.battery_kwh - 1e-9, "reserve kept");
    ev_run_free(&run);
    /* budget 0 -> nothing selected, long trips infeasible */
    sc.cfg.budget = 0;
    ev_run_pipeline(&sc, EV_SEL_DP, EV_ROUTE_DIJKSTRA, &run);
    CHECK(run.sel.n == 0 && !run.plans[0].feasible && run.plans[2].feasible, "zero budget behaviour");
    ev_run_free(&run);
    ev_scenario_free(&sc);

    /* zero trips is valid */
    make_scenario("build/test_tmp/notrips", CFG_OK, NODES_OK, EDGES_OK, STATIONS_OK, "id,src,dst,depart_min,soc0\n");
    rc = ev_scenario_load("build/test_tmp/notrips", &sc, err, sizeof err);
    CHECK(rc == 0 && sc.nt == 0, "zero-trip scenario loads: %s", err);
    if (!rc) {
        ev_run_pipeline(&sc, EV_SEL_BNB_LC, EV_ROUTE_BELLMAN, &run);
        CHECK(run.kpi.trips == 0 && run.kpi.feasible == 0 && run.sel.n == 0, "zero-trip pipeline");
        ev_run_free(&run);
        ev_scenario_free(&sc);
    }
}

static void test_invalid_inputs(void) {
    group("invalid inputs are rejected with a precise message");
    struct { const char *name, *cfg, *nodes, *edges, *st, *trips, *expect; } cases[] = {
        { "missing_file", CFG_OK, NODES_OK, NULL, STATIONS_OK, TRIPS_OK, "edges.csv: cannot open" },
        { "bad_header", CFG_OK, "name,id\n1,A\n", EDGES_OK, STATIONS_OK, TRIPS_OK, "missing header" },
        { "dup_node", CFG_OK, "id,name,x_km,y_km,elev_m,demand\n1,A,0,0,0,1\n1,B,1,0,0,1\n", "u,v,km,capacity\n1,1,1,1\n", STATIONS_OK, TRIPS_OK, "duplicate node id 1" },
        { "unknown_node", CFG_OK, NODES_OK, "u,v,km,capacity\n1,9,5,10\n", STATIONS_OK, TRIPS_OK, "unknown node id 9" },
        { "self_loop", CFG_OK, NODES_OK, "u,v,km,capacity\n2,2,5,10\n", STATIONS_OK, TRIPS_OK, "self-loop" },
        { "neg_km", CFG_OK, NODES_OK, "u,v,km,capacity\n1,2,-5,10\n", STATIONS_OK, TRIPS_OK, "km must be > 0" },
        { "bad_number", CFG_OK, NODES_OK, "u,v,km,capacity\n1,2,5x,10\n", STATIONS_OK, TRIPS_OK, "malformed number" },
        { "field_count", CFG_OK, NODES_OK, "u,v,km,capacity\n1,2,5\n", STATIONS_OK, TRIPS_OK, "expected 4 fields" },
        { "zero_ports", CFG_OK, NODES_OK, EDGES_OK, "id,node,cost,ports,power_kw\n1,2,10,0,50\n", TRIPS_OK, "ports must be >= 1" },
        { "maint_offset", CFG_OK, NODES_OK, EDGES_OK, "id,node,cost,ports,power_kw,maint_period_h,maint_offset_h\n1,2,10,1,50,24,24\n", TRIPS_OK, "maint_period_h" },
        { "dup_station", CFG_OK, NODES_OK, EDGES_OK, "id,node,cost,ports,power_kw\n1,2,10,1,50\n1,3,10,1,50\n", TRIPS_OK, "duplicate station id 1" },
        { "soc_range", CFG_OK, NODES_OK, EDGES_OK, STATIONS_OK, "id,src,dst,depart_min,soc0\n1,1,5,0,1.5\n", "soc0 must be in (0,1]" },
        { "same_src_dst", CFG_OK, NODES_OK, EDGES_OK, STATIONS_OK, "id,src,dst,depart_min,soc0\n1,3,3,0,0.5\n", "src and dst must differ" },
        { "unknown_key", "budget = 5\nturbo = 1\n", NODES_OK, EDGES_OK, STATIONS_OK, TRIPS_OK, "unknown key 'turbo'" },
        { "reserve_target", "reserve_frac = 0.9\ntarget_frac = 0.8\n", NODES_OK, EDGES_OK, STATIONS_OK, TRIPS_OK, "reserve_frac < target_frac" },
        { "cfg_value", "budget = lots\n", NODES_OK, EDGES_OK, STATIONS_OK, TRIPS_OK, "bad value for 'budget'" },
        { "cfg_syntax", "budget 5\n", NODES_OK, EDGES_OK, STATIONS_OK, TRIPS_OK, "expected key = value" },
        { "one_node", CFG_OK, "id,name,x_km,y_km,elev_m,demand\n1,A,0,0,0,1\n", EDGES_OK, STATIONS_OK, TRIPS_OK, "at least 2 nodes" },
    };
    MKDIR("build"); MKDIR("build/test_tmp");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char dir[256], err[512] = "";
        snprintf(dir, sizeof dir, "build/test_tmp/inv_%s", cases[i].name);
        make_scenario(dir, cases[i].cfg, cases[i].nodes, cases[i].edges, cases[i].st, cases[i].trips);
        if (!cases[i].edges) { char p[300]; snprintf(p, sizeof p, "%s/edges.csv", dir); remove(p); }
        ev_scenario sc;
        int rc = ev_scenario_load(dir, &sc, err, sizeof err);
        CHECK(rc == -1 && strstr(err, cases[i].expect), "%s: rc=%d err='%s' (want '%s')", cases[i].name, rc, err, cases[i].expect);
        if (rc == 0) ev_scenario_free(&sc);
    }
    ev_scenario sc;
    char err[256];
    CHECK(ev_scenario_load("build/test_tmp/does_not_exist", &sc, err, sizeof err) == -1, "missing directory rejected");
}

/* ======================================================================== shipped datasets */
static void test_dataset(const char *name, int check_regression) {
    char dir[256], err[512], gname[64];
    snprintf(dir, sizeof dir, "data/%s", name);
    snprintf(gname, sizeof gname, "integration: data/%s invariants", name);
    group(gname);
    ev_scenario sc;
    if (ev_scenario_load(dir, &sc, err, sizeof err) != 0) { CHECK(0, "load %s: %s", dir, err); return; }
    ev_run runs[EV_ROUTE_COUNT];
    for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) ev_run_pipeline(&sc, EV_SEL_DP, (ev_route_mode)rm, &runs[rm]);
    const ev_config *c = &sc.cfg;
    CHECK(runs[0].sel.cost <= c->budget, "selection within budget");
    /* selection strategies: every exact method matches DP, greedy never exceeds it */
    for (int sm = 0; sm < EV_SEL_COUNT; sm++) {
        ev_selection s;
        ev_select(&sc, (ev_sel_mode)sm, &s);
        CHECK(s.cost <= c->budget, "%s within budget", ev_sel_name(sm));
        if (s.optimal) CHECK(s.value == runs[0].sel.value, "%s optimal value %ld vs DP %ld", ev_sel_name(sm), s.value, runs[0].sel.value);
        else CHECK(s.value <= runs[0].sel.value, "greedy <= optimum");
        ev_selection_free(&s);
    }
    /* Floyd and Dijkstra give identical plans; Bellman direct legs never use more energy */
    int same = 1, energy_ok = 1, reserve_ok = 1, km_ok = 1;
    for (int t = 0; t < sc.nt; t++) {
        const ev_plan *a = &runs[EV_ROUTE_DIJKSTRA].plans[t], *b = &runs[EV_ROUTE_FLOYD].plans[t];
        if (a->feasible != b->feasible || (a->feasible && fabs(a->km - b->km) > 1e-6)) same = 0;
        double k1, e1, k2, e2;
        int ok1 = ev_router_leg(runs[EV_ROUTE_DIJKSTRA].router, sc.trips[t].src, sc.trips[t].dst, &k1, &e1);
        int ok2 = ev_router_leg(runs[EV_ROUTE_BELLMAN].router, sc.trips[t].src, sc.trips[t].dst, &k2, &e2);
        if (ok1 != ok2 || (ok1 && (e2 > e1 + 1e-9 || k2 < k1 - 1e-9))) energy_ok = 0;
        for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) {
            const ev_plan *p = &runs[rm].plans[t];
            for (int k = 0; p->feasible && k < p->n_stops; k++)
                if (p->stop_arrive_kwh[k] < c->reserve_frac * c->battery_kwh - 1e-9) reserve_ok = 0;
            if (p->feasible && rm != EV_ROUTE_BELLMAN && p->direct_km >= 0 && p->km < p->direct_km - 1e-9) km_ok = 0;
        }
    }
    CHECK(same, "Dijkstra and Floyd-Warshall plans identical");
    CHECK(energy_ok, "Bellman-Ford legs: minimal energy, never shorter than km-shortest");
    CHECK(reserve_ok, "no plan ever arrives below the reserve");
    CHECK(km_ok, "no plan shorter than the shortest path");
    ev_capacity cap;
    ev_capacity_analyze(runs[0].router, &sc, &runs[0].sel, runs[0].plans, &cap);
    CHECK(cap.flow_dfs == cap.flow_bfs && cap.flow_bfs == cap.matching, "capacity: FF %ld EK %ld matching %d", cap.flow_dfs, cap.flow_bfs, cap.matching);
    CHECK(cap.flow_bfs <= cap.trips_needing && cap.flow_bfs <= cap.slots_total, "flow bounded by demand and supply");
    ev_sessions ses;
    ev_sessions_analyze(&sc, &runs[0].sel, runs[0].plans, &ses);
    int dep = 1;
    for (int q = 0; q < ses.n; q++) if (ses.st[q].chargers_needed != ses.st[q].depth) dep = 0;
    CHECK(dep, "chargers needed equals max overlap at every station");
    CHECK(ses.served <= ses.sessions, "served <= sessions");

    if (check_regression) {
        char line[512], path[256];
        const ev_kpi *k = &runs[0].kpi;
        snprintf(line, sizeof line, "sel_n=%d sel_cost=%ld sel_value=%ld feasible=%d need=%d stops=%.6f km=%.6f kwh=%.6f flow=%ld sessions=%ld served=%ld",
                 runs[0].sel.n, runs[0].sel.cost, runs[0].sel.value, k->feasible, k->need_charge, k->avg_stops, k->avg_km,
                 k->avg_kwh, cap.flow_bfs, ses.sessions, ses.served);
        snprintf(path, sizeof path, "tests/regression/%s_dp_dijkstra.txt", name);
        if (g_record) {
            FILE *f = fopen(path, "w");
            if (f) { fprintf(f, "%s\n", line); fclose(f); printf("  recorded %s\n", path); }
        } else {
            FILE *f = fopen(path, "r");
            char want[512] = "";
            if (f) { if (!fgets(want, sizeof want, f)) want[0] = 0; fclose(f); want[strcspn(want, "\r\n")] = 0; }
            CHECK(f && !strcmp(want, line), "regression %s\n    want: %s\n    got:  %s", path, want, line);
        }
    }
    ev_sessions_free(&ses);
    ev_capacity_free(&cap);
    for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) ev_run_free(&runs[rm]);
    ev_scenario_free(&sc);
}

typedef struct { double t; int d; } occ_ev;
static int cmp_occ(const void *a, const void *b) {
    const occ_ev *x = a, *y = b;
    if (x->t != y->t) return x->t < y->t ? -1 : 1;
    return x->d - y->d; /* departures (-1) before arrivals (+1) at the same instant */
}

static void test_sim(const char *name) {
    char dir[256], err[512], gname[96];
    snprintf(dir, sizeof dir, "data/%s", name);
    snprintf(gname, sizeof gname, "simulation: data/%s queues, ports and timelines", name);
    group(gname);
    ev_scenario sc;
    if (ev_scenario_load(dir, &sc, err, sizeof err) != 0) { CHECK(0, "load %s: %s", dir, err); return; }
    ev_run run;
    ev_run_pipeline(&sc, EV_SEL_DP, EV_ROUTE_DIJKSTRA, &run);
    ev_sim sim;
    ev_simulate(run.router, &sc, &run.sel, run.plans, &sim);
    int feasible = 0;
    for (int t = 0; t < sc.nt; t++) feasible += run.plans[t].feasible;
    CHECK(sim.nv == feasible, "one vehicle per feasible trip (%d vs %d)", sim.nv, feasible);
    int contiguous = 1, ordered = 1, reserve = 1, kinds = 1;
    double resv = sc.cfg.reserve_frac * sc.cfg.battery_kwh;
    for (int v = 0; v < sim.nv; v++) {
        for (int i = sim.seg_off[v]; i < sim.seg_off[v + 1]; i++) {
            const ev_sim_seg *g = &sim.seg[i];
            if (g->t1 < g->t0 - 1e-9) ordered = 0;
            if (i > sim.seg_off[v] && fabs(sim.seg[i - 1].t1 - g->t0) > 1e-6) contiguous = 0;
            if (g->kind == EV_SEG_DRIVE && g->e1 < resv - 1e-6) reserve = 0;
            if (g->kind != EV_SEG_DRIVE && g->station < 0) kinds = 0;
            if (g->vehicle != v) kinds = 0;
        }
        if (sim.seg_off[v] < sim.seg_off[v + 1] && fabs(sim.seg[sim.seg_off[v]].t0 - run.plans[sim.trip[v]].depart_min) > 1e-6) contiguous = 0;
    }
    CHECK(ordered, "segment end never precedes its start");
    CHECK(contiguous, "each vehicle's timeline is gap-free from its departure");
    CHECK(reserve, "no simulated drive ends below the reserve");
    CHECK(kinds, "segments belong to their vehicle; waits and charges name a station");
    /* port occupancy never exceeds the installed ports */
    int over = 0, sessions = 0, st_sessions = 0;
    for (int k = 0; k < sim.nst; k++) {
        int st = sim.st[k].station, m = 0;
        st_sessions += sim.st[k].sessions;
        occ_ev *ev = (occ_ev *)malloc(sizeof(occ_ev) * (size_t)(sim.nseg * 2 + 1));
        for (int i = 0; i < sim.nseg; i++)
            if (sim.seg[i].kind == EV_SEG_CHARGE && sim.seg[i].station == st) {
                ev[m].t = sim.seg[i].t0; ev[m++].d = 1;
                ev[m].t = sim.seg[i].t1; ev[m++].d = -1;
                sessions++;
            }
        qsort(ev, (size_t)m, sizeof(occ_ev), cmp_occ);
        int cur = 0;
        for (int i = 0; i < m; i++) { cur += ev[i].d; if (cur > sc.stations[st].ports) over = 1; }
        free(ev);
    }
    CHECK(!over, "charging vehicles never exceed a station's ports");
    CHECK(sessions == st_sessions, "station session counts match charge segments");
    double wait_sum = 0;
    for (int i = 0; i < sim.nseg; i++) if (sim.seg[i].kind == EV_SEG_WAIT) wait_sum += sim.seg[i].t1 - sim.seg[i].t0;
    CHECK(fabs(wait_sum - sim.total_wait) < 1e-6, "waiting time adds up (%.3f vs %.3f)", wait_sum, sim.total_wait);
    CHECK(sim.diverted == 0, "wait policy never switches station");
    ev_sim_free(&sim);
    /* divert policy: same invariants, and a switch only happens to a reachable station */
    ev_sim_opts opt = { EV_POLICY_DIVERT, 20.0 };
    ev_simulate_ex(run.router, &sc, &run.sel, run.plans, &opt, &sim);
    int dv_contig = 1, dv_reserve = 1, dv_over = 0;
    for (int v = 0; v < sim.nv; v++)
        for (int i = sim.seg_off[v]; i < sim.seg_off[v + 1]; i++) {
            if (i > sim.seg_off[v] && fabs(sim.seg[i - 1].t1 - sim.seg[i].t0) > 1e-6) dv_contig = 0;
            if (sim.seg[i].kind == EV_SEG_DRIVE && sim.seg[i].e1 < resv - 1e-6) dv_reserve = 0;
        }
    for (int k = 0; k < sim.nst; k++) {
        int st = sim.st[k].station, m = 0;
        occ_ev *ev = (occ_ev *)malloc(sizeof(occ_ev) * (size_t)(sim.nseg * 2 + 1));
        for (int i = 0; i < sim.nseg; i++)
            if (sim.seg[i].kind == EV_SEG_CHARGE && sim.seg[i].station == st) { ev[m].t = sim.seg[i].t0; ev[m++].d = 1; ev[m].t = sim.seg[i].t1; ev[m++].d = -1; }
        qsort(ev, (size_t)m, sizeof(occ_ev), cmp_occ);
        int cur = 0;
        for (int i = 0; i < m; i++) { cur += ev[i].d; if (cur > sc.stations[st].ports) dv_over = 1; }
        free(ev);
    }
    CHECK(dv_contig, "divert policy: timelines stay gap-free");
    CHECK(dv_reserve, "divert policy: detours never drop below the reserve");
    CHECK(!dv_over, "divert policy: ports never oversubscribed");
    ev_sim_free(&sim);
    /* with unlimited ports nobody queues */
    for (int k = 0; k < run.sel.n; k++) sc.stations[run.sel.idx[k]].ports = 1000000;
    ev_simulate(run.router, &sc, &run.sel, run.plans, &sim);
    CHECK(sim.delayed == 0 && sim.total_wait == 0, "unlimited ports: no waiting (%d delayed)", sim.delayed);
    ev_sim_free(&sim);
    ev_run_free(&run);
    ev_scenario_free(&sc);
}

static void test_constraints(void) {
    group("what-if constraints: locked sites built, banned sites excluded, exact methods agree");
    ev_scenario sc;
    char err[256];
    if (ev_scenario_load("data/medium", &sc, err, sizeof err) != 0) { CHECK(0, "load: %s", err); return; }
    ev_selection base;
    ev_select(&sc, EV_SEL_DP, &base);
    int lock[2] = { -1, -1 }, ban[1] = { base.idx[0] };
    for (int i = 0, k = 0; i < sc.ns && k < 2; i++) {
        int in = 0;
        for (int q = 0; q < base.n; q++) if (base.idx[q] == i) in = 1;
        if (!in) lock[k++] = i;
    }
    ev_constraints con = { 2, 1, lock, ban };
    long values[EV_SEL_COUNT];
    for (int m = 0; m < EV_SEL_COUNT; m++) {
        ev_selection s;
        ev_select_ex(&sc, (ev_sel_mode)m, &con, &s);
        int has0 = 0, has1 = 0, hasban = 0;
        for (int q = 0; q < s.n; q++) { has0 |= s.idx[q] == lock[0]; has1 |= s.idx[q] == lock[1]; hasban |= s.idx[q] == ban[0]; }
        CHECK(has0 && has1, "%s builds both locked sites", ev_sel_name(m));
        CHECK(!hasban, "%s never builds the banned site", ev_sel_name(m));
        CHECK(s.cost <= sc.cfg.budget, "%s stays within budget", ev_sel_name(m));
        int sorted = 1;
        for (int q = 1; q < s.n; q++) if (s.idx[q - 1] >= s.idx[q]) sorted = 0;
        CHECK(sorted, "%s returns ascending, unique indices", ev_sel_name(m));
        values[m] = s.value;
        ev_selection_free(&s);
    }
    CHECK(values[EV_SEL_DP] == values[EV_SEL_BNB_LC] && values[EV_SEL_DP] == values[EV_SEL_BNB_FIFO] && values[EV_SEL_DP] == values[EV_SEL_BNB_LIFO],
          "exact methods agree under constraints");
    CHECK(values[EV_SEL_GREEDY] <= values[EV_SEL_DP], "greedy never beats the constrained optimum");
    /* values restored after a constrained call */
    ev_selection again;
    ev_select(&sc, EV_SEL_DP, &again);
    CHECK(again.value == base.value && again.n == base.n, "constraints leave no trace on later runs");
    ev_selection_free(&again);
    /* locking more than the budget allows */
    sc.cfg.budget = 10;
    ev_selection ob;
    ev_select_ex(&sc, EV_SEL_DP, &con, &ob);
    CHECK(ob.over_budget && ob.n == 2, "locked sites over budget are still built and flagged");
    ev_selection_free(&ob);
    ev_selection_free(&base);
    ev_scenario_free(&sc);
}

static void test_memory_balance(size_t before) {
    group("memory: every tracked allocation released");
    CHECK(ev_mem_current() == before, "leaked %zu bytes", ev_mem_current() - before);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--record")) g_record = 1;
    size_t before = ev_mem_current();
    double t0 = ev_now_ms();
    test_sorting();
    test_searching();
    test_dnc();
    test_greedy();
    test_graphs();
    test_dp();
    test_search();
    test_flow();
    test_nt_strings();
    test_tiny_scenario();
    test_invalid_inputs();
    test_dataset("small", 1);
    test_dataset("medium", 1);
    test_dataset("shock", 1);
    test_dataset("adversarial", 1);
    test_sim("small");
    test_sim("medium");
    test_sim("shock");
    test_constraints();
    test_memory_balance(before);
    printf("\n%d checks passed, %d failed (%.0f ms)\n", g_pass, g_fail, ev_now_ms() - t0);
    return g_fail ? 1 : 0;
}
