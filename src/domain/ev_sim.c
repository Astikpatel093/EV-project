/* ev_sim.c - event-driven day simulation with per-station FIFO queues and driver policies.
 *
 * Every vehicle is a feasible plan. Legs are computed through the router as the day unfolds,
 * so a driver who decides to switch station gets a real route and energy cost for the detour.
 *
 *   ARRIVE(v, j)  free port  -> start charging, schedule DONE
 *                 all busy   -> WAIT policy: join the FIFO queue
 *                               DIVERT policy: if the estimated wait exceeds the threshold, drive
 *                               to the best reachable alternative, otherwise join the queue
 *   DONE(v, j)    free the port, start the head of the queue, schedule the vehicle's next leg
 */
#include "ev_sim.h"
#include "ev_mem.h"

#include <string.h>

int ev_plan_path(ev_router *r, const ev_scenario *sc, const ev_plan *p, int *out, int cap, int *leg_end) {
    const ev_trip *tr = &sc->trips[p->trip];
    int pts[EV_MAX_STOPS + 2], np = 0;
    pts[np++] = tr->src;
    for (int k = 0; k < p->n_stops; k++) pts[np++] = sc->stations[p->stop[k]].node;
    pts[np++] = tr->dst;
    int len = 0;
    int *tmp = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
    for (int k = 0; k + 1 < np; k++) {
        int l = ev_router_path(r, pts[k], pts[k + 1], tmp, sc->nn);
        if (l <= 0) { ev_free(tmp); return 0; }
        for (int i = (k == 0 ? 0 : 1); i < l; i++) {
            if (len >= cap) { ev_free(tmp); return 0; }
            out[len++] = tmp[i];
        }
        if (leg_end) leg_end[k] = len - 1;
    }
    ev_free(tmp);
    return len;
}

/* ---------------------------------------------------------------- event heap */
enum { EVT_ARRIVE = 0, EVT_DONE = 1 };
typedef struct { double t; long seq; int v, j, type; double e; } event; /* j = local station index */

static int ev_less(const event *a, const event *b) { return a->t < b->t || (a->t == b->t && a->seq < b->seq); }
typedef struct { event *a; int n, cap; } heap;
static void hpush(heap *h, event e) {
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 256; h->a = (event *)ev_realloc(h->a, sizeof(event) * (size_t)h->cap); }
    int i = h->n++;
    h->a[i] = e;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!ev_less(&h->a[i], &h->a[p])) break;
        event t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t; i = p;
    }
}
static event hpop(heap *h) {
    event top = h->a[0];
    h->a[0] = h->a[--h->n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->n && ev_less(&h->a[l], &h->a[m])) m = l;
        if (r < h->n && ev_less(&h->a[r], &h->a[m])) m = r;
        if (m == i) break;
        event t = h->a[m]; h->a[m] = h->a[i]; h->a[i] = t; i = m;
    }
    return top;
}

/* ---------------------------------------------------------------- stations */
typedef struct {
    int node, ports, busy;
    double power;
    double *port_end;          /* end time of each port's session (free if <= now) */
    int *qv; double *qt, *qe;  /* FIFO of (vehicle, arrival time, arrival energy) */
    int qh, qt_, qcap;
    double mean_session;       /* running mean session length, for wait estimates */
    int sessions_seen;
} station_s;

static void qpush(station_s *q, int v, double t, double e) {
    if (q->qt_ == q->qcap) {
        if (q->qh > 0) {
            int n = q->qt_ - q->qh;
            memmove(q->qv, q->qv + q->qh, sizeof(int) * (size_t)n);
            memmove(q->qt, q->qt + q->qh, sizeof(double) * (size_t)n);
            memmove(q->qe, q->qe + q->qh, sizeof(double) * (size_t)n);
            q->qh = 0; q->qt_ = n;
        } else {
            q->qcap = q->qcap ? q->qcap * 2 : 16;
            q->qv = (int *)ev_realloc(q->qv, sizeof(int) * (size_t)q->qcap);
            q->qt = (double *)ev_realloc(q->qt, sizeof(double) * (size_t)q->qcap);
            q->qe = (double *)ev_realloc(q->qe, sizeof(double) * (size_t)q->qcap);
        }
    }
    q->qv[q->qt_] = v; q->qt[q->qt_] = t; q->qe[q->qt_] = e; q->qt_++;
}

static double est_wait(const station_s *q, double now) {
    if (q->busy < q->ports) return 0;
    double first = 1e300;
    for (int p = 0; p < q->ports; p++) if (q->port_end[p] < first) first = q->port_end[p];
    int ahead = q->qt_ - q->qh;
    return (first > now ? first - now : 0) + (double)ahead / (double)q->ports * q->mean_session;
}

/* ---------------------------------------------------------------- vehicles */
typedef struct {
    int trip;
    int stop;          /* index of the planned stop being served or headed to */
    int node;          /* node of the station currently used (or src before the first leg) */
    int switched;      /* already switched for the current stop */
    double waited;
} veh;

typedef struct { int v, kind; double t0, t1; int a, b, station; double e0, e1; } rawseg; /* drive: a->b nodes */

typedef struct { rawseg *s; int n, cap; } segbuf;
static void sadd(segbuf *b, rawseg s) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->s = (rawseg *)ev_realloc(b->s, sizeof(rawseg) * (size_t)b->cap); }
    b->s[b->n++] = s;
}

typedef struct {
    ev_router *r;
    const ev_scenario *sc;
    const ev_plan *plans;
    const ev_selection *sel;
    station_s *Q;
    int *local;
    veh *V;
    heap H;
    long seq;
    segbuf segs;
    double B, target, reserve, speed;
    ev_sim *out;
    const ev_sim_opts *opt;
} simctx;

/* next node after the current stop: next planned stop's station, or the destination */
static int next_target(const simctx *c, const veh *v) {
    const ev_plan *p = &c->plans[v->trip];
    if (v->stop + 1 < p->n_stops) return c->sc->stations[p->stop[v->stop + 1]].node;
    return c->sc->trips[v->trip].dst;
}

static void drive_to_station(simctx *c, int vi, int from, int j, double t, double e) {
    double km, kwh;
    int to = c->Q[j].node;
    if (!ev_router_leg(c->r, from, to, &km, &kwh)) { km = 0; kwh = 0; }
    double d = km / c->speed * 60.0;
    rawseg s = { vi, EV_SEG_DRIVE, t, t + d, from, to, -1, e, e - kwh };
    sadd(&c->segs, s);
    event a = { t + d, c->seq++, vi, j, EVT_ARRIVE, e - kwh };
    hpush(&c->H, a);
}

static void start_charge(simctx *c, int vi, int j, double t_arr, double t, double e) {
    station_s *q = &c->Q[j];
    veh *v = &c->V[vi];
    if (t > t_arr + 1e-9) {
        rawseg w = { vi, EV_SEG_WAIT, t_arr, t, q->node, q->node, c->sel->idx[j], e, e };
        sadd(&c->segs, w);
        v->waited += t - t_arr;
        c->out->st[j].waited++;
        c->out->st[j].wait_min += t - t_arr;
    }
    double need = c->target - e;
    double d = need > 0 ? need / q->power * 60.0 : 0;
    if (d < 5.0) d = 5.0; /* plug-in and handover */
    double after = e < c->target ? c->target : e;
    rawseg s = { vi, EV_SEG_CHARGE, t, t + d, q->node, q->node, c->sel->idx[j], e, after };
    sadd(&c->segs, s);
    int port = 0;
    for (int p = 1; p < q->ports; p++) if (q->port_end[p] < q->port_end[port]) port = p;
    q->port_end[port] = t + d;
    q->busy++;
    q->sessions_seen++;
    q->mean_session += (d - q->mean_session) / q->sessions_seen;
    event done = { t + d, c->seq++, vi, j, EVT_DONE, after };
    hpush(&c->H, done);
}

static int try_divert(simctx *c, int vi, int j, double t, double e) {
    veh *v = &c->V[vi];
    if (!c->opt || c->opt->policy != EV_POLICY_DIVERT || v->switched) return 0;
    double here = est_wait(&c->Q[j], t);
    double thr = c->opt->divert_after_min > 0 ? c->opt->divert_after_min : 20.0;
    if (here <= thr) return 0;
    int next = next_target(c, v);
    int best = -1;
    double best_score = here;
    for (int k = 0; k < c->sel->n; k++) {
        if (k == j) continue;
        double km, kwh, km2, kwh2;
        if (!ev_router_leg(c->r, c->Q[j].node, c->Q[k].node, &km, &kwh)) continue;
        if (e - kwh < c->reserve) continue;                       /* must arrive above the reserve */
        if (!ev_router_leg(c->r, c->Q[k].node, next, &km2, &kwh2)) continue;
        if (kwh2 > c->target - c->reserve) continue;              /* and continue after charging */
        double drive = km / c->speed * 60.0;
        double score = drive + est_wait(&c->Q[k], t + drive);
        if (score < best_score - 1e-9) { best_score = score; best = k; }
    }
    if (best < 0) return 0;
    v->switched = 1;
    c->out->st[j].diverted_away++;
    drive_to_station(c, vi, c->Q[j].node, best, t, e);
    return 1;
}

int ev_simulate_ex(ev_router *r, const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans,
                   const ev_sim_opts *opts, ev_sim *out) {
    memset(out, 0, sizeof *out);
    double t_start = ev_now_ms();
    const ev_config *cf = &sc->cfg;
    simctx c;
    memset(&c, 0, sizeof c);
    c.r = r; c.sc = sc; c.plans = plans; c.sel = sel; c.out = out; c.opt = opts;
    c.B = cf->battery_kwh; c.target = cf->target_frac * c.B; c.reserve = cf->reserve_frac * c.B; c.speed = cf->speed_kmh;

    int nv = 0;
    for (int t = 0; t < sc->nt; t++) if (plans[t].feasible) nv++;
    out->nv = nv;
    out->trip = (int *)ev_malloc(sizeof(int) * (size_t)(nv ? nv : 1));
    c.V = (veh *)ev_calloc((size_t)(nv ? nv : 1), sizeof(veh));

    int S = sel->n;
    out->nst = S;
    out->st = (ev_sim_station *)ev_calloc((size_t)(S ? S : 1), sizeof(ev_sim_station));
    c.Q = (station_s *)ev_calloc((size_t)(S ? S : 1), sizeof(station_s));
    c.local = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1));
    for (int i = 0; i < sc->ns; i++) c.local[i] = -1;
    for (int j = 0; j < S; j++) {
        const ev_station *st = &sc->stations[sel->idx[j]];
        c.local[sel->idx[j]] = j;
        out->st[j].station = sel->idx[j];
        c.Q[j].node = st->node; c.Q[j].ports = st->ports; c.Q[j].power = st->power_kw; c.Q[j].mean_session = 30.0;
        c.Q[j].port_end = (double *)ev_calloc((size_t)st->ports, sizeof(double));
    }

    int v = 0;
    for (int t = 0; t < sc->nt; t++) {
        const ev_plan *p = &plans[t];
        if (!p->feasible) continue;
        out->trip[v] = t;
        veh *ve = &c.V[v];
        ve->trip = t;
        ve->node = sc->trips[t].src;
        double e0 = sc->trips[t].soc0 * c.B;
        if (p->n_stops == 0) {
            double km, kwh;
            if (!ev_router_leg(r, sc->trips[t].src, sc->trips[t].dst, &km, &kwh)) { km = p->km; kwh = p->kwh; }
            double d = km / c.speed * 60.0;
            rawseg s = { v, EV_SEG_DRIVE, p->depart_min, p->depart_min + d, sc->trips[t].src, sc->trips[t].dst, -1, e0, e0 - kwh };
            sadd(&c.segs, s);
        } else {
            drive_to_station(&c, v, sc->trips[t].src, c.local[p->stop[0]], p->depart_min, e0);
        }
        v++;
    }

    while (c.H.n > 0) {
        event e = hpop(&c.H);
        out->events++;
        veh *ve = &c.V[e.v];
        station_s *q = &c.Q[e.j];
        if (e.type == EVT_ARRIVE) {
            ve->node = q->node;
            if (q->busy < q->ports) { start_charge(&c, e.v, e.j, e.t, e.t, e.e); continue; }
            if (try_divert(&c, e.v, e.j, e.t, e.e)) continue;
            qpush(q, e.v, e.t, e.e);
            int ql = q->qt_ - q->qh;
            if (ql > out->st[e.j].max_queue) out->st[e.j].max_queue = ql;
        } else {
            out->st[e.j].sessions++;
            q->busy--;
            /* the vehicle moves on */
            const ev_plan *p = &plans[ve->trip];
            int next = next_target(&c, ve);
            ve->stop++;
            ve->switched = 0;
            if (ve->stop < p->n_stops) drive_to_station(&c, e.v, q->node, c.local[p->stop[ve->stop]], e.t, e.e);
            else {
                double km, kwh;
                if (!ev_router_leg(r, q->node, next, &km, &kwh)) { km = 0; kwh = 0; }
                double d = km / c.speed * 60.0;
                rawseg s = { e.v, EV_SEG_DRIVE, e.t, e.t + d, q->node, next, -1, e.e, e.e - kwh };
                sadd(&c.segs, s);
            }
            /* the port goes to the head of the queue */
            if (q->qh < q->qt_) {
                int w = q->qv[q->qh];
                double ta = q->qt[q->qh], ea = q->qe[q->qh];
                q->qh++;
                start_charge(&c, w, e.j, ta, e.t, ea);
            }
        }
    }

    /* group segments by vehicle (counting sort keeps each vehicle's time order) and build paths */
    out->seg_off = (int *)ev_calloc((size_t)nv + 1, sizeof(int));
    for (int i = 0; i < c.segs.n; i++) out->seg_off[c.segs.s[i].v + 1]++;
    for (int i = 0; i < nv; i++) out->seg_off[i + 1] += out->seg_off[i];
    out->nseg = c.segs.n;
    out->seg = (ev_sim_seg *)ev_malloc(sizeof(ev_sim_seg) * (size_t)(c.segs.n ? c.segs.n : 1));
    int *fill = (int *)ev_malloc(sizeof(int) * (size_t)(nv ? nv : 1));
    for (int i = 0; i < nv; i++) fill[i] = out->seg_off[i];
    rawseg *ord = (rawseg *)ev_malloc(sizeof(rawseg) * (size_t)(c.segs.n ? c.segs.n : 1));
    for (int i = 0; i < c.segs.n; i++) ord[fill[c.segs.s[i].v]++] = c.segs.s[i];

    out->path_off = (int *)ev_calloc((size_t)nv + 1, sizeof(int));
    int pcap = 1024, plen = 0;
    out->path = (int *)ev_malloc(sizeof(int) * (size_t)pcap);
    int *tmp = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
    for (int vi = 0; vi < nv; vi++) {
        int first = 1;
        double end = 0;
        for (int i = out->seg_off[vi]; i < out->seg_off[vi + 1]; i++) {
            const rawseg *g = &ord[i];
            ev_sim_seg *o = &out->seg[i];
            o->vehicle = vi; o->kind = g->kind; o->t0 = g->t0; o->t1 = g->t1; o->station = g->station; o->e0 = g->e0; o->e1 = g->e1;
            if (g->kind == EV_SEG_DRIVE) {
                int l = ev_router_path(r, g->a, g->b, tmp, sc->nn);
                if (l <= 0) { tmp[0] = g->a; l = 1; if (g->b != g->a) { tmp[1] = g->b; l = 2; } }
                int skip = first ? 0 : 1;
                while (plen + l > pcap) { pcap *= 2; out->path = (int *)ev_realloc(out->path, sizeof(int) * (size_t)pcap); }
                int start = first ? plen : plen - 1;
                for (int k = skip; k < l; k++) out->path[plen++] = tmp[k];
                o->p0 = start - out->path_off[vi];
                o->p1 = plen - 1 - out->path_off[vi];
                first = 0;
            } else {
                o->p0 = o->p1 = plen - 1 - out->path_off[vi];
            }
            if (g->t1 > end) end = g->t1;
        }
        out->path_off[vi + 1] = plen;
        if (c.V[vi].waited > 1e-9) { out->delayed++; out->total_wait += c.V[vi].waited; if (c.V[vi].waited > out->max_wait) out->max_wait = c.V[vi].waited; }
        if (end > out->end_min) out->end_min = end;
    }
    for (int j = 0; j < S; j++) out->diverted += out->st[j].diverted_away;
    out->mean_extra_min = nv ? out->total_wait / nv : 0;

    ev_free(tmp); ev_free(ord); ev_free(fill); ev_free(c.segs.s); ev_free(c.H.a);
    for (int j = 0; j < S; j++) { ev_free(c.Q[j].port_end); ev_free(c.Q[j].qv); ev_free(c.Q[j].qt); ev_free(c.Q[j].qe); }
    ev_free(c.Q); ev_free(c.local); ev_free(c.V);
    out->ms = ev_now_ms() - t_start;
    return 0;
}

int ev_simulate(ev_router *r, const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans, ev_sim *out) {
    return ev_simulate_ex(r, sc, sel, plans, NULL, out);
}

void ev_sim_free(ev_sim *s) {
    ev_free(s->trip); ev_free(s->path_off); ev_free(s->path);
    ev_free(s->seg); ev_free(s->seg_off); ev_free(s->st);
    memset(s, 0, sizeof *s);
}
