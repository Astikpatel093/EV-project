/* ev_mem.h - tracked allocation and timing helpers.
 *
 * Every library allocation goes through ev_malloc/ev_free so the benchmark
 * runner can report peak heap usage per algorithm without OS-specific tools.
 */
#ifndef EV_MEM_H
#define EV_MEM_H

#include <stddef.h>

void  *ev_malloc(size_t n);
void  *ev_calloc(size_t count, size_t size);
void  *ev_realloc(void *p, size_t n);
void   ev_free(void *p);

size_t ev_mem_current(void);   /* bytes currently live */
size_t ev_mem_peak(void);      /* high-water mark since last reset */
void   ev_mem_reset_peak(void);/* peak := current */

double ev_now_ms(void);        /* monotonic-enough wall clock in ms (C11 timespec_get) */

/* Deterministic PRNG (xorshift64*) so data, tests and benchmarks are reproducible. */
typedef struct { unsigned long long s; } ev_rng;
void               ev_rng_seed(ev_rng *r, unsigned long long seed);
unsigned long long ev_rng_next(ev_rng *r);
double             ev_rng_unit(ev_rng *r);              /* [0,1) */
long               ev_rng_range(ev_rng *r, long lo, long hi); /* [lo,hi] inclusive */

#endif
