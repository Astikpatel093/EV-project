#include "ev_graph.h"
#include "ev_mem.h"
#include "ev_sort.h"

#include <string.h>

ev_graph *ev_graph_build(int n, int m, const int *src, const int *dst, const double *w) {
    ev_graph *g = (ev_graph *)ev_calloc(1, sizeof(ev_graph));
    g->n = n; g->m = m;
    g->head = (int *)ev_calloc((size_t)n + 1, sizeof(int));
    g->arcs = (ev_arc *)ev_malloc(sizeof(ev_arc) * (size_t)(m ? m : 1));
    g->tail = (int *)ev_malloc(sizeof(int) * (size_t)(m ? m : 1));
    g->pos = (int *)ev_malloc(sizeof(int) * (size_t)(m ? m : 1));
    for (int k = 0; k < m; k++) g->head[src[k] + 1]++;
    for (int u = 0; u < n; u++) g->head[u + 1] += g->head[u];
    int *fill = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
    for (int u = 0; u < n; u++) fill[u] = g->head[u];
    for (int k = 0; k < m; k++) {
        int p = fill[src[k]]++;
        g->arcs[p].to = dst[k];
        g->arcs[p].id = k;
        g->arcs[p].w = w[k];
        g->tail[k] = src[k];
        g->pos[k] = p;
    }
    ev_free(fill);
    return g;
}

void ev_graph_free(ev_graph *g) {
    if (!g) return;
    ev_free(g->head); ev_free(g->arcs); ev_free(g->tail); ev_free(g->pos);
    ev_free(g);
}

int ev_graph_find_arc(const ev_graph *g, int u, int v) {
    int best = -1;
    for (int p = g->head[u]; p < g->head[u + 1]; p++)
        if (g->arcs[p].to == v && (best < 0 || g->arcs[p].w < g->arcs[best].w)) best = p;
    return best;
}

/* ------------------------------------------------------------ Dijkstra */
typedef struct { double d; int v; } hq;

static void hq_push(hq *h, int *sz, double d, int v) {
    int i = (*sz)++;
    h[i].d = d; h[i].v = v;
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (h[p].d <= h[i].d) break;
        hq t = h[p]; h[p] = h[i]; h[i] = t; i = p;
    }
}

static hq hq_pop(hq *h, int *sz) {
    hq top = h[0];
    h[0] = h[--(*sz)];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < *sz && h[l].d < h[m].d) m = l;
        if (r < *sz && h[r].d < h[m].d) m = r;
        if (m == i) break;
        hq t = h[m]; h[m] = h[i]; h[i] = t; i = m;
    }
    return top;
}

void ev_dijkstra(const ev_graph *g, int s, double *dist, int *pred_arc) {
    int n = g->n;
    for (int v = 0; v < n; v++) { dist[v] = EV_INF; if (pred_arc) pred_arc[v] = -1; }
    if (s < 0 || s >= n) return;
    hq *heap = (hq *)ev_malloc(sizeof(hq) * (size_t)(g->m + 2));
    unsigned char *done = (unsigned char *)ev_calloc((size_t)n, 1);
    int hs = 0;
    dist[s] = 0;
    hq_push(heap, &hs, 0, s);
    while (hs > 0) {
        hq t = hq_pop(heap, &hs);
        int u = t.v;
        if (done[u]) continue; /* lazy deletion */
        done[u] = 1;
        for (int p = g->head[u]; p < g->head[u + 1]; p++) {
            const ev_arc *a = &g->arcs[p];
            double nd = dist[u] + a->w;
            if (nd < dist[a->to]) {
                dist[a->to] = nd;
                if (pred_arc) pred_arc[a->to] = a->id;
                hq_push(heap, &hs, nd, a->to);
            }
        }
    }
    ev_free(done);
    ev_free(heap);
}

/* ------------------------------------------------------------ Bellman-Ford */
int ev_bellman_ford(const ev_graph *g, int s, double *dist, int *pred_arc, int *passes) {
    int n = g->n;
    for (int v = 0; v < n; v++) { dist[v] = EV_INF; if (pred_arc) pred_arc[v] = -1; }
    if (passes) *passes = 0;
    if (s < 0 || s >= n) return 0;
    dist[s] = 0;
    int round;
    for (round = 1; round <= n; round++) {
        int changed = 0;
        for (int u = 0; u < n; u++) {
            if (dist[u] >= EV_INF) continue;
            for (int p = g->head[u]; p < g->head[u + 1]; p++) {
                const ev_arc *a = &g->arcs[p];
                double nd = dist[u] + a->w;
                if (nd < dist[a->to] - 1e-12) {
                    dist[a->to] = nd;
                    if (pred_arc) pred_arc[a->to] = a->id;
                    changed = 1;
                }
            }
        }
        if (passes) *passes = round;
        if (!changed) return 0;
        if (round == n) return -1; /* still relaxing after n-1 rounds -> negative cycle */
    }
    return 0;
}

/* ------------------------------------------------------------ Floyd-Warshall */
int ev_floyd_warshall(const ev_graph *g, double *D, int *next) {
    int n = g->n;
    size_t nn = (size_t)n * n;
    for (size_t i = 0; i < nn; i++) { D[i] = EV_INF; next[i] = -1; }
    for (int i = 0; i < n; i++) { D[(size_t)i * n + i] = 0; next[(size_t)i * n + i] = i; }
    for (int u = 0; u < n; u++)
        for (int p = g->head[u]; p < g->head[u + 1]; p++) {
            const ev_arc *a = &g->arcs[p];
            size_t k = (size_t)u * n + a->to;
            if (a->w < D[k]) { D[k] = a->w; next[k] = a->to; }
        }
    for (int k = 0; k < n; k++) {
        const double *Dk = D + (size_t)k * n;
        for (int i = 0; i < n; i++) {
            double dik = D[(size_t)i * n + k];
            if (dik >= EV_INF) continue;
            double *Di = D + (size_t)i * n;
            int *Ni = next + (size_t)i * n;
            int nik = Ni[k];
            for (int j = 0; j < n; j++) {
                double nd = dik + Dk[j];
                if (nd < Di[j]) { Di[j] = nd; Ni[j] = nik; }
            }
        }
    }
    for (int i = 0; i < n; i++) if (D[(size_t)i * n + i] < 0) return -1;
    return 0;
}

int ev_floyd_path(int n, const int *next, int a, int b, int *out, int cap) {
    if (next[(size_t)a * n + b] < 0) return 0;
    int cnt = 0, u = a;
    if (cnt < cap) out[cnt++] = u;
    while (u != b) {
        u = next[(size_t)u * n + b];
        if (u < 0 || cnt >= cap) return 0;
        out[cnt++] = u;
    }
    return cnt;
}

/* ------------------------------------------------------------ Prim (dense) */
double ev_prim_dense(int n, const double *W, int *parent) {
    double *key = (double *)ev_malloc(sizeof(double) * (size_t)(n ? n : 1));
    unsigned char *in = (unsigned char *)ev_calloc((size_t)(n ? n : 1), 1);
    double total = 0;
    for (int v = 0; v < n; v++) { key[v] = EV_INF; parent[v] = -1; }
    for (int it = 0; it < n; it++) {
        int u = -1;
        for (int v = 0; v < n; v++)
            if (!in[v] && (u < 0 || key[v] < key[u])) u = v;
        if (key[u] >= EV_INF) key[u] = 0;   /* new component root */
        else total += key[u];
        in[u] = 1;
        for (int v = 0; v < n; v++) {
            double w = W[(size_t)u * n + v];
            if (!in[v] && v != u && w < key[v]) { key[v] = w; parent[v] = u; }
        }
    }
    ev_free(key);
    ev_free(in);
    return total;
}

/* ------------------------------------------------------------ union-find + Kruskal */
void ev_dsu_init(ev_dsu *d, int n) {
    d->n = n;
    d->p = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
    d->r = (int *)ev_calloc((size_t)(n ? n : 1), sizeof(int));
    for (int i = 0; i < n; i++) d->p[i] = i;
}

int ev_dsu_find(ev_dsu *d, int x) {
    while (d->p[x] != x) { d->p[x] = d->p[d->p[x]]; x = d->p[x]; }
    return x;
}

int ev_dsu_union(ev_dsu *d, int a, int b) {
    a = ev_dsu_find(d, a); b = ev_dsu_find(d, b);
    if (a == b) return 0;
    if (d->r[a] < d->r[b]) { int t = a; a = b; b = t; }
    d->p[b] = a;
    if (d->r[a] == d->r[b]) d->r[a]++;
    return 1;
}

void ev_dsu_free(ev_dsu *d) { ev_free(d->p); ev_free(d->r); d->p = d->r = NULL; }

typedef struct { double w; int i; } wi;
static int cmp_wi(const void *a, const void *b) {
    const wi *x = (const wi *)a, *y = (const wi *)b;
    if (x->w < y->w) return -1;
    if (x->w > y->w) return 1;
    return (x->i > y->i) - (x->i < y->i);
}

double ev_kruskal(int n, int m, const int *u, const int *v, const double *w, int *chosen, int *nchosen) {
    wi *ord = (wi *)ev_malloc(sizeof(wi) * (size_t)(m ? m : 1));
    for (int k = 0; k < m; k++) { ord[k].w = w[k]; ord[k].i = k; }
    ev_merge_sort(ord, (size_t)m, sizeof(wi), cmp_wi);
    ev_dsu d;
    ev_dsu_init(&d, n);
    double total = 0;
    int c = 0;
    for (int k = 0; k < m && c < n - 1; k++) {
        int e = ord[k].i;
        if (w[e] >= EV_INF) break;
        if (ev_dsu_union(&d, u[e], v[e])) { total += w[e]; if (chosen) chosen[c] = e; c++; }
    }
    if (nchosen) *nchosen = c;
    ev_dsu_free(&d);
    ev_free(ord);
    return total;
}
