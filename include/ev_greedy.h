/* ev_greedy.h - greedy algorithms: activity selection, interval partitioning, Huffman.
 * (Dijkstra, Prim and Kruskal live in ev_graph.h.)
 * Intervals are half-open [start, end): a session ending at 10:00 and one
 * starting at 10:00 can share a charger.
 */
#ifndef EV_GREEDY_H
#define EV_GREEDY_H

#include <stddef.h>

typedef struct { int id; double start, end; } ev_interval;

/* Max number of mutually compatible intervals (earliest-finish-first).
 * chosen[] receives indices into iv (caller allocates n). Θ(n log n). */
int ev_activity_select(const ev_interval *iv, int n, int *chosen);

/* Minimum resources (chargers) so no two overlapping intervals share one.
 * assign[i] = resource index of iv[i]. Θ(n log n) with a min-heap of end times. */
int ev_interval_partition(const ev_interval *iv, int n, int *assign);

/* Max depth (max number of intervals overlapping a point) - lower bound oracle. */
int ev_interval_depth(const ev_interval *iv, int n);

/* ---------------------------------------------------------------- Huffman */
typedef struct ev_huff ev_huff;

ev_huff    *ev_huff_build(const unsigned char *data, size_t n); /* NULL if n == 0 */
const char *ev_huff_code(const ev_huff *h, unsigned char sym); /* "" if symbol absent */
int         ev_huff_symbols(const ev_huff *h);
/* Encode: returns number of bits; *out is ev_malloc'd, caller ev_free()s. */
size_t      ev_huff_encode(const ev_huff *h, const unsigned char *data, size_t n, unsigned char **out);
/* Decode nbits; writes up to cap symbols; returns symbols written or (size_t)-1 on corrupt input. */
size_t      ev_huff_decode(const ev_huff *h, const unsigned char *bits, size_t nbits, unsigned char *out, size_t cap);
double      ev_huff_avg_bits(const ev_huff *h); /* weighted average code length */
void        ev_huff_free(ev_huff *h);

#endif
