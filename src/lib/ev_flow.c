#include "ev_flow.h"
#include "ev_mem.h"

#include <string.h>

struct ev_flow {
    int n, m, cap_m;
    int *head;   /* first edge out of vertex (linked list) */
    int *to, *next;
    long *cap, *flow;
};

ev_flow *ev_flow_create(int n) {
    ev_flow *f = (ev_flow *)ev_calloc(1, sizeof(ev_flow));
    f->n = n;
    f->head = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) f->head[i] = -1;
    f->cap_m = 64;
    f->to = (int *)ev_malloc(sizeof(int) * (size_t)f->cap_m);
    f->next = (int *)ev_malloc(sizeof(int) * (size_t)f->cap_m);
    f->cap = (long *)ev_malloc(sizeof(long) * (size_t)f->cap_m);
    f->flow = (long *)ev_malloc(sizeof(long) * (size_t)f->cap_m);
    return f;
}

static void push_arc(ev_flow *f, int u, int v, long c) {
    if (f->m == f->cap_m) {
        f->cap_m *= 2;
        f->to = (int *)ev_realloc(f->to, sizeof(int) * (size_t)f->cap_m);
        f->next = (int *)ev_realloc(f->next, sizeof(int) * (size_t)f->cap_m);
        f->cap = (long *)ev_realloc(f->cap, sizeof(long) * (size_t)f->cap_m);
        f->flow = (long *)ev_realloc(f->flow, sizeof(long) * (size_t)f->cap_m);
    }
    f->to[f->m] = v; f->cap[f->m] = c; f->flow[f->m] = 0;
    f->next[f->m] = f->head[u]; f->head[u] = f->m;
    f->m++;
}

int ev_flow_add_edge(ev_flow *f, int u, int v, long cap) {
    if (u < 0 || v < 0 || u >= f->n || v >= f->n || cap < 0) return -1;
    int id = f->m;
    push_arc(f, u, v, cap);  /* even id: forward */
    push_arc(f, v, u, 0);    /* odd id : residual */
    return id;
}

long ev_flow_edge_flow(const ev_flow *f, int e) { return (e >= 0 && e < f->m) ? f->flow[e] : 0; }

void ev_flow_reset(ev_flow *f) { for (int e = 0; e < f->m; e++) f->flow[e] = 0; }

static long resid(const ev_flow *f, int e) { return f->cap[e] - f->flow[e]; }

/* Iterative DFS for one augmenting path; returns bottleneck pushed (0 if none). */
static long dfs_augment(ev_flow *f, int s, int t, int *stamp, int mark, int *pe, int *stack_v, int *it) {
    int sp = 0;
    stack_v[sp++] = s;
    stamp[s] = mark;
    it[s] = f->head[s];
    while (sp > 0) {
        int u = stack_v[sp - 1];
        if (u == t) break;
        int advanced = 0;
        while (it[u] != -1) {
            int e = it[u];
            it[u] = f->next[e];
            int v = f->to[e];
            if (resid(f, e) > 0 && stamp[v] != mark) {
                stamp[v] = mark;
                pe[v] = e;
                it[v] = f->head[v];
                stack_v[sp++] = v;
                advanced = 1;
                break;
            }
        }
        if (!advanced) sp--;
    }
    if (sp == 0) return 0;
    long b = -1;
    for (int v = t; v != s; v = f->to[pe[v] ^ 1]) { long r = resid(f, pe[v]); if (b < 0 || r < b) b = r; }
    for (int v = t; v != s; v = f->to[pe[v] ^ 1]) { f->flow[pe[v]] += b; f->flow[pe[v] ^ 1] -= b; }
    return b;
}

static long bfs_augment(ev_flow *f, int s, int t, int *stamp, int mark, int *pe, int *queue) {
    int qh = 0, qt = 0;
    queue[qt++] = s;
    stamp[s] = mark;
    while (qh < qt && stamp[t] != mark) {
        int u = queue[qh++];
        for (int e = f->head[u]; e != -1; e = f->next[e]) {
            int v = f->to[e];
            if (resid(f, e) > 0 && stamp[v] != mark) { stamp[v] = mark; pe[v] = e; queue[qt++] = v; }
        }
    }
    if (stamp[t] != mark) return 0;
    long b = -1;
    for (int v = t; v != s; v = f->to[pe[v] ^ 1]) { long r = resid(f, pe[v]); if (b < 0 || r < b) b = r; }
    for (int v = t; v != s; v = f->to[pe[v] ^ 1]) { f->flow[pe[v]] += b; f->flow[pe[v] ^ 1] -= b; }
    return b;
}

long ev_flow_maxflow(ev_flow *f, int s, int t, ev_flow_mode mode, long *augmentations) {
    if (augmentations) *augmentations = 0;
    if (s < 0 || t < 0 || s >= f->n || t >= f->n || s == t) return 0;
    int n = f->n;
    int *stamp = (int *)ev_calloc((size_t)n, sizeof(int));
    int *pe = (int *)ev_malloc(sizeof(int) * (size_t)n);
    int *buf = (int *)ev_malloc(sizeof(int) * (size_t)n);
    int *it = (int *)ev_malloc(sizeof(int) * (size_t)n);
    long total = 0, aug = 0;
    for (int mark = 1;; mark++) {
        long b = (mode == EV_FLOW_DFS) ? dfs_augment(f, s, t, stamp, mark, pe, buf, it)
                                       : bfs_augment(f, s, t, stamp, mark, pe, buf);
        if (b <= 0) break;
        total += b;
        aug++;
    }
    if (augmentations) *augmentations = aug;
    ev_free(stamp); ev_free(pe); ev_free(buf); ev_free(it);
    return total;
}

void ev_flow_min_cut(const ev_flow *f, int s, unsigned char *side) {
    memset(side, 0, (size_t)f->n);
    int *q = (int *)ev_malloc(sizeof(int) * (size_t)f->n);
    int qh = 0, qt = 0;
    q[qt++] = s; side[s] = 1;
    while (qh < qt) {
        int u = q[qh++];
        for (int e = f->head[u]; e != -1; e = f->next[e])
            if (resid(f, e) > 0 && !side[f->to[e]]) { side[f->to[e]] = 1; q[qt++] = f->to[e]; }
    }
    ev_free(q);
}

void ev_flow_free(ev_flow *f) {
    if (!f) return;
    ev_free(f->head); ev_free(f->to); ev_free(f->next); ev_free(f->cap); ev_free(f->flow);
    ev_free(f);
}

/* ------------------------------------------------------------ bipartite matching (Kuhn) */
static int kuhn_try(int u, const int *head, const int *adj, int *match_r, int *match_l, int *seen, int mark) {
    for (int k = head[u]; k < head[u + 1]; k++) {
        int v = adj[k];
        if (seen[v] == mark) continue;
        seen[v] = mark;
        if (match_r[v] < 0 || kuhn_try(match_r[v], head, adj, match_r, match_l, seen, mark)) {
            match_r[v] = u;
            match_l[u] = v;
            return 1;
        }
    }
    return 0;
}

int ev_bipartite_match(int nl, int nr, const int *head, const int *adj, int *match_l, int *match_r) {
    for (int i = 0; i < nl; i++) match_l[i] = -1;
    for (int j = 0; j < nr; j++) match_r[j] = -1;
    int *seen = (int *)ev_calloc((size_t)(nr ? nr : 1), sizeof(int));
    int size = 0;
    /* greedy warm start: cheap and does not affect optimality */
    for (int u = 0; u < nl; u++)
        for (int k = head[u]; k < head[u + 1]; k++)
            if (match_r[adj[k]] < 0) { match_r[adj[k]] = u; match_l[u] = adj[k]; size++; break; }
    for (int u = 0; u < nl; u++) {
        if (match_l[u] >= 0) continue;
        if (kuhn_try(u, head, adj, match_r, match_l, seen, u + 1)) size++;
    }
    ev_free(seen);
    return size;
}
