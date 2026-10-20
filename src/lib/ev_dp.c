#include "ev_dp.h"
#include "ev_mem.h"

#include <stdio.h>
#include <string.h>

#define BIG 1e300

/* ------------------------------------------------------------ 0/1 knapsack */
long ev_knapsack01(int n, const int *w, const long *v, int W, int *take) {
    if (n < 0 || W < 0) return -1;
    for (int i = 0; i < n; i++) if (w[i] < 0 || v[i] < 0) return -1;
    long *best = (long *)ev_calloc((size_t)W + 1, sizeof(long));
    size_t row = ((size_t)W + 8) / 8;
    unsigned char *keep = (unsigned char *)ev_calloc((size_t)(n ? n : 1) * row, 1);
    for (int i = 0; i < n; i++) {
        unsigned char *k = keep + (size_t)i * row;
        for (int c = W; c >= w[i]; c--) {
            long cand = best[c - w[i]] + v[i];
            if (cand > best[c]) { best[c] = cand; k[c >> 3] |= (unsigned char)(1u << (c & 7)); }
        }
    }
    long ans = best[W];
    if (take) {
        int c = W;
        for (int i = n - 1; i >= 0; i--) {
            unsigned char *k = keep + (size_t)i * row;
            if ((k[c >> 3] >> (c & 7)) & 1) { take[i] = 1; c -= w[i]; }
            else take[i] = 0;
        }
    }
    ev_free(best);
    ev_free(keep);
    return ans;
}

long ev_knapsack_brute(int n, const int *w, const long *v, int W, int *take) {
    if (n > 25 || n < 0) return -1;
    long best = 0;
    unsigned long bestmask = 0;
    for (unsigned long mask = 0; mask < (1UL << n); mask++) {
        long cw = 0, cv = 0;
        for (int i = 0; i < n; i++) if (mask >> i & 1UL) { cw += w[i]; cv += v[i]; }
        if (cw <= W && cv > best) { best = cv; bestmask = mask; }
    }
    if (take) for (int i = 0; i < n; i++) take[i] = (int)(bestmask >> i & 1UL);
    return best;
}

/* ------------------------------------------------------------ matrix chain */
long long ev_matrix_chain(int n, const long *dims, int *split) {
    if (n <= 0) return 0;
    long long *m = (long long *)ev_calloc((size_t)n * n, sizeof(long long));
    for (int len = 2; len <= n; len++)
        for (int i = 0; i + len - 1 < n; i++) {
            int j = i + len - 1;
            m[(size_t)i * n + j] = -1;
            for (int k = i; k < j; k++) {
                long long c = m[(size_t)i * n + k] + m[(size_t)(k + 1) * n + j] +
                              (long long)dims[i] * dims[k + 1] * dims[j + 1];
                if (m[(size_t)i * n + j] < 0 || c < m[(size_t)i * n + j]) {
                    m[(size_t)i * n + j] = c;
                    if (split) split[(size_t)i * n + j] = k;
                }
            }
        }
    long long ans = m[n - 1];
    ev_free(m);
    return ans;
}

static void paren_rec(int n, const int *split, int i, int j, char *buf, int cap, int *pos) {
    if (i == j) { *pos += snprintf(buf + *pos, (size_t)(cap > *pos ? cap - *pos : 0), "A%d", i + 1); if (*pos > cap) *pos = cap; return; }
    if (*pos < cap - 1) buf[(*pos)++] = '(';
    int k = split[(size_t)i * n + j];
    paren_rec(n, split, i, k, buf, cap, pos);
    paren_rec(n, split, k + 1, j, buf, cap, pos);
    if (*pos < cap - 1) buf[(*pos)++] = ')';
}

void ev_matrix_chain_paren(int n, const int *split, char *buf, int cap) {
    int pos = 0;
    if (cap <= 0) return;
    buf[0] = '\0';
    if (n <= 0) return;
    paren_rec(n, split, 0, n - 1, buf, cap, &pos);
    if (pos >= cap) pos = cap - 1;
    buf[pos] = '\0';
}

long long ev_matrix_chain_left(int n, const long *dims) {
    long long c = 0;
    for (int k = 1; k < n; k++) c += (long long)dims[0] * dims[k] * dims[k + 1];
    return c;
}

long long ev_matrix_chain_right(int n, const long *dims) {
    long long c = 0;
    for (int k = n - 2; k >= 0; k--) c += (long long)dims[k] * dims[k + 1] * dims[n];
    return c;
}

/* ------------------------------------------------------------ LCS */
int ev_lcs(const int *a, int na, const int *b, int nb, int *out) {
    if (na <= 0 || nb <= 0) return 0;
    int *L = (int *)ev_calloc((size_t)(na + 1) * (nb + 1), sizeof(int));
#define LL(i, j) L[(size_t)(i) * (nb + 1) + (j)]
    for (int i = 1; i <= na; i++)
        for (int j = 1; j <= nb; j++)
            LL(i, j) = (a[i - 1] == b[j - 1]) ? LL(i - 1, j - 1) + 1
                                               : (LL(i - 1, j) >= LL(i, j - 1) ? LL(i - 1, j) : LL(i, j - 1));
    int len = LL(na, nb);
    if (out) {
        int i = na, j = nb, k = len;
        while (i > 0 && j > 0) {
            if (a[i - 1] == b[j - 1]) { out[--k] = a[i - 1]; i--; j--; }
            else if (LL(i - 1, j) >= LL(i, j - 1)) i--;
            else j--;
        }
    }
#undef LL
    ev_free(L);
    return len;
}

/* ------------------------------------------------------------ optimal BST */
double ev_optimal_bst(int n, const double *p, const double *q, int *root) {
    int N = n + 2;
    double *e = (double *)ev_calloc((size_t)N * N, sizeof(double));
    double *w = (double *)ev_calloc((size_t)N * N, sizeof(double));
#define E(i, j) e[(size_t)(i) * N + (j)]
#define Wt(i, j) w[(size_t)(i) * N + (j)]
    for (int i = 1; i <= n + 1; i++) { E(i, i - 1) = q[i - 1]; Wt(i, i - 1) = q[i - 1]; }
    for (int l = 1; l <= n; l++)
        for (int i = 1; i <= n - l + 1; i++) {
            int j = i + l - 1;
            E(i, j) = BIG;
            Wt(i, j) = Wt(i, j - 1) + p[j] + q[j];
            for (int r = i; r <= j; r++) {
                double t = E(i, r - 1) + E(r + 1, j) + Wt(i, j);
                if (t < E(i, j)) { E(i, j) = t; if (root) root[(size_t)i * N + j] = r; }
            }
        }
    double ans = n >= 1 ? E(1, n) : q[0];
#undef E
#undef Wt
    ev_free(e);
    ev_free(w);
    return ans;
}

static double bal_rec(const double *p, const double *q, int i, int j, int d) {
    if (j < i) return (d + 1) * q[j];
    int r = (i + j) / 2;
    return (d + 1) * p[r] + bal_rec(p, q, i, r - 1, d + 1) + bal_rec(p, q, r + 1, j, d + 1);
}

double ev_balanced_bst_cost(int n, const double *p, const double *q) {
    return bal_rec(p, q, 1, n, 0);
}

/* ------------------------------------------------------------ TSP */
double ev_tsp_held_karp(int n, const double *D, int *tour) {
    if (n <= 0 || n > 20) return BIG;
    if (n == 1) { if (tour) { tour[0] = 0; tour[1] = 0; } return 0; }
    int k = n - 1;                         /* vertices 1..n-1 encoded as bits 0..k-1 */
    size_t S = (size_t)1 << k;
    double *dp = (double *)ev_malloc(sizeof(double) * S * (size_t)k);
    signed char *par = (signed char *)ev_malloc(S * (size_t)k);
    for (size_t i = 0; i < S * (size_t)k; i++) { dp[i] = BIG; par[i] = -1; }
    for (int j = 0; j < k; j++) dp[((size_t)1 << j) * k + j] = D[0 * n + (j + 1)];
    for (size_t mask = 1; mask < S; mask++)
        for (int j = 0; j < k; j++) {
            if (!(mask >> j & 1)) continue;
            double cur = dp[mask * k + j];
            if (cur >= BIG) continue;
            for (int t = 0; t < k; t++) {
                if (mask >> t & 1) continue;
                double dd = D[(size_t)(j + 1) * n + (t + 1)];
                if (dd >= BIG) continue;
                size_t nm = mask | ((size_t)1 << t);
                if (cur + dd < dp[nm * k + t]) { dp[nm * k + t] = cur + dd; par[nm * k + t] = (signed char)j; }
            }
        }
    double best = BIG;
    int last = -1;
    for (int j = 0; j < k; j++) {
        double c = dp[(S - 1) * k + j];
        double back = D[(size_t)(j + 1) * n + 0];
        if (c < BIG && back < BIG && c + back < best) { best = c + back; last = j; }
    }
    if (tour && last >= 0) {
        size_t mask = S - 1;
        int pos = n - 1, j = last;
        tour[0] = 0; tour[n] = 0;
        while (j >= 0) {
            tour[pos--] = j + 1;
            int pj = par[mask * k + j];
            mask &= ~((size_t)1 << j);
            j = pj;
        }
    }
    ev_free(dp);
    ev_free(par);
    return best;
}

static void tsp_perm(int n, const double *D, int *perm, int depth, double cost, double *best, int *bt) {
    if (cost >= *best) return;
    if (depth == n) {
        double back = D[(size_t)perm[n - 1] * n + 0];
        if (back < BIG && cost + back < *best) {
            *best = cost + back;
            for (int i = 0; i < n; i++) bt[i] = perm[i];
        }
        return;
    }
    for (int i = depth; i < n; i++) {
        int t = perm[depth]; perm[depth] = perm[i]; perm[i] = t;
        double d = D[(size_t)perm[depth - 1] * n + perm[depth]];
        if (d < BIG) tsp_perm(n, D, perm, depth + 1, cost + d, best, bt);
        t = perm[depth]; perm[depth] = perm[i]; perm[i] = t;
    }
}

double ev_tsp_brute(int n, const double *D, int *tour) {
    if (n <= 0 || n > 11) return BIG;
    if (n == 1) { if (tour) { tour[0] = 0; tour[1] = 0; } return 0; }
    int perm[12], bt[12];
    for (int i = 0; i < n; i++) perm[i] = i;
    double best = BIG;
    tsp_perm(n, D, perm, 1, 0, &best, bt);
    if (tour && best < BIG) { for (int i = 0; i < n; i++) tour[i] = bt[i]; tour[n] = 0; }
    return best;
}
