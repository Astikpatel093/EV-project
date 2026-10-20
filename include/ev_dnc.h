/* ev_dnc.h - divide-and-conquer modules.
 *  - maximum subarray (worst grid-overload window)          T(n)=2T(n/2)+Θ(n)   = Θ(n log n)
 *  - fast powering (battery degradation, modular hashing)   T(e)=T(e/2)+Θ(1)    = Θ(log e)
 *  - celebrity discovery (overflow hub station)             T(n)=T(n-1)+Θ(1)    = Θ(n) queries
 *  - classical vs Strassen matrix multiplication            Θ(n^3) vs Θ(n^log2 7)≈Θ(n^2.807)
 */
#ifndef EV_DNC_H
#define EV_DNC_H

typedef struct { long lo, hi; double sum; } ev_subarray; /* inclusive [lo,hi] */

ev_subarray ev_max_subarray_dc(const double *a, long n);     /* n >= 1 */
ev_subarray ev_max_subarray_kadane(const double *a, long n); /* Θ(n) oracle */
ev_subarray ev_max_subarray_brute(const double *a, long n);  /* Θ(n^2) oracle */

double ev_pow_fast(double base, unsigned long long e);
double ev_pow_naive(double base, unsigned long long e);
/* (b^e) mod m with m < 2^32 so intermediate products fit in 64 bits. */
unsigned long long ev_powmod(unsigned long long b, unsigned long long e, unsigned long long m);

/* knows(ctx,a,b) != 0  <=>  a knows b. Returns celebrity index or -1. */
typedef int (*ev_knows_fn)(void *ctx, int a, int b);
int ev_celebrity_dc(int n, ev_knows_fn knows, void *ctx, long *queries);
int ev_celebrity_naive(int n, ev_knows_fn knows, void *ctx, long *queries);

/* Row-major n x n matrices; C must not alias A or B. */
void ev_matmul_classic(const double *A, const double *B, double *C, int n);
/* Strassen with padding to the next power of two; falls back to classic below `cutoff`. */
void ev_matmul_strassen(const double *A, const double *B, double *C, int n, int cutoff);

#endif
