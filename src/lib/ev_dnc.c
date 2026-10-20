#include "ev_dnc.h"
#include "ev_mem.h"

#include <string.h>

/* ------------------------------------------------------------ max subarray */
static ev_subarray cross(const double *a, long lo, long mid, long hi) {
    double s = 0, best_l = a[mid];
    long bl = mid;
    for (long i = mid; i >= lo; i--) { s += a[i]; if (s > best_l) { best_l = s; bl = i; } }
    s = 0;
    double best_r = a[mid + 1];
    long br = mid + 1;
    for (long j = mid + 1; j <= hi; j++) { s += a[j]; if (s > best_r) { best_r = s; br = j; } }
    ev_subarray r = { bl, br, best_l + best_r };
    return r;
}

static ev_subarray msa(const double *a, long lo, long hi) {
    if (lo == hi) { ev_subarray r = { lo, hi, a[lo] }; return r; }
    long mid = lo + (hi - lo) / 2;
    ev_subarray L = msa(a, lo, mid), R = msa(a, mid + 1, hi), C = cross(a, lo, mid, hi);
    if (L.sum >= R.sum && L.sum >= C.sum) return L;
    if (R.sum >= L.sum && R.sum >= C.sum) return R;
    return C;
}

ev_subarray ev_max_subarray_dc(const double *a, long n) {
    if (n <= 0) { ev_subarray r = { -1, -1, 0 }; return r; }
    return msa(a, 0, n - 1);
}

ev_subarray ev_max_subarray_kadane(const double *a, long n) {
    ev_subarray best = { -1, -1, 0 };
    if (n <= 0) return best;
    best.lo = best.hi = 0; best.sum = a[0];
    double cur = a[0];
    long start = 0;
    for (long i = 1; i < n; i++) {
        if (cur < 0) { cur = a[i]; start = i; } else cur += a[i];
        if (cur > best.sum) { best.sum = cur; best.lo = start; best.hi = i; }
    }
    return best;
}

ev_subarray ev_max_subarray_brute(const double *a, long n) {
    ev_subarray best = { -1, -1, 0 };
    if (n <= 0) return best;
    best.lo = best.hi = 0; best.sum = a[0];
    for (long i = 0; i < n; i++) {
        double s = 0;
        for (long j = i; j < n; j++) {
            s += a[j];
            if (s > best.sum) { best.sum = s; best.lo = i; best.hi = j; }
        }
    }
    return best;
}

/* ------------------------------------------------------------ powering */
double ev_pow_fast(double base, unsigned long long e) {
    if (e == 0) return 1.0;
    double half = ev_pow_fast(base, e / 2);   /* T(e) = T(e/2) + O(1) */
    return (e & 1ULL) ? half * half * base : half * half;
}

double ev_pow_naive(double base, unsigned long long e) {
    double r = 1.0;
    for (unsigned long long i = 0; i < e; i++) r *= base;
    return r;
}

unsigned long long ev_powmod(unsigned long long b, unsigned long long e, unsigned long long m) {
    if (m == 1) return 0;
    unsigned long long r = 1;
    b %= m;
    while (e) {
        if (e & 1ULL) r = (r * b) % m;
        b = (b * b) % m;
        e >>= 1;
    }
    return r;
}

/* ------------------------------------------------------------ celebrity */
static int celeb_rec(int n, ev_knows_fn knows, void *ctx, long *q) {
    /* candidate among people 0..n-1 ; T(n) = T(n-1) + 1 query */
    if (n == 1) return 0;
    int c = celeb_rec(n - 1, knows, ctx, q);
    (*q)++;
    if (knows(ctx, c, n - 1)) return n - 1; /* c knows someone: c is out */
    return c;                               /* n-1 is not known by c: n-1 is out */
}

int ev_celebrity_dc(int n, ev_knows_fn knows, void *ctx, long *queries) {
    long q = 0;
    if (n <= 0) { if (queries) *queries = 0; return -1; }
    /* Recursion depth n: for very large n use the iterative elimination (same recurrence). */
    int c;
    if (n <= 20000) c = celeb_rec(n, knows, ctx, &q);
    else {
        c = 0;
        for (int i = 1; i < n; i++) { q++; if (knows(ctx, c, i)) c = i; }
    }
    /* verification pass: 2(n-1) queries */
    for (int i = 0; i < n; i++) {
        if (i == c) continue;
        q += 2;
        if (knows(ctx, c, i) || !knows(ctx, i, c)) { if (queries) *queries = q; return -1; }
    }
    if (queries) *queries = q;
    return c;
}

int ev_celebrity_naive(int n, ev_knows_fn knows, void *ctx, long *queries) {
    long q = 0;
    int result = -1;
    for (int c = 0; c < n && result < 0; c++) {
        int ok = 1;
        for (int i = 0; i < n && ok; i++) {
            if (i == c) continue;
            q += 2;
            if (knows(ctx, c, i) || !knows(ctx, i, c)) ok = 0;
        }
        if (ok) result = c;
    }
    if (queries) *queries = q;
    return result;
}

/* ------------------------------------------------------------ matrices */
void ev_matmul_classic(const double *A, const double *B, double *C, int n) {
    memset(C, 0, sizeof(double) * (size_t)n * (size_t)n);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) {
            double a = A[(size_t)i * n + k];
            if (a == 0.0) continue;
            const double *brow = B + (size_t)k * n;
            double *crow = C + (size_t)i * n;
            for (int j = 0; j < n; j++) crow[j] += a * brow[j];
        }
}

/* sub-matrix views via stride */
static void add_m(const double *X, int sx, const double *Y, int sy, double *Z, int sz, int n, double sign) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) Z[(size_t)i * sz + j] = X[(size_t)i * sx + j] + sign * Y[(size_t)i * sy + j];
}

static void copy_m(const double *X, int sx, double *Z, int sz, int n) {
    for (int i = 0; i < n; i++) memcpy(Z + (size_t)i * sz, X + (size_t)i * sx, sizeof(double) * (size_t)n);
}

static void classic_strided(const double *A, int sa, const double *B, int sb, double *C, int sc, int n) {
    for (int i = 0; i < n; i++) {
        double *crow = C + (size_t)i * sc;
        for (int j = 0; j < n; j++) crow[j] = 0.0;
        for (int k = 0; k < n; k++) {
            double a = A[(size_t)i * sa + k];
            const double *brow = B + (size_t)k * sb;
            for (int j = 0; j < n; j++) crow[j] += a * brow[j];
        }
    }
}

/* C = A*B, all n x n (n power of two), contiguous with stride n for temporaries */
static void strassen_rec(const double *A, int sa, const double *B, int sb, double *C, int sc, int n, int cutoff) {
    if (n <= cutoff) { classic_strided(A, sa, B, sb, C, sc, n); return; }
    int h = n / 2;
    size_t hh = (size_t)h * h;
    const double *A11 = A, *A12 = A + h, *A21 = A + (size_t)h * sa, *A22 = A21 + h;
    const double *B11 = B, *B12 = B + h, *B21 = B + (size_t)h * sb, *B22 = B21 + h;
    double *buf = (double *)ev_malloc(sizeof(double) * hh * 9);
    double *T1 = buf, *T2 = buf + hh;
    double *M[7];
    for (int i = 0; i < 7; i++) M[i] = buf + hh * (2 + i);

    add_m(A11, sa, A22, sa, T1, h, h, +1); add_m(B11, sb, B22, sb, T2, h, h, +1);
    strassen_rec(T1, h, T2, h, M[0], h, h, cutoff);                 /* M1=(A11+A22)(B11+B22) */
    add_m(A21, sa, A22, sa, T1, h, h, +1);
    strassen_rec(T1, h, B11, sb, M[1], h, h, cutoff);               /* M2=(A21+A22)B11 */
    add_m(B12, sb, B22, sb, T2, h, h, -1);
    strassen_rec(A11, sa, T2, h, M[2], h, h, cutoff);               /* M3=A11(B12-B22) */
    add_m(B21, sb, B11, sb, T2, h, h, -1);
    strassen_rec(A22, sa, T2, h, M[3], h, h, cutoff);               /* M4=A22(B21-B11) */
    add_m(A11, sa, A12, sa, T1, h, h, +1);
    strassen_rec(T1, h, B22, sb, M[4], h, h, cutoff);               /* M5=(A11+A12)B22 */
    add_m(A21, sa, A11, sa, T1, h, h, -1); add_m(B11, sb, B12, sb, T2, h, h, +1);
    strassen_rec(T1, h, T2, h, M[5], h, h, cutoff);                 /* M6=(A21-A11)(B11+B12) */
    add_m(A12, sa, A22, sa, T1, h, h, -1); add_m(B21, sb, B22, sb, T2, h, h, +1);
    strassen_rec(T1, h, T2, h, M[6], h, h, cutoff);                 /* M7=(A12-A22)(B21+B22) */

    double *C11 = C, *C12 = C + h, *C21 = C + (size_t)h * sc, *C22 = C21 + h;
    for (int i = 0; i < h; i++)
        for (int j = 0; j < h; j++) {
            size_t k = (size_t)i * h + j;
            C11[(size_t)i * sc + j] = M[0][k] + M[3][k] - M[4][k] + M[6][k];
            C12[(size_t)i * sc + j] = M[2][k] + M[4][k];
            C21[(size_t)i * sc + j] = M[1][k] + M[3][k];
            C22[(size_t)i * sc + j] = M[0][k] - M[1][k] + M[2][k] + M[5][k];
        }
    ev_free(buf);
}

void ev_matmul_strassen(const double *A, const double *B, double *C, int n, int cutoff) {
    if (n <= 0) return;
    if (cutoff < 1) cutoff = 1;
    int p = 1;
    while (p < n) p <<= 1;
    if (p == n) { strassen_rec(A, n, B, n, C, n, n, cutoff); return; }
    size_t pp = (size_t)p * p;
    double *Ap = (double *)ev_calloc(pp, sizeof(double));
    double *Bp = (double *)ev_calloc(pp, sizeof(double));
    double *Cp = (double *)ev_malloc(pp * sizeof(double));
    copy_m(A, n, Ap, p, n);
    copy_m(B, n, Bp, p, n);
    strassen_rec(Ap, p, Bp, p, Cp, p, p, cutoff);
    copy_m(Cp, p, C, n, n);
    ev_free(Ap); ev_free(Bp); ev_free(Cp);
}
