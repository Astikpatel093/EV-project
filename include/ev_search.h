/* ev_search.h - backtracking and branch-and-bound.
 *  N-Queens (backtracking framework validation)
 *  Subset sum (spend the installation budget exactly)
 *  0/1 knapsack branch-and-bound with FIFO, LIFO and least-cost (best-bound) frontiers,
 *  used as the exact solver for the station-selection decision.
 */
#ifndef EV_SEARCH_H
#define EV_SEARCH_H

/* Count all solutions; first (may be NULL, size n) receives the first solution's columns. */
long ev_nqueens_count(int n, int *first, long *nodes);

/* Backtracking subset sum over non-negative a[]; take[i] in {0,1}.
 * Returns 1 if found, 0 if proven impossible, -1 if node_limit (>0) was exhausted. */
int ev_subset_sum(const int *a, int n, long target, long node_limit, int *take, long *nodes);
/* DP oracle (Θ(n*target) bits). Returns 1 if achievable, -1 if target too large. */
int ev_subset_sum_dp(const int *a, int n, long target);

typedef enum { EV_BNB_FIFO = 0, EV_BNB_LIFO = 1, EV_BNB_LC = 2 } ev_bnb_mode;

typedef struct {
    long best;            /* best value found */
    long expanded;        /* nodes taken off the frontier and branched */
    long generated;       /* nodes created */
    long max_frontier;    /* peak frontier size */
    int  complete;        /* 1 = search finished (optimal); 0 = node limit hit */
} ev_bnb_stats;

/* node_limit <= 0 means default (4,000,000). take[] may be NULL. */
long ev_knapsack_bnb(int n, const int *w, const long *v, int W, ev_bnb_mode mode,
                     long node_limit, int *take, ev_bnb_stats *st);
const char *ev_bnb_mode_name(ev_bnb_mode m);

#endif
