/* ev_dp.h - dynamic programming modules.
 *  0/1 knapsack (station selection)          Θ(nW) time, Θ(nW) bits for reconstruction
 *  matrix-chain order (demand projections)    Θ(n^3)
 *  LCS (planned route vs GPS trace)           Θ(ab)
 *  optimal BST (station lookup index)         Θ(n^3)
 *  Held-Karp TSP (maintenance van tour)       Θ(n^2 2^n), n <= 20
 * Bellman-Ford and Floyd-Warshall are in ev_graph.h.
 */
#ifndef EV_DP_H
#define EV_DP_H

/* Returns optimal value; take[i] in {0,1} (may be NULL). Returns -1 on bad input. */
long ev_knapsack01(int n, const int *w, const long *v, int W, int *take);
/* Brute-force oracle for n <= 25. */
long ev_knapsack_brute(int n, const int *w, const long *v, int W, int *take);

/* dims has n+1 entries: matrix i is dims[i] x dims[i+1]. split is n*n (may be NULL).
 * Returns minimum scalar multiplications. */
long long ev_matrix_chain(int n, const long *dims, int *split);
/* Writes parenthesization like "((A1A2)A3)" into buf. */
void ev_matrix_chain_paren(int n, const int *split, char *buf, int cap);
/* Cost of multiplying strictly left-to-right (baseline). */
long long ev_matrix_chain_left(int n, const long *dims);
long long ev_matrix_chain_right(int n, const long *dims);

/* LCS of integer sequences; out (may be NULL, size >= min(na,nb)) receives one LCS. */
int ev_lcs(const int *a, int na, const int *b, int nb, int *out);

/* CLRS optimal BST. p[1..n] key probabilities, q[0..n] gap probabilities (arrays of n+1).
 * root is (n+2)*(n+2) (may be NULL). Returns expected search cost e[1][n]. */
double ev_optimal_bst(int n, const double *p, const double *q, int *root);
/* Expected cost of a balanced BST (median split) over the same keys/probabilities. */
double ev_balanced_bst_cost(int n, const double *p, const double *q);

/* D is n*n (asymmetric allowed). tour gets n+1 vertices starting/ending at 0.
 * Returns EV_INF-like (>=1e299) if no Hamiltonian cycle. n in [1, 20]. */
double ev_tsp_held_karp(int n, const double *D, int *tour);
double ev_tsp_brute(int n, const double *D, int *tour); /* n <= 11 oracle */

#endif
