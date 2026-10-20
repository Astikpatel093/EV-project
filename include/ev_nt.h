/* ev_nt.h - number theory and string matching.
 *  Chinese Remainder Theorem (generalized, non-coprime moduli) for aligning
 *  periodic maintenance windows.
 *  Pattern matching on station event logs: naive, Rabin-Karp, finite automaton, KMP.
 */
#ifndef EV_NT_H
#define EV_NT_H

long long ev_gcd(long long a, long long b);
long long ev_ext_gcd(long long a, long long b, long long *x, long long *y); /* returns g = ax+by */
long long ev_mulmod(long long a, long long b, long long m);                /* overflow-safe */
long long ev_modinv(long long a, long long m);                              /* -1 if none */

/* Solve x = a[i] (mod m[i]) for i < k. Returns 1 and sets x in [0, M), M = lcm(m);
 * returns 0 if inconsistent, -1 on invalid input or if lcm overflows 2^62. */
int ev_crt(int k, const long long *a, const long long *m, long long *x, long long *M);

typedef struct {
    long matches;            /* total occurrences */
    long long comparisons;   /* character comparisons (or table steps for FA) */
    long long preprocess;    /* preprocessing work units */
    long first;              /* first match position or -1 */
} ev_match_stats;

/* Each returns the number of matches; positions stored in out[0..cap) (out may be NULL). */
long ev_match_naive(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st);
long ev_match_rabin_karp(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st);
long ev_match_fa(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st);
long ev_match_kmp(const char *t, long n, const char *p, long m, long *out, long cap, ev_match_stats *st);

#endif
