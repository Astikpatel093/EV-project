/* ev_plan.c - selection, routing, trip planning, capacity and session analysis. */
#include "ev_plan.h"
#include "ev_dnc.h"
#include "ev_dp.h"
#include "ev_flow.h"
#include "ev_greedy.h"
#include "ev_mem.h"
#include "ev_search.h"
#include "ev_sort.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *SEL_NAMES[EV_SEL_COUNT] = { "greedy", "dp", "bnb-lc", "bnb-fifo", "bnb-lifo" };
static const char *ROUTE_NAMES[EV_ROUTE_COUNT] = { "dijkstra", "floyd", "bellman" };

const char *ev_sel_name(ev_sel_mode m) { return (m >= 0 && m < EV_SEL_COUNT) ? SEL_NAMES[m] : "?"; }
const char *ev_route_name(ev_route_mode m) { return (m >= 0 && m < EV_ROUTE_COUNT) ? ROUTE_NAMES[m] : "?"; }
int ev_sel_parse(const char *s) { for (int i = 0; i < EV_SEL_COUNT; i++) if (!strcmp(s, SEL_NAMES[i])) return i; return -1; }
int ev_route_parse(const char *s) { for (int i = 0; i < EV_ROUTE_COUNT; i++) if (!strcmp(s, ROUTE_NAMES[i])) return i; return -1; }

/* ======================================================================== router */
struct ev_router {
    const ev_scenario *sc;
    ev_route_mode mode;
    int n;
    double **dist;  /* tree cache per source (Dijkstra / Bellman-Ford) */
    int **pa;
    double *D;      /* Floyd-Warshall all pairs */
    int *next;
    long runs;
    size_t bytes;
    int S;
    int *st_idx, *st_node;
    double *leg_km, *leg_kwh;
    unsigned char *leg_ok;
};

static void ensure_tree(ev_router *r, int a) {
    if (r->dist[a]) return;
    int n = r->n;
    r->dist[a] = (double *)ev_malloc(sizeof(double) * (size_t)n);
    r->pa[a] = (int *)ev_malloc(sizeof(int) * (size_t)n);
    r->bytes += (sizeof(double) + sizeof(int)) * (size_t)n;
    r->runs++;
    if (r->mode == EV_ROUTE_BELLMAN) {
        if (ev_bellman_ford(r->sc->g_kwh, a, r->dist[a], r->pa[a], NULL) < 0) {
            /* impossible with regen_eff <= 1 (every cycle has non-negative energy); be safe */
            for (int v = 0; v < n; v++) { r->dist[a][v] = EV_INF; r->pa[a][v] = -1; }
            r->dist[a][a] = 0;
        }
    } else {
        ev_dijkstra(r->sc->g_km, a, r->dist[a], r->pa[a]);
    }
}

static int leg_raw(ev_router *r, int a, int b, double *km, double *kwh) {
    const ev_scenario *sc = r->sc;
    double K = 0, E = 0;
    if (a == b) { *km = 0; *kwh = 0; return 1; }
    if (r->mode == EV_ROUTE_FLOYD) {
        int n = r->n;
        if (r->next[(size_t)a * n + b] < 0) return 0;
        int u = a, guard = 0;
        while (u != b && guard++ <= n) {
            int v = r->next[(size_t)u * n + b];
            int p = ev_graph_find_arc(sc->g_km, u, v);
            if (p < 0) return 0;
            int id = sc->g_km->arcs[p].id;
            K += sc->arc_km[id];
            E += sc->arc_kwh[id];
            u = v;
        }
        if (u != b) return 0;
    } else {
        ensure_tree(r, a);
        if (r->dist[a][b] >= EV_INF) return 0;
        int v = b, guard = 0;
        while (v != a && guard++ <= r->n) {
            int k = r->pa[a][v];
            if (k < 0) return 0;
            K += sc->arc_km[k];
            E += sc->arc_kwh[k];
            v = sc->arc_src[k];
        }
        if (v != a) return 0;
    }
    *km = K;
    *kwh = E;
    return 1;
}

ev_router *ev_router_create(const ev_scenario *sc, ev_route_mode mode, const int *station_idx, int ns) {
    ev_router *r = (ev_router *)ev_calloc(1, sizeof(ev_router));
    r->sc = sc;
    r->mode = mode;
    r->n = sc->nn;
    if (mode == EV_ROUTE_FLOYD) {
        size_t nn = (size_t)r->n * r->n;
        r->D = (double *)ev_malloc(sizeof(double) * nn);
        r->next = (int *)ev_malloc(sizeof(int) * nn);
        r->bytes += (sizeof(double) + sizeof(int)) * nn;
        ev_floyd_warshall(sc->g_km, r->D, r->next);
        r->runs = r->n;
    } else {
        r->dist = (double **)ev_calloc((size_t)r->n, sizeof(double *));
        r->pa = (int **)ev_calloc((size_t)r->n, sizeof(int *));
    }
    r->S = ns;
    if (ns > 0) {
        r->st_idx = (int *)ev_malloc(sizeof(int) * (size_t)ns);
        r->st_node = (int *)ev_malloc(sizeof(int) * (size_t)ns);
        r->leg_km = (double *)ev_malloc(sizeof(double) * (size_t)ns * ns);
        r->leg_kwh = (double *)ev_malloc(sizeof(double) * (size_t)ns * ns);
        r->leg_ok = (unsigned char *)ev_malloc((size_t)ns * ns);
        for (int i = 0; i < ns; i++) { r->st_idx[i] = station_idx[i]; r->st_node[i] = sc->stations[station_idx[i]].node; }
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < ns; j++) {
                size_t k = (size_t)i * ns + j;
                r->leg_ok[k] = (unsigned char)leg_raw(r, r->st_node[i], r->st_node[j], &r->leg_km[k], &r->leg_kwh[k]);
            }
    }
    return r;
}

int ev_router_leg(ev_router *r, int a, int b, double *km, double *kwh) {
    double k = 0, e = 0;
    int ok = leg_raw(r, a, b, &k, &e);
    if (km) *km = ok ? k : EV_INF;
    if (kwh) *kwh = ok ? e : EV_INF;
    return ok;
}

int ev_router_path(ev_router *r, int a, int b, int *out, int cap) {
    if (r->mode == EV_ROUTE_FLOYD) return ev_floyd_path(r->n, r->next, a, b, out, cap);
    if (a == b) { if (cap > 0) { out[0] = a; return 1; } return 0; }
    ensure_tree(r, a);
    if (r->dist[a][b] >= EV_INF) return 0;
    int cnt = 0, v = b;
    while (v != a) {
        if (cnt >= cap) return 0;
        out[cnt++] = v;
        int k = r->pa[a][v];
        if (k < 0) return 0;
        v = r->sc->arc_src[k];
    }
    if (cnt >= cap) return 0;
    out[cnt++] = a;
    for (int i = 0, j = cnt - 1; i < j; i++, j--) { int t = out[i]; out[i] = out[j]; out[j] = t; }
    return cnt;
}

size_t ev_router_bytes(const ev_router *r) { return r->bytes; }
long ev_router_sssp_runs(const ev_router *r) { return r->runs; }

void ev_router_free(ev_router *r) {
    if (!r) return;
    if (r->dist) {
        for (int i = 0; i < r->n; i++) { ev_free(r->dist[i]); ev_free(r->pa[i]); }
        ev_free(r->dist); ev_free(r->pa);
    }
    ev_free(r->D); ev_free(r->next);
    ev_free(r->st_idx); ev_free(r->st_node);
    ev_free(r->leg_km); ev_free(r->leg_kwh); ev_free(r->leg_ok);
    ev_free(r);
}

/* ======================================================================== station values */
void ev_station_values(ev_scenario *sc) {
    if (sc->values_ready) return;
    long *pass = (long *)ev_calloc((size_t)sc->nn, sizeof(long));
    int *path = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
    ev_router *r = ev_router_create(sc, EV_ROUTE_DIJKSTRA, NULL, 0);
    const ev_config *c = &sc->cfg;
    for (int t = 0; t < sc->nt; t++) {
        const ev_trip *tr = &sc->trips[t];
        double km, kwh;
        if (!ev_router_leg(r, tr->src, tr->dst, &km, &kwh)) continue;
        double avail0 = (tr->soc0 - c->reserve_frac) * c->battery_kwh;
        int wgt = (kwh > avail0) ? 4 : 1;
        int len = ev_router_path(r, tr->src, tr->dst, path, sc->nn);
        for (int i = 0; i < len; i++) pass[path[i]] += wgt;
    }
    for (int s = 0; s < sc->ns; s++) {
        long v = pass[sc->stations[s].node];
        sc->stations[s].value = (int)(v > 2000000000L ? 2000000000L : v);
    }
    ev_router_free(r);
    ev_free(path);
    ev_free(pass);
    sc->values_ready = 1;
}

/* ======================================================================== selection */
static const ev_scenario *g_sc_cmp;
static int cmp_station_ratio(const void *a, const void *b) {
    const ev_station *x = &g_sc_cmp->stations[*(const int *)a], *y = &g_sc_cmp->stations[*(const int *)b];
    double l = (double)x->value * y->cost, rr = (double)y->value * x->cost;
    if (l > rr) return -1;
    if (l < rr) return 1;
    return (*(const int *)a) - (*(const int *)b);
}

int ev_select(ev_scenario *sc, ev_sel_mode mode, ev_selection *sel) {
    memset(sel, 0, sizeof *sel);
    sel->mode = mode;
    ev_station_values(sc);
    int n = sc->ns, W = sc->cfg.budget;
    size_t base = ev_mem_current();
    ev_mem_reset_peak();
    double t0 = ev_now_ms();
    int *take = (int *)ev_calloc((size_t)(n ? n : 1), sizeof(int));
    int *w = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
    long *v = (long *)ev_malloc(sizeof(long) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) { w[i] = sc->stations[i].cost; v[i] = sc->stations[i].value; }
    switch (mode) {
    case EV_SEL_GREEDY: {
        int *ord = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
        for (int i = 0; i < n; i++) ord[i] = i;
        g_sc_cmp = sc;
        ev_merge_sort(ord, (size_t)n, sizeof(int), cmp_station_ratio);
        long cw = 0;
        for (int k = 0; k < n; k++) {
            int i = ord[k];
            if (v[i] > 0 && cw + w[i] <= W) { take[i] = 1; cw += w[i]; }
        }
        ev_free(ord);
        sel->optimal = 0;
        break;
    }
    case EV_SEL_DP:
        ev_knapsack01(n, w, v, W, take);
        sel->optimal = 1;
        break;
    default: {
        ev_bnb_mode bm = mode == EV_SEL_BNB_FIFO ? EV_BNB_FIFO : mode == EV_SEL_BNB_LIFO ? EV_BNB_LIFO : EV_BNB_LC;
        ev_bnb_stats st;
        ev_knapsack_bnb(n, w, v, W, bm, 0, take, &st);
        sel->search_nodes = st.generated;
        sel->optimal = st.complete;
        break;
    }
    }
    int cnt = 0;
    for (int i = 0; i < n; i++) if (take[i] && v[i] > 0) cnt++;
    sel->idx = (int *)ev_malloc(sizeof(int) * (size_t)(cnt ? cnt : 1));
    for (int i = 0; i < n; i++)
        if (take[i] && v[i] > 0) {   /* zero-value stations add cost but no service */
            sel->idx[sel->n++] = i;
            sel->value += v[i];
            sel->cost += w[i];
        }
    sel->ms = ev_now_ms() - t0;
    sel->peak_bytes = ev_mem_peak() - base;
    ev_free(take); ev_free(w); ev_free(v);
    return 0;
}

static int cmp_int_idx(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

int ev_select_ex(ev_scenario *sc, ev_sel_mode mode, const ev_constraints *con, ev_selection *sel) {
    if (!con || (con->nlock == 0 && con->nban == 0)) return ev_select(sc, mode, sel);
    ev_station_values(sc);
    int n = sc->ns, budget = sc->cfg.budget;
    int *saved = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
    unsigned char *locked = (unsigned char *)ev_calloc((size_t)(n ? n : 1), 1);
    for (int i = 0; i < n; i++) saved[i] = sc->stations[i].value;
    long lock_cost = 0;
    for (int k = 0; k < con->nlock; k++) {
        int i = con->lock[k];
        if (i < 0 || i >= n || locked[i]) continue;
        locked[i] = 1;
        lock_cost += sc->stations[i].cost;
        sc->stations[i].value = 0;              /* not offered to the search: built anyway */
    }
    for (int k = 0; k < con->nban; k++) { int i = con->ban[k]; if (i >= 0 && i < n && !locked[i]) sc->stations[i].value = 0; }
    long left = (long)budget - lock_cost;
    sc->cfg.budget = left > 0 ? (int)left : 0;
    ev_select(sc, mode, sel);
    sc->cfg.budget = budget;
    for (int i = 0; i < n; i++) sc->stations[i].value = saved[i];
    /* merge the locked sites into the selection */
    int nl = 0;
    for (int i = 0; i < n; i++) nl += locked[i];
    if (nl) {
        sel->idx = (int *)ev_realloc(sel->idx, sizeof(int) * (size_t)(sel->n + nl));
        for (int i = 0; i < n; i++)
            if (locked[i]) { sel->idx[sel->n++] = i; sel->cost += sc->stations[i].cost; sel->value += sc->stations[i].value; }
        qsort(sel->idx, (size_t)sel->n, sizeof(int), cmp_int_idx);
    }
    sel->over_budget = left < 0;
    ev_free(saved);
    ev_free(locked);
    return 0;
}

void ev_selection_free(ev_selection *sel) { ev_free(sel->idx); sel->idx = NULL; sel->n = 0; }

/* ======================================================================== trip planning */
typedef struct {
    int K;
    double *km_s, *kwh_s, *km_d, *kwh_d;
    unsigned char *ok_s, *ok_d;
    double *dist;
    int *pred;
    unsigned char *done;
} plan_scratch;

static void scratch_init(plan_scratch *s, int S) {
    s->K = S + 2;
    s->km_s = (double *)ev_malloc(sizeof(double) * (size_t)(S + 1));
    s->kwh_s = (double *)ev_malloc(sizeof(double) * (size_t)(S + 1));
    s->km_d = (double *)ev_malloc(sizeof(double) * (size_t)(S + 1));
    s->kwh_d = (double *)ev_malloc(sizeof(double) * (size_t)(S + 1));
    s->ok_s = (unsigned char *)ev_malloc((size_t)(S + 1));
    s->ok_d = (unsigned char *)ev_malloc((size_t)(S + 1));
    s->dist = (double *)ev_malloc(sizeof(double) * (size_t)s->K);
    s->pred = (int *)ev_malloc(sizeof(int) * (size_t)s->K);
    s->done = (unsigned char *)ev_malloc((size_t)s->K);
}

static void scratch_free(plan_scratch *s) {
    ev_free(s->km_s); ev_free(s->kwh_s); ev_free(s->km_d); ev_free(s->kwh_d);
    ev_free(s->ok_s); ev_free(s->ok_d); ev_free(s->dist); ev_free(s->pred); ev_free(s->done);
}

static int plan_one(ev_router *r, const ev_scenario *sc, int t, ev_plan *p, plan_scratch *s) {
    const ev_config *c = &sc->cfg;
    const ev_trip *tr = &sc->trips[t];
    int S = r->S, K = S + 2, DST = S + 1;
    double B = c->battery_kwh;
    double avail0 = (tr->soc0 - c->reserve_frac) * B;
    double availS = (c->target_frac - c->reserve_frac) * B;

    memset(p, 0, sizeof *p);
    p->trip = t;
    p->depart_min = tr->depart_min;

    double dkm, dkwh;
    int ok_dir = ev_router_leg(r, tr->src, tr->dst, &dkm, &dkwh);
    p->direct_km = ok_dir ? dkm : -1;
    p->needs_charge = !ok_dir || dkwh > avail0;
    for (int j = 0; j < S; j++) {
        s->ok_s[j] = (unsigned char)ev_router_leg(r, tr->src, r->st_node[j], &s->km_s[j], &s->kwh_s[j]);
        s->ok_d[j] = (unsigned char)ev_router_leg(r, r->st_node[j], tr->dst, &s->km_d[j], &s->kwh_d[j]);
    }
    /* dense Dijkstra on the charging-leg graph (K = S+2 vertices, O(K^2)) minimising km;
     * an arc exists only if its energy fits the usable battery at its tail */
    for (int i = 0; i < K; i++) { s->dist[i] = EV_INF; s->pred[i] = -1; s->done[i] = 0; }
    s->dist[0] = 0;
    for (;;) {
        int u = -1;
        for (int i = 0; i < K; i++) if (!s->done[i] && s->dist[i] < EV_INF && (u < 0 || s->dist[i] < s->dist[u])) u = i;
        if (u < 0 || u == DST) break;
        s->done[u] = 1;
        for (int v = 1; v < K; v++) {
            if (v == u || s->done[v]) continue;
            double w = EV_INF;
            if (u == 0) {
                if (v == DST) { if (ok_dir && dkwh <= avail0) w = dkm; }
                else if (s->ok_s[v - 1] && s->kwh_s[v - 1] <= avail0) w = s->km_s[v - 1];
            } else {
                int i = u - 1;
                if (v == DST) { if (s->ok_d[i] && s->kwh_d[i] <= availS) w = s->km_d[i]; }
                else {
                    size_t k = (size_t)i * S + (v - 1);
                    if (r->leg_ok[k] && r->leg_kwh[k] <= availS) w = r->leg_km[k];
                }
            }
            if (w < EV_INF && v != DST) w += 1e-3; /* tie-break: prefer fewer stops at equal km */
            if (w < EV_INF && s->dist[u] + w < s->dist[v]) { s->dist[v] = s->dist[u] + w; s->pred[v] = u; }
        }
    }
    if (s->dist[DST] >= EV_INF) return 0;

    /* reconstruct stops */
    int seq[EV_MAX_STOPS + 64], ns = 0;
    for (int v = s->pred[DST]; v > 0; v = s->pred[v]) if (ns < (int)(sizeof seq / sizeof seq[0])) seq[ns++] = v - 1;
    for (int i = 0, j = ns - 1; i < j; i++, j--) { int tmp = seq[i]; seq[i] = seq[j]; seq[j] = tmp; }

    /* simulate energy and time along the plan */
    double e = tr->soc0 * B, tm = tr->depart_min, km = 0, kwh = 0, chg = 0;
    int prev = -1; /* -1 = src */
    for (int k = 0; k <= ns; k++) {
        double lk, le;
        if (k < ns) {
            int j = seq[k];
            if (prev < 0) { lk = s->km_s[j]; le = s->kwh_s[j]; }
            else { size_t q = (size_t)prev * S + j; lk = r->leg_km[q]; le = r->leg_kwh[q]; }
        } else {
            if (prev < 0) { lk = dkm; le = dkwh; } else { lk = s->km_d[prev]; le = s->kwh_d[prev]; }
        }
        km += lk; kwh += le;
        tm += lk / c->speed_kmh * 60.0;
        e -= le;
        if (k < ns) {
            int j = seq[k];
            const ev_station *st = &sc->stations[r->st_idx[j]];
            double need = c->target_frac * B - e;
            double ct = need > 0 ? need / st->power_kw * 60.0 : 0.0;
            if (p->n_stops < EV_MAX_STOPS) {
                p->stop[p->n_stops] = r->st_idx[j];
                p->stop_arrive_min[p->n_stops] = tm;
                p->stop_arrive_kwh[p->n_stops] = e;
                p->stop_charge_min[p->n_stops] = ct;
            }
            p->n_stops++;
            tm += ct; chg += ct;
            if (e < c->target_frac * B) e = c->target_frac * B;
            prev = j;
        }
    }
    if (p->n_stops > EV_MAX_STOPS) p->n_stops = EV_MAX_STOPS;
    p->feasible = 1;
    p->km = km; p->kwh = kwh; p->charge_min = chg; p->arrive_min = tm;
    return 1;
}

int ev_plan_trip(ev_router *r, const ev_scenario *sc, int t, ev_plan *p) {
    plan_scratch s;
    scratch_init(&s, r->S);
    int ok = plan_one(r, sc, t, p, &s);
    scratch_free(&s);
    return ok;
}

void ev_plan_all(ev_router *r, const ev_scenario *sc, ev_plan *plans, ev_kpi *kpi) {
    plan_scratch s;
    scratch_init(&s, r->S);
    ev_kpi k;
    memset(&k, 0, sizeof k);
    k.trips = sc->nt;
    int with_direct = 0;
    for (int t = 0; t < sc->nt; t++) {
        ev_plan *p = &plans[t];
        plan_one(r, sc, t, p, &s);
        if (p->needs_charge) k.need_charge++;
        if (!p->feasible) continue;
        k.feasible++;
        if (p->needs_charge) k.charged_ok++;
        k.avg_km += p->km; k.avg_kwh += p->kwh; k.avg_stops += p->n_stops;
        k.avg_charge_min += p->charge_min; k.avg_travel_min += p->arrive_min - p->depart_min;
        if (p->direct_km >= 0) { k.avg_detour_km += p->km - p->direct_km; with_direct++; }
    }
    if (k.feasible) {
        k.avg_km /= k.feasible; k.avg_kwh /= k.feasible; k.avg_stops /= k.feasible;
        k.avg_charge_min /= k.feasible; k.avg_travel_min /= k.feasible;
    }
    if (with_direct) k.avg_detour_km /= with_direct;
    k.feasible_pct = k.trips ? 100.0 * k.feasible / k.trips : 0;
    if (kpi) *kpi = k;
    scratch_free(&s);
}

/* ======================================================================== capacity */
int ev_capacity_analyze(ev_router *r, const ev_scenario *sc, const ev_selection *sel,
                        const ev_plan *plans, ev_capacity *out) {
    memset(out, 0, sizeof *out);
    out->bottleneck_station = -1;
    const ev_config *c = &sc->cfg;
    int S = sel->n;
    out->assigned = (int *)ev_calloc((size_t)(S ? S : 1), sizeof(int));
    if (S == 0) return 0;
    int *local = (int *)ev_malloc(sizeof(int) * (size_t)sc->ns);
    for (int i = 0; i < sc->ns; i++) local[i] = -1;
    for (int j = 0; j < S; j++) local[sel->idx[j]] = j;

    /* needing trips and their eligible stations (CSR) */
    int *need = (int *)ev_malloc(sizeof(int) * (size_t)(sc->nt ? sc->nt : 1));
    int T = 0;
    for (int t = 0; t < sc->nt; t++)
        if (plans[t].n_stops > 0 || (plans[t].needs_charge && !plans[t].feasible)) need[T++] = t;
    out->trips_needing = T;
    int *head = (int *)ev_calloc((size_t)T + 1, sizeof(int));
    int capa = 1024, na = 0;
    int *adj = (int *)ev_malloc(sizeof(int) * (size_t)capa);
    int *elig_cnt = (int *)ev_calloc((size_t)S, sizeof(int));
    for (int q = 0; q < T; q++) {
        const ev_trip *tr = &sc->trips[need[q]];
        const ev_plan *p = &plans[need[q]];
        double avail0 = (tr->soc0 - c->reserve_frac) * c->battery_kwh;
        int first = p->n_stops > 0 ? local[p->stop[0]] : -1;
        for (int j = 0; j < S; j++) {
            double k1, e1, k2, e2;
            int node = sc->stations[sel->idx[j]].node;
            int ok = (j == first);
            if (!ok && ev_router_leg(r, tr->src, node, &k1, &e1) && e1 <= avail0 &&
                ev_router_leg(r, node, tr->dst, &k2, &e2)) {
                double detour = p->direct_km >= 0 ? k1 + k2 - p->direct_km : 0;
                ok = detour <= c->max_detour_km;
            }
            if (ok) {
                if (na == capa) { capa *= 2; adj = (int *)ev_realloc(adj, sizeof(int) * (size_t)capa); }
                adj[na++] = j;
                elig_cnt[j]++;
            }
        }
        head[q + 1] = na;
    }
    long per = c->window_min / c->session_min;
    if (per < 1) per = 1;
    long *slots = (long *)ev_malloc(sizeof(long) * (size_t)S);
    for (int j = 0; j < S; j++) { slots[j] = sc->stations[sel->idx[j]].ports * per; out->slots_total += slots[j]; }

    /* ---- max-flow: s -> trip (1) -> station (1) -> t (slots) */
    int V = T + S + 2, src = 0, snk = T + S + 1;
    ev_flow *f = ev_flow_create(V);
    for (int q = 0; q < T; q++) ev_flow_add_edge(f, src, 1 + q, 1);
    for (int q = 0; q < T; q++)
        for (int k = head[q]; k < head[q + 1]; k++) ev_flow_add_edge(f, 1 + q, 1 + T + adj[k], 1);
    int *sink_edge = (int *)ev_malloc(sizeof(int) * (size_t)S);
    for (int j = 0; j < S; j++) sink_edge[j] = ev_flow_add_edge(f, 1 + T + j, snk, slots[j]);
    double t0 = ev_now_ms();
    out->flow_dfs = ev_flow_maxflow(f, src, snk, EV_FLOW_DFS, &out->aug_dfs);
    out->ms_dfs = ev_now_ms() - t0;
    ev_flow_reset(f);
    t0 = ev_now_ms();
    out->flow_bfs = ev_flow_maxflow(f, src, snk, EV_FLOW_BFS, &out->aug_bfs);
    out->ms_bfs = ev_now_ms() - t0;
    int worst = -1, worst_elig = -1;
    for (int j = 0; j < S; j++) {
        out->assigned[j] = (int)ev_flow_edge_flow(f, sink_edge[j]);
        if (out->assigned[j] >= slots[j] && elig_cnt[j] > slots[j] && elig_cnt[j] > worst_elig) { worst = j; worst_elig = elig_cnt[j]; }
    }
    out->bottleneck_station = worst >= 0 ? sel->idx[worst] : -1;
    ev_flow_free(f);

    /* ---- bipartite matching: trips <-> individual port-slots */
    long *slot_off = (long *)ev_malloc(sizeof(long) * (size_t)(S + 1));
    slot_off[0] = 0;
    for (int j = 0; j < S; j++) {
        long use = slots[j] < T ? slots[j] : T; /* more slots than trips can never be used */
        slot_off[j + 1] = slot_off[j] + use;
    }
    long NR = slot_off[S];
    int *mh = (int *)ev_calloc((size_t)T + 1, sizeof(int));
    long tot = 0;
    for (int q = 0; q < T; q++) {
        for (int k = head[q]; k < head[q + 1]; k++) tot += slot_off[adj[k] + 1] - slot_off[adj[k]];
        mh[q + 1] = (int)tot;
    }
    int *madj = (int *)ev_malloc(sizeof(int) * (size_t)(tot ? tot : 1));
    long w = 0;
    for (int q = 0; q < T; q++)
        for (int k = head[q]; k < head[q + 1]; k++)
            for (long sl = slot_off[adj[k]]; sl < slot_off[adj[k] + 1]; sl++) madj[w++] = (int)sl;
    int *ml = (int *)ev_malloc(sizeof(int) * (size_t)(T ? T : 1));
    int *mr = (int *)ev_malloc(sizeof(int) * (size_t)(NR ? NR : 1));
    t0 = ev_now_ms();
    out->matching = ev_bipartite_match(T, (int)NR, mh, madj, ml, mr);
    out->ms_match = ev_now_ms() - t0;

    ev_free(ml); ev_free(mr); ev_free(madj); ev_free(mh); ev_free(slot_off);
    ev_free(sink_edge); ev_free(slots); ev_free(elig_cnt); ev_free(adj); ev_free(head);
    ev_free(need); ev_free(local);
    return 0;
}

void ev_capacity_free(ev_capacity *c) { ev_free(c->assigned); c->assigned = NULL; }

/* ======================================================================== sessions */
int ev_sessions_analyze(const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans, ev_sessions *out) {
    memset(out, 0, sizeof *out);
    const ev_config *c = &sc->cfg;
    int S = sel->n;
    out->n = S;
    out->st = (ev_station_load *)ev_calloc((size_t)(S ? S : 1), sizeof(ev_station_load));
    out->hourly_kwh = (double *)ev_calloc((size_t)c->horizon_h, sizeof(double));
    out->surge_lo = out->surge_hi = -1;
    int *local = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1));
    for (int i = 0; i < sc->ns; i++) local[i] = -1;
    for (int j = 0; j < S; j++) { local[sel->idx[j]] = j; out->st[j].station = sel->idx[j]; }

    /* bucket sessions by station */
    int *cnt = (int *)ev_calloc((size_t)(S + 1), sizeof(int));
    for (int t = 0; t < sc->nt; t++)
        for (int k = 0; plans[t].feasible && k < plans[t].n_stops; k++) {
            int j = local[plans[t].stop[k]];
            if (j >= 0) cnt[j + 1]++;
        }
    for (int j = 0; j < S; j++) cnt[j + 1] += cnt[j];
    int total = cnt[S];
    ev_interval *iv = (ev_interval *)ev_malloc(sizeof(ev_interval) * (size_t)(total ? total : 1));
    int *fill = (int *)ev_malloc(sizeof(int) * (size_t)(S ? S : 1));
    for (int j = 0; j < S; j++) fill[j] = cnt[j];
    for (int t = 0; t < sc->nt; t++)
        for (int k = 0; plans[t].feasible && k < plans[t].n_stops; k++) {
            int j = local[plans[t].stop[k]];
            if (j < 0) continue;
            double a = plans[t].stop_arrive_min[k], d = plans[t].stop_charge_min[k];
            if (d < 5.0) d = 5.0; /* plug-in / handover overhead */
            ev_interval x = { t, a, a + d };
            iv[fill[j]++] = x;
            const ev_station *st = &sc->stations[sel->idx[j]];
            int h = (int)floor(a / 60.0) % c->horizon_h;
            if (h < 0) h += c->horizon_h;
            out->hourly_kwh[h] += plans[t].stop_charge_min[k] / 60.0 * st->power_kw;
        }
    out->sessions = total;
    for (int j = 0; j < S; j++) {
        int m = cnt[j + 1] - cnt[j];
        ev_station_load *L = &out->st[j];
        L->sessions = m;
        if (m == 0) continue;
        ev_interval *seg = iv + cnt[j];
        int *assign = (int *)ev_malloc(sizeof(int) * (size_t)m);
        L->chargers_needed = ev_interval_partition(seg, m, assign);
        L->depth = ev_interval_depth(seg, m);
        int ports = sc->stations[sel->idx[j]].ports;
        if (L->chargers_needed > ports) out->overloaded++;
        /* repeated activity selection: fill each physical port with a max compatible set */
        ev_interval *rem = (ev_interval *)ev_malloc(sizeof(ev_interval) * (size_t)m);
        memcpy(rem, seg, sizeof(ev_interval) * (size_t)m);
        int nrem = m, served = 0;
        int *chosen = (int *)ev_malloc(sizeof(int) * (size_t)m);
        unsigned char *mark = (unsigned char *)ev_malloc((size_t)m);
        for (int pidx = 0; pidx < ports && nrem > 0; pidx++) {
            int got = ev_activity_select(rem, nrem, chosen);
            served += got;
            memset(mark, 0, (size_t)nrem);
            for (int q = 0; q < got; q++) mark[chosen[q]] = 1;
            int w = 0;
            for (int q = 0; q < nrem; q++) if (!mark[q]) rem[w++] = rem[q];
            nrem = w;
        }
        L->served = served;
        out->served += served;
        ev_free(mark); ev_free(chosen); ev_free(rem); ev_free(assign);
    }
    /* worst consecutive overload window: max subarray of (load - grid) per hour */
    double *net = (double *)ev_malloc(sizeof(double) * (size_t)c->horizon_h);
    for (int h = 0; h < c->horizon_h; h++) net[h] = out->hourly_kwh[h] - c->grid_kw;
    ev_subarray msa = ev_max_subarray_dc(net, c->horizon_h);
    if (msa.sum > 0) { out->surge_lo = msa.lo; out->surge_hi = msa.hi; out->surge_excess_kwh = msa.sum; }
    ev_free(net); ev_free(fill); ev_free(iv); ev_free(cnt); ev_free(local);
    return 0;
}

void ev_sessions_free(ev_sessions *s) { ev_free(s->st); ev_free(s->hourly_kwh); s->st = NULL; s->hourly_kwh = NULL; }

/* ======================================================================== pipeline */
int ev_run_pipeline(ev_scenario *sc, ev_sel_mode sm, ev_route_mode rm, ev_run *run) {
    return ev_run_pipeline_ex(sc, sm, rm, NULL, run);
}

int ev_run_pipeline_ex(ev_scenario *sc, ev_sel_mode sm, ev_route_mode rm, const ev_constraints *con, ev_run *run) {
    memset(run, 0, sizeof *run);
    run->sel_mode = sm;
    run->route_mode = rm;
    double t0 = ev_now_ms();
    ev_station_values(sc);
    run->ms_values = ev_now_ms() - t0;
    ev_select_ex(sc, sm, con, &run->sel);
    run->ms_select = run->sel.ms;
    run->peak_select = run->sel.peak_bytes;

    size_t base = ev_mem_current();
    ev_mem_reset_peak();
    t0 = ev_now_ms();
    run->router = ev_router_create(sc, rm, run->sel.idx, run->sel.n);
    run->ms_router = ev_now_ms() - t0;
    run->plans = (ev_plan *)ev_calloc((size_t)(sc->nt ? sc->nt : 1), sizeof(ev_plan));
    t0 = ev_now_ms();
    ev_plan_all(run->router, sc, run->plans, &run->kpi);
    run->ms_plan = ev_now_ms() - t0;
    run->peak_route = ev_mem_peak() - base;
    return 0;
}

void ev_run_free(ev_run *run) {
    ev_router_free(run->router);
    ev_free(run->plans);
    ev_selection_free(&run->sel);
    run->router = NULL;
    run->plans = NULL;
}
