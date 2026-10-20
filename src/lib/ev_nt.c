#include "ev_nt.h"
#include "ev_dnc.h"
#include "ev_mem.h"

#include <string.h>

/* ------------------------------------------------------------ number theory */
long long ev_gcd(long long a, long long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { long long t = a % b; a = b; b = t; }
    return a;
}

long long ev_ext_gcd(long long a, long long b, long long *x, long long *y) {
    if (b == 0) { *x = 1; *y = 0; return a; }
    long long x1, y1;
    long long g = ev_ext_gcd(b, a % b, &x1, &y1);
    *x = y1;
    *y = x1 - (a / b) * y1;
    return g;
}

long long ev_mulmod(long long a, long long b, long long m) {
    /* binary multiplication: no 128-bit types needed (portable to MSVC) */
    a %= m; if (a < 0) a += m;
    b %= m; if (b < 0) b += m;
    long long r = 0;
    while (b > 0) {
        if (b & 1) { r += a; if (r >= m) r -= m; }
        a += a; if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}

long long ev_modinv(long long a, long long m) {
    long long x, y;
    long long g = ev_ext_gcd(((a % m) + m) % m, m, &x, &y);
    if (g != 1) return -1;
    return ((x % m) + m) % m;
}

int ev_crt(int k, const long long *a, const long long *m, long long *x, long long *M) {
    if (k <= 0) return -1;
    long long cx = 0, cm = 1;
    for (int i = 0; i < k; i++) {
        if (m[i] <= 0) return -1;
        long long ai = ((a[i] % m[i]) + m[i]) % m[i];
        /* solve cx + cm*t = ai (mod m[i]) */
        long long p, q;
        long long g = ev_ext_gcd(cm, m[i], &p, &q);
        long long diff = ai - cx;
        if (diff % g != 0) return 0;
        long long mg = m[i] / g;
        if (cm > (1LL << 62) / mg) return -1;  /* lcm overflow */
        long long l = cm * mg;
        long long t = ev_mulmod((diff / g) % mg, ((p % mg) + mg) % mg, mg);
        cx = (cx + ev_mulmod(cm, t, l)) % l;
        cm = l;
    }
    if (x) *x = cx;
    if (M) *M = cm;
    return 1;
}

/* ------------------------------------------------------------ string matching */
static void st_init(ev_match_stats *st) {
    if (!st) return;
    st->matches = 0; st->comparisons = 0; st->preprocess = 0; st->first = -1;
}

static void record(long pos, long *cnt, long *out, long cap, ev_match_stats *st) {
    if (out && *cnt < cap) out[*cnt] = pos;
    if (st && st->first < 0) st->first = pos;
    (*cnt)++;
}

long ev_match_naive(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st) {
    st_init(st);
    long cnt = 0;
    long long cmp = 0;
    if (m <= 0 || m > n) { if (st) st->matches = 0; return 0; }
    for (long s = 0; s + m <= n; s++) {
        long j = 0;
        while (j < m) { cmp++; if (t[s + j] != p[j]) break; j++; }
        if (j == m) record(s, &cnt, out, cap, st);
    }
    if (st) { st->matches = cnt; st->comparisons = cmp; }
    return cnt;
}

long ev_match_rabin_karp(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st) {
    st_init(st);
    long cnt = 0;
    long long cmp = 0;
    if (m <= 0 || m > n) return 0;
    const unsigned long long Q = 1000000007ULL, D = 256ULL;
    unsigned long long h = ev_powmod(D, (unsigned long long)(m - 1), Q); /* D^(m-1) mod Q via fast power */
    unsigned long long hp = 0, ht = 0;
    for (long i = 0; i < m; i++) {
        hp = (hp * D + (unsigned char)p[i]) % Q;
        ht = (ht * D + (unsigned char)t[i]) % Q;
    }
    for (long s = 0;; s++) {
        if (hp == ht) {             /* verify to rule out spurious hits */
            long j = 0;
            while (j < m) { cmp++; if (t[s + j] != p[j]) break; j++; }
            if (j == m) record(s, &cnt, out, cap, st);
        }
        if (s + m >= n) break;
        /* roll: ht = ((ht - t[s]*D^(m-1)) * D + t[s+m]) mod Q ; all terms < Q*D < 2^40 */
        unsigned long long lead = ((unsigned char)t[s] * h) % Q;
        ht = ((ht + Q - lead) % Q * D + (unsigned char)t[s + m]) % Q;
    }
    if (st) { st->matches = cnt; st->comparisons = cmp; st->preprocess = 2 * m; }
    return cnt;
}

long ev_match_fa(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st) {
    st_init(st);
    long cnt = 0;
    if (m <= 0 || m > n) return 0;
    /* delta[q][a] built in O(m * |Sigma|) using the KMP failure function */
    int *delta = (int *)ev_malloc(sizeof(int) * (size_t)(m + 1) * 256);
    long long pre = 0;
    for (int a = 0; a < 256; a++) delta[a] = 0;
    delta[(unsigned char)p[0]] = 1;
    int x = 0; /* state reached by p[1..q-1] */
    for (long q = 1; q <= m; q++) {
        for (int a = 0; a < 256; a++) { delta[(size_t)q * 256 + a] = delta[(size_t)x * 256 + a]; pre++; }
        if (q < m) {
            delta[(size_t)q * 256 + (unsigned char)p[q]] = (int)q + 1;
            x = delta[(size_t)x * 256 + (unsigned char)p[q]];
        }
    }
    int state = 0;
    for (long i = 0; i < n; i++) {
        state = delta[(size_t)state * 256 + (unsigned char)t[i]];
        if (state == m) record(i - m + 1, &cnt, out, cap, st);
    }
    ev_free(delta);
    if (st) { st->matches = cnt; st->comparisons = n; st->preprocess = pre; }
    return cnt;
}

long ev_match_kmp(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st) {
    st_init(st);
    long cnt = 0;
    long long cmp = 0, pre = 0;
    if (m <= 0 || m > n) return 0;
    long *pi = (long *)ev_malloc(sizeof(long) * (size_t)m);
    pi[0] = 0;
    for (long q = 1, k = 0; q < m; q++) {
        while (k > 0 && p[k] != p[q]) { k = pi[k - 1]; pre++; }
        pre++;
        if (p[k] == p[q]) k++;
        pi[q] = k;
    }
    for (long i = 0, q = 0; i < n; i++) {
        while (q > 0 && p[q] != t[i]) { q = pi[q - 1]; cmp++; }
        cmp++;
        if (p[q] == t[i]) q++;
        if (q == m) { record(i - m + 1, &cnt, out, cap, st); q = pi[q - 1]; }
    }
    ev_free(pi);
    if (st) { st->matches = cnt; st->comparisons = cmp; st->preprocess = pre; }
    return cnt;
}
