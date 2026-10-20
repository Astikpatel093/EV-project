/* ev_graph.h - weighted directed graphs in CSR form and the graph algorithms:
 *   greedy : Dijkstra (binary heap), Prim (dense, O(V^2)), Kruskal (union-find)
 *   DP     : Bellman-Ford (early exit), Floyd-Warshall (with path reconstruction)
 * Arc k of a graph built from arrays (src,dst,w) keeps id = k, so two graphs
 * built from the same src/dst arrays have identical arc numbering.
 */
#ifndef EV_GRAPH_H
#define EV_GRAPH_H

#define EV_INF 1e300

typedef struct { int to; int id; double w; } ev_arc;

typedef struct {
    int n, m;
    int *head;    /* n+1 offsets into arcs */
    ev_arc *arcs; /* m arcs grouped by tail */
    int *tail;    /* tail[id] for each original arc id */
    int *pos;     /* pos[id] = index of arc id inside arcs[] */
} ev_graph;

ev_graph *ev_graph_build(int n, int m, const int *src, const int *dst, const double *w);
void      ev_graph_free(ev_graph *g);
/* Arc index (into arcs[]) of the minimum-weight arc u->v, or -1. */
int       ev_graph_find_arc(const ev_graph *g, int u, int v);

/* Single-source shortest paths; pred_arc[v] = original arc id entering v (-1 for none).
 * dist[v] = EV_INF when unreachable. pred_arc may be NULL. */
void ev_dijkstra(const ev_graph *g, int s, double *dist, int *pred_arc);
/* Returns 0 on success, -1 if a negative cycle is reachable from s.
 * passes (optional) receives the number of relaxation rounds executed. */
int  ev_bellman_ford(const ev_graph *g, int s, double *dist, int *pred_arc, int *passes);
/* D and next are n*n row-major. next[i*n+j] = vertex after i on a shortest i->j path, -1 if none.
 * Returns 0, or -1 if a negative cycle exists. */
int  ev_floyd_warshall(const ev_graph *g, double *D, int *next);
/* Reconstruct a path from Floyd next[]; returns vertex count written (0 if unreachable). */
int  ev_floyd_path(int n, const int *next, int a, int b, int *out, int cap);

/* MST on a dense symmetric weight matrix W (n*n, EV_INF = no edge).
 * parent[v] = MST parent (-1 for component roots). Returns total weight. */
double ev_prim_dense(int n, const double *W, int *parent);
/* Kruskal on an undirected edge list; chosen[] gets indices of chosen edges.
 * Returns total weight; *nchosen = number of edges taken (n - components). */
double ev_kruskal(int n, int m, const int *u, const int *v, const double *w, int *chosen, int *nchosen);

/* Union-find (path halving + union by rank). */
typedef struct { int *p, *r; int n; } ev_dsu;
void ev_dsu_init(ev_dsu *d, int n);
int  ev_dsu_find(ev_dsu *d, int x);
int  ev_dsu_union(ev_dsu *d, int a, int b); /* 1 if merged */
void ev_dsu_free(ev_dsu *d);

#endif
