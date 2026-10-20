/* ev_flow.h - capacity allocation and assignment.
 *  Ford-Fulkerson with DFS augmenting paths  O(E * |f*|)
 *  Edmonds-Karp (BFS augmenting paths)       O(V E^2)
 *  Maximum bipartite matching (Kuhn / augmenting paths)  O(V E)
 */
#ifndef EV_FLOW_H
#define EV_FLOW_H

typedef struct ev_flow ev_flow;

typedef enum { EV_FLOW_DFS = 0, EV_FLOW_BFS = 1 } ev_flow_mode;

ev_flow *ev_flow_create(int n);
int      ev_flow_add_edge(ev_flow *f, int u, int v, long cap); /* returns edge id, -1 on error */
long     ev_flow_maxflow(ev_flow *f, int s, int t, ev_flow_mode mode, long *augmentations);
long     ev_flow_edge_flow(const ev_flow *f, int edge_id);
void     ev_flow_reset(ev_flow *f); /* zero all flows */
/* After maxflow: side[v] = 1 if v reachable from s in the residual graph (min-cut S side). */
void     ev_flow_min_cut(const ev_flow *f, int s, unsigned char *side);
void     ev_flow_free(ev_flow *f);

/* Bipartite graph: left vertex i has neighbours adj[head[i] .. head[i+1]-1] in [0, nr).
 * match_l[i] = matched right vertex or -1; match_r likewise. Returns matching size. */
int ev_bipartite_match(int nl, int nr, const int *head, const int *adj, int *match_l, int *match_r);

#endif
