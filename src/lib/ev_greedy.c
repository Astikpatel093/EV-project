#include "ev_greedy.h"
#include "ev_mem.h"
#include "ev_sort.h"

#include <string.h>

/* ------------------------------------------------------------ helpers */
typedef struct { double key; int idx; } kv;

static int cmp_kv(const void *a, const void *b) {
    const kv *x = (const kv *)a, *y = (const kv *)b;
    if (x->key < y->key) return -1;
    if (x->key > y->key) return 1;
    return (x->idx > y->idx) - (x->idx < y->idx);
}

/* ------------------------------------------------------------ activity selection */
int ev_activity_select(const ev_interval *iv, int n, int *chosen) {
    if (n <= 0) return 0;
    kv *ord = (kv *)ev_malloc(sizeof(kv) * (size_t)n);
    for (int i = 0; i < n; i++) { ord[i].key = iv[i].end; ord[i].idx = i; }
    ev_merge_sort(ord, (size_t)n, sizeof(kv), cmp_kv);
    int cnt = 0;
    double last_end = -1e300;
    for (int k = 0; k < n; k++) {
        int i = ord[k].idx;
        if (iv[i].start >= last_end) { chosen[cnt++] = i; last_end = iv[i].end; }
    }
    ev_free(ord);
    return cnt;
}

/* ------------------------------------------------------------ interval partitioning */
typedef struct { double end; int res; } hnode;

static void heap_push(hnode *h, int *sz, hnode v) {
    int i = (*sz)++;
    h[i] = v;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p].end < h[i].end || (h[p].end == h[i].end && h[p].res <= h[i].res)) break;
        hnode t = h[p]; h[p] = h[i]; h[i] = t; i = p;
    }
}

static hnode heap_pop(hnode *h, int *sz) {
    hnode top = h[0];
    h[0] = h[--(*sz)];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < *sz && (h[l].end < h[m].end || (h[l].end == h[m].end && h[l].res < h[m].res))) m = l;
        if (r < *sz && (h[r].end < h[m].end || (h[r].end == h[m].end && h[r].res < h[m].res))) m = r;
        if (m == i) break;
        hnode t = h[m]; h[m] = h[i]; h[i] = t; i = m;
    }
    return top;
}

int ev_interval_partition(const ev_interval *iv, int n, int *assign) {
    if (n <= 0) return 0;
    kv *ord = (kv *)ev_malloc(sizeof(kv) * (size_t)n);
    for (int i = 0; i < n; i++) { ord[i].key = iv[i].start; ord[i].idx = i; }
    ev_merge_sort(ord, (size_t)n, sizeof(kv), cmp_kv);
    hnode *heap = (hnode *)ev_malloc(sizeof(hnode) * (size_t)n);
    int hs = 0, used = 0;
    for (int k = 0; k < n; k++) {
        int i = ord[k].idx;
        int res;
        if (hs > 0 && heap[0].end <= iv[i].start) res = heap_pop(heap, &hs).res;
        else res = used++;
        assign[i] = res;
        hnode v = { iv[i].end, res };
        heap_push(heap, &hs, v);
    }
    ev_free(heap);
    ev_free(ord);
    return used;
}

int ev_interval_depth(const ev_interval *iv, int n) {
    if (n <= 0) return 0;
    kv *ev = (kv *)ev_malloc(sizeof(kv) * (size_t)n * 2);
    for (int i = 0; i < n; i++) {
        ev[2 * i].key = iv[i].start;   ev[2 * i].idx = 1;   /* +1 sorts after -1 at equal key */
        ev[2 * i + 1].key = iv[i].end; ev[2 * i + 1].idx = -1;
    }
    ev_merge_sort(ev, (size_t)n * 2, sizeof(kv), cmp_kv);
    int cur = 0, best = 0;
    for (int i = 0; i < 2 * n; i++) { cur += ev[i].idx; if (cur > best) best = cur; }
    ev_free(ev);
    return best;
}

/* ------------------------------------------------------------ Huffman */
struct ev_huff {
    int nnodes, root, nsym;
    int left[511], right[511], sym[511];
    unsigned long long freq[511];
    char *code[256];
    unsigned long long total;
};

static int hnode_less(const ev_huff *h, int a, int b) {
    if (h->freq[a] != h->freq[b]) return h->freq[a] < h->freq[b];
    return a < b; /* deterministic tie-break */
}

static void hh_push(const ev_huff *h, int *heap, int *sz, int v) {
    int i = (*sz)++;
    heap[i] = v;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!hnode_less(h, heap[i], heap[p])) break;
        int t = heap[p]; heap[p] = heap[i]; heap[i] = t; i = p;
    }
}

static int hh_pop(const ev_huff *h, int *heap, int *sz) {
    int top = heap[0];
    heap[0] = heap[--(*sz)];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < *sz && hnode_less(h, heap[l], heap[m])) m = l;
        if (r < *sz && hnode_less(h, heap[r], heap[m])) m = r;
        if (m == i) break;
        int t = heap[m]; heap[m] = heap[i]; heap[i] = t; i = m;
    }
    return top;
}

static void assign_codes(ev_huff *h, int node, char *buf, int depth) {
    if (h->left[node] < 0) {
        buf[depth] = '\0';
        if (depth == 0) { buf[0] = '0'; buf[1] = '\0'; depth = 1; } /* single-symbol alphabet */
        h->code[h->sym[node]] = (char *)ev_malloc((size_t)depth + 1);
        memcpy(h->code[h->sym[node]], buf, (size_t)depth + 1);
        return;
    }
    buf[depth] = '0'; assign_codes(h, h->left[node], buf, depth + 1);
    buf[depth] = '1'; assign_codes(h, h->right[node], buf, depth + 1);
}

ev_huff *ev_huff_build(const unsigned char *data, size_t n) {
    if (n == 0) return NULL;
    ev_huff *h = (ev_huff *)ev_calloc(1, sizeof(ev_huff));
    unsigned long long f[256] = { 0 };
    for (size_t i = 0; i < n; i++) f[data[i]]++;
    int heap[511], hs = 0;
    for (int s = 0; s < 256; s++) {
        if (!f[s]) continue;
        int id = h->nnodes++;
        h->left[id] = h->right[id] = -1;
        h->sym[id] = s;
        h->freq[id] = f[s];
        hh_push(h, heap, &hs, id);
    }
    h->nsym = h->nnodes;
    h->total = n;
    while (hs > 1) {
        int a = hh_pop(h, heap, &hs), b = hh_pop(h, heap, &hs);
        int id = h->nnodes++;
        h->left[id] = a; h->right[id] = b; h->sym[id] = -1;
        h->freq[id] = h->freq[a] + h->freq[b];
        hh_push(h, heap, &hs, id);
    }
    h->root = heap[0];
    char buf[260];
    assign_codes(h, h->root, buf, 0);
    return h;
}

const char *ev_huff_code(const ev_huff *h, unsigned char sym) {
    return (h && h->code[sym]) ? h->code[sym] : "";
}

int ev_huff_symbols(const ev_huff *h) { return h ? h->nsym : 0; }

size_t ev_huff_encode(const ev_huff *h, const unsigned char *data, size_t n, unsigned char **out) {
    size_t bits = 0;
    for (size_t i = 0; i < n; i++) bits += strlen(ev_huff_code(h, data[i]));
    unsigned char *buf = (unsigned char *)ev_calloc(bits / 8 + 1, 1);
    size_t pos = 0;
    for (size_t i = 0; i < n; i++)
        for (const char *c = ev_huff_code(h, data[i]); *c; c++, pos++)
            if (*c == '1') buf[pos >> 3] |= (unsigned char)(1u << (pos & 7));
    *out = buf;
    return bits;
}

size_t ev_huff_decode(const ev_huff *h, const unsigned char *bits, size_t nbits, unsigned char *out, size_t cap) {
    if (!h) return 0;
    size_t cnt = 0;
    if (h->nsym == 1) {
        for (size_t i = 0; i < nbits && cnt < cap; i++) out[cnt++] = (unsigned char)h->sym[h->root];
        return cnt;
    }
    int node = h->root;
    for (size_t i = 0; i < nbits; i++) {
        int b = (bits[i >> 3] >> (i & 7)) & 1;
        node = b ? h->right[node] : h->left[node];
        if (node < 0) return (size_t)-1;
        if (h->left[node] < 0) {
            if (cnt >= cap) return cnt;
            out[cnt++] = (unsigned char)h->sym[node];
            node = h->root;
        }
    }
    if (node != h->root) return (size_t)-1; /* trailing partial code */
    return cnt;
}

double ev_huff_avg_bits(const ev_huff *h) {
    if (!h || !h->total) return 0;
    double s = 0;
    for (int i = 0; i < h->nsym; i++) s += (double)h->freq[i] * (double)strlen(h->code[h->sym[i]]);
    return s / (double)h->total;
}

void ev_huff_free(ev_huff *h) {
    if (!h) return;
    for (int s = 0; s < 256; s++) ev_free(h->code[s]);
    ev_free(h);
}
