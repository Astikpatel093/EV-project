#include "ev_search.h"
#include "ev_mem.h"
#include "ev_sort.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------ N-Queens */
typedef struct { int n; long count, nodes; int *cols, *first; int have_first; unsigned full; } nq_ctx;

static void nq_rec(nq_ctx *c, int row, unsigned colmask, unsigned d1, unsigned d2) {
    c->nodes++;
    if (row == c->n) {
        if (!c->have_first && c->first) { memcpy(c->first, c->cols, sizeof(int) * (size_t)c->n); c->have_first = 1; }
        c->count++;
        return;
    }
    unsigned avail = c->full & ~(colmask | d1 | d2);
    while (avail) {
        unsigned bit = avail & (0u - avail);
        avail ^= bit;
        int col = 0;
        while (!((bit >> col) & 1u)) col++;
        c->cols[row] = col;
        nq_rec(c, row + 1, colmask | bit, ((d1 | bit) << 1) & c->full, (d2 | bit) >> 1);
    }
}

long ev_nqueens_count(int n, int *first, long *nodes) {
    if (n <= 0 || n > 30) { if (nodes) *nodes = 0; return n == 0 ? 1 : -1; }
    nq_ctx c;
    memset(&c, 0, sizeof c);
    c.n = n;
    c.first = first;
    c.full = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    c.cols = (int *)ev_malloc(sizeof(int) * (size_t)n);
    nq_rec(&c, 0, 0, 0, 0);
    ev_free(c.cols);
    if (nodes) *nodes = c.nodes;
    return c.count;
}

/* ------------------------------------------------------------ subset sum */
typedef struct { int v; int idx; } vi;
static int cmp_vi_desc(const void *a, const void *b) {
    const vi *x = (const vi *)a, *y = (const vi *)b;
    if (x->v != y->v) return y->v - x->v;
    return x->idx - y->idx;
}

typedef struct { const vi *it; const long *suffix; int n; long target; int *pick; long nodes, limit; int aborted; } ss_ctx;

static int ss_rec(ss_ctx *c, int i, long sum) {
    c->nodes++;
    if (c->limit > 0 && c->nodes > c->limit) { c->aborted = 1; return 0; }
    if (sum == c->target) return 1;
    if (i == c->n) return 0;
    if (sum + c->suffix[i] < c->target) return 0; /* cannot reach */
    if (sum + c->it[i].v <= c->target) {
        c->pick[i] = 1;
        if (ss_rec(c, i + 1, sum + c->it[i].v)) return 1;
        c->pick[i] = 0;
        if (c->aborted) return 0;
    }
    return ss_rec(c, i + 1, sum);
}

int ev_subset_sum(const int *a, int n, long target, long node_limit, int *take, long *nodes) {
    if (take) for (int i = 0; i < n; i++) take[i] = 0;
    if (target < 0) { if (nodes) *nodes = 0; return 0; }
    vi *it = (vi *)ev_malloc(sizeof(vi) * (size_t)(n ? n : 1));
    long *suffix = (long *)ev_calloc((size_t)n + 1, sizeof(long));
    int *pick = (int *)ev_calloc((size_t)(n ? n : 1), sizeof(int));
    for (int i = 0; i < n; i++) { it[i].v = a[i] < 0 ? 0 : a[i]; it[i].idx = i; }
    ev_merge_sort(it, (size_t)n, sizeof(vi), cmp_vi_desc);
    for (int i = n - 1; i >= 0; i--) suffix[i] = suffix[i + 1] + it[i].v;
    ss_ctx c = { it, suffix, n, target, pick, 0, node_limit, 0 };
    int ok = ss_rec(&c, 0, 0);
    if (!ok && c.aborted) ok = -1;
    if (ok == 1 && take) for (int i = 0; i < n; i++) if (pick[i]) take[it[i].idx] = 1;
    if (nodes) *nodes = c.nodes;
    ev_free(it); ev_free(suffix); ev_free(pick);
    return ok;
}

int ev_subset_sum_dp(const int *a, int n, long target) {
    if (target < 0) return 0;
    if (target > 50000000L) return -1;
    unsigned char *r = (unsigned char *)ev_calloc((size_t)target + 1, 1);
    r[0] = 1;
    for (int i = 0; i < n; i++) {
        if (a[i] <= 0) continue;
        for (long s = target; s >= a[i]; s--) if (r[s - a[i]]) r[s] = 1;
    }
    int ans = r[target];
    ev_free(r);
    return ans;
}

/* ------------------------------------------------------------ knapsack branch and bound */
typedef struct { int level; int parent; long weight, value; double bound; char took; } bnode;

typedef struct {
    int n, W;
    const int *w; const long *v;
    int *ord; /* items in decreasing value/weight */
} kctx;

static double kbound(const kctx *k, int level, long weight, long value) {
    if (weight > k->W) return -1;
    double b = (double)value;
    long cw = weight;
    for (int t = level; t < k->n; t++) {
        int i = k->ord[t];
        if (cw + k->w[i] <= k->W) { cw += k->w[i]; b += (double)k->v[i]; }
        else { b += (double)k->v[i] * (double)(k->W - cw) / (double)k->w[i]; break; }
    }
    return b;
}

static const kctx *g_k; /* comparator context for sorting by ratio */
static int cmp_ratio(const void *a, const void *b) {
    int i = *(const int *)a, j = *(const int *)b;
    /* v_i/w_i > v_j/w_j  <=>  v_i*w_j > v_j*w_i (w == 0 treated as infinite ratio) */
    double li = (double)g_k->v[i] * (double)(g_k->w[j] ? g_k->w[j] : 0);
    double lj = (double)g_k->v[j] * (double)(g_k->w[i] ? g_k->w[i] : 0);
    if (g_k->w[i] == 0 && g_k->w[j] != 0) return -1;
    if (g_k->w[j] == 0 && g_k->w[i] != 0) return 1;
    if (li > lj) return -1;
    if (li < lj) return 1;
    return i - j;
}

/* max-heap on bound for LC */
static void lc_push(int *h, int *sz, const bnode *pool, int x) {
    int i = (*sz)++;
    h[i] = x;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (pool[h[p]].bound >= pool[h[i]].bound) break;
        int t = h[p]; h[p] = h[i]; h[i] = t; i = p;
    }
}
static int lc_pop(int *h, int *sz, const bnode *pool) {
    int top = h[0];
    h[0] = h[--(*sz)];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < *sz && pool[h[l]].bound > pool[h[m]].bound) m = l;
        if (r < *sz && pool[h[r]].bound > pool[h[m]].bound) m = r;
        if (m == i) break;
        int t = h[m]; h[m] = h[i]; h[i] = t; i = m;
    }
    return top;
}

const char *ev_bnb_mode_name(ev_bnb_mode m) {
    switch (m) { case EV_BNB_FIFO: return "FIFO"; case EV_BNB_LIFO: return "LIFO"; default: return "LC"; }
}

long ev_knapsack_bnb(int n, const int *w, const long *v, int W, ev_bnb_mode mode,
                     long node_limit, int *take, ev_bnb_stats *st) {
    ev_bnb_stats s = { 0, 0, 0, 0, 1 };
    if (node_limit <= 0) node_limit = 4000000L;
    if (n < 0 || W < 0) { if (st) *st = s; return -1; }
    kctx k = { n, W, w, v, (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1)) };
    for (int i = 0; i < n; i++) k.ord[i] = i;
    g_k = &k;
    ev_merge_sort(k.ord, (size_t)n, sizeof(int), cmp_ratio);

    /* incumbent: greedy fill in ratio order, skipping items that do not fit */
    int *greedy = (int *)ev_calloc((size_t)(n ? n : 1), sizeof(int));
    long cw = 0, best = 0;
    for (int t = 0; t < n; t++) {
        int i = k.ord[t];
        if (cw + w[i] <= W) { cw += w[i]; best += v[i]; greedy[i] = 1; }
    }
    int best_node = -1;

    long cap = 1024;
    bnode *pool = (bnode *)ev_malloc(sizeof(bnode) * (size_t)cap);
    int *front = (int *)ev_malloc(sizeof(int) * (size_t)cap);
    long fhead = 0, ftail = 0; /* FIFO: [fhead, ftail); LIFO: stack in [0, ftail); LC: heap size ftail */
    long npool = 0;

#define NEWNODE(lv, par, wt, val, tk) do { \
        if (npool == cap) { cap *= 2; pool = (bnode *)ev_realloc(pool, sizeof(bnode) * (size_t)cap); \
                            front = (int *)ev_realloc(front, sizeof(int) * (size_t)cap); } \
        pool[npool].level = (lv); pool[npool].parent = (par); pool[npool].weight = (wt); \
        pool[npool].value = (val); pool[npool].took = (char)(tk); \
        pool[npool].bound = kbound(&k, (lv), (wt), (val)); npool++; s.generated++; } while (0)

    NEWNODE(0, -1, 0, 0, 0);
    front[ftail++] = 0;
    if (mode == EV_BNB_LC) { int hs = 0; ftail = 0; lc_push(front, &hs, pool, 0); ftail = hs; }

    while ((mode == EV_BNB_FIFO) ? (fhead < ftail) : (ftail > 0)) {
        long fsize = (mode == EV_BNB_FIFO) ? (ftail - fhead) : ftail;
        if (fsize > s.max_frontier) s.max_frontier = fsize;
        int x;
        if (mode == EV_BNB_FIFO) x = front[fhead++];
        else if (mode == EV_BNB_LIFO) x = front[--ftail];
        else { int hs = (int)ftail; x = lc_pop(front, &hs, pool); ftail = hs; }

        if (floor(pool[x].bound + 1e-9) <= (double)best) {
            if (mode == EV_BNB_LC) break; /* best-first: every remaining bound is <= this one */
            continue;
        }
        if (pool[x].level >= n) continue;
        if (s.generated + 2 > node_limit) { s.complete = 0; break; }
        s.expanded++;
        int lv = pool[x].level, item = k.ord[lv];
        long pw = pool[x].weight, pv = pool[x].value;

        int kids[2], nk = 0;
        /* include child */
        if (pw + w[item] <= W) {
            NEWNODE(lv + 1, x, pw + w[item], pv + v[item], 1);
            kids[nk++] = (int)(npool - 1);
        }
        /* exclude child */
        NEWNODE(lv + 1, x, pw, pv, 0);
        kids[nk++] = (int)(npool - 1);

        /* LIFO pops include first when we push exclude first */
        int order[2] = { 0, 1 };
        if (mode == EV_BNB_LIFO && nk == 2) { order[0] = 1; order[1] = 0; }
        for (int t = 0; t < nk; t++) {
            int c = kids[order[t]];
            if (pool[c].value > best) { best = pool[c].value; best_node = c; }
            if (pool[c].level < n && floor(pool[c].bound + 1e-9) > (double)best) {
                if (mode == EV_BNB_FIFO) {
                    if (ftail == cap) { /* compact */
                        memmove(front, front + fhead, sizeof(int) * (size_t)(ftail - fhead));
                        ftail -= fhead; fhead = 0;
                    }
                    front[ftail++] = c;
                } else if (mode == EV_BNB_LIFO) front[ftail++] = c;
                else { int hs = (int)ftail; lc_push(front, &hs, pool, c); ftail = hs; }
            }
        }
    }
#undef NEWNODE

    if (take) {
        if (best_node < 0) for (int i = 0; i < n; i++) take[i] = greedy[i];
        else {
            for (int i = 0; i < n; i++) take[i] = 0;
            for (int x = best_node; x > 0; x = pool[x].parent)
                if (pool[x].took) take[k.ord[pool[x].level - 1]] = 1;
        }
    }
    s.best = best;
    if (st) *st = s;
    ev_free(pool); ev_free(front); ev_free(greedy); ev_free(k.ord);
    return best;
}
