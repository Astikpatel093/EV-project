/* ev_report.c - exports, strategy comparison and the syllabus analysis report. */
#include "ev_report.h"
#include "ev_dnc.h"
#include "ev_dp.h"
#include "ev_greedy.h"
#include "ev_mem.h"
#include "ev_nt.h"
#include "ev_search.h"
#include "ev_sort.h"

#include <math.h>
#include <string.h>

/* ======================================================================== JSON writer */
void ev_json_open(ev_json *j, FILE *f) { j->f = f; j->depth = 0; j->first[0] = 1; }

static void jkey(ev_json *j, const char *key) {
    if (!j->first[j->depth]) fputc(',', j->f);
    j->first[j->depth] = 0;
    fputc('\n', j->f);
    for (int i = 0; i < j->depth; i++) fputs("  ", j->f);
    if (key) fprintf(j->f, "\"%s\": ", key);
}

static void jpush(ev_json *j, char c) { fputc(c, j->f); if (j->depth < 63) j->depth++; j->first[j->depth] = 1; }
static void jpop(ev_json *j, char c) {
    int empty = j->first[j->depth];
    if (j->depth > 0) j->depth--;
    if (!empty) { fputc('\n', j->f); for (int i = 0; i < j->depth; i++) fputs("  ", j->f); }
    fputc(c, j->f);
    if (j->depth == 0) fputc('\n', j->f);
}

void ev_json_obj(ev_json *j, const char *key) { if (j->depth > 0) jkey(j, key); jpush(j, '{'); }
void ev_json_end_obj(ev_json *j) { jpop(j, '}'); }
void ev_json_arr(ev_json *j, const char *key) { jkey(j, key); jpush(j, '['); }
void ev_json_end_arr(ev_json *j) { jpop(j, ']'); }
void ev_json_num(ev_json *j, const char *key, double v) {
    jkey(j, key);
    if (!isfinite(v)) fputs("null", j->f); else fprintf(j->f, "%.6g", v);
}
void ev_json_int(ev_json *j, const char *key, long long v) { jkey(j, key); fprintf(j->f, "%lld", v); }
void ev_json_bool(ev_json *j, const char *key, int v) { jkey(j, key); fputs(v ? "true" : "false", j->f); }
void ev_json_str(ev_json *j, const char *key, const char *v) {
    jkey(j, key);
    fputc('"', j->f);
    for (const char *p = v; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', j->f); fputc(*p, j->f); }
        else if ((unsigned char)*p < 0x20) fprintf(j->f, "\\u%04x", (unsigned char)*p);
        else fputc(*p, j->f);
    }
    fputc('"', j->f);
}

/* ======================================================================== exports */
int ev_export_selection_csv(const char *path, const ev_scenario *sc, const ev_selection *sel) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "station_id,node_id,node_name,cost,ports,power_kw,value\n");
    for (int k = 0; k < sel->n; k++) {
        const ev_station *s = &sc->stations[sel->idx[k]];
        const ev_node *nd = &sc->nodes[s->node];
        fprintf(f, "%d,%d,%s,%d,%d,%.1f,%d\n", s->id, nd->id, nd->name, s->cost, s->ports, s->power_kw, s->value);
    }
    fclose(f);
    return 0;
}

int ev_export_plans_csv(const char *path, const ev_scenario *sc, const ev_run *run) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "trip_id,src_id,dst_id,feasible,needs_charge,km,kwh,direct_km,depart_min,arrive_min,charge_min,stops\n");
    for (int t = 0; t < sc->nt; t++) {
        const ev_plan *p = &run->plans[t];
        const ev_trip *tr = &sc->trips[t];
        fprintf(f, "%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,", tr->id, sc->nodes[tr->src].id,
                sc->nodes[tr->dst].id, p->feasible, p->needs_charge, p->feasible ? p->km : 0.0,
                p->feasible ? p->kwh : 0.0, p->direct_km, p->depart_min, p->feasible ? p->arrive_min : 0.0,
                p->charge_min);
        for (int k = 0; k < p->n_stops; k++) fprintf(f, "%s%d", k ? ";" : "", sc->stations[p->stop[k]].id);
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

int ev_export_summary_json(const char *path, const ev_scenario *sc, const ev_run *run,
                           const ev_capacity *cap, const ev_sessions *ses) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    ev_json j;
    ev_json_open(&j, f);
    ev_json_obj(&j, NULL);
    ev_json_str(&j, "scenario", sc->cfg.name);
    ev_json_obj(&j, "size");
    ev_json_int(&j, "nodes", sc->nn); ev_json_int(&j, "roads", sc->ne);
    ev_json_int(&j, "candidates", sc->ns); ev_json_int(&j, "trips", sc->nt);
    ev_json_int(&j, "budget", sc->cfg.budget);
    ev_json_end_obj(&j);
    ev_json_obj(&j, "strategy");
    ev_json_str(&j, "selection", ev_sel_name(run->sel_mode));
    ev_json_str(&j, "routing", ev_route_name(run->route_mode));
    ev_json_end_obj(&j);
    ev_json_obj(&j, "selection");
    ev_json_int(&j, "stations", run->sel.n); ev_json_int(&j, "cost", run->sel.cost);
    ev_json_int(&j, "value", run->sel.value); ev_json_bool(&j, "optimal", run->sel.optimal);
    ev_json_int(&j, "search_nodes", run->sel.search_nodes);
    ev_json_num(&j, "ms", run->ms_select); ev_json_int(&j, "peak_bytes", (long long)run->peak_select);
    ev_json_arr(&j, "station_ids");
    for (int k = 0; k < run->sel.n; k++) ev_json_int(&j, NULL, sc->stations[run->sel.idx[k]].id);
    ev_json_end_arr(&j);
    ev_json_end_obj(&j);
    const ev_kpi *k = &run->kpi;
    ev_json_obj(&j, "service");
    ev_json_int(&j, "trips", k->trips); ev_json_int(&j, "feasible", k->feasible);
    ev_json_num(&j, "feasible_pct", k->feasible_pct); ev_json_int(&j, "need_charge", k->need_charge);
    ev_json_int(&j, "charged_ok", k->charged_ok); ev_json_num(&j, "avg_km", k->avg_km);
    ev_json_num(&j, "avg_kwh", k->avg_kwh); ev_json_num(&j, "avg_stops", k->avg_stops);
    ev_json_num(&j, "avg_charge_min", k->avg_charge_min); ev_json_num(&j, "avg_detour_km", k->avg_detour_km);
    ev_json_num(&j, "avg_travel_min", k->avg_travel_min);
    ev_json_end_obj(&j);
    ev_json_obj(&j, "performance");
    ev_json_num(&j, "values_ms", run->ms_values); ev_json_num(&j, "router_ms", run->ms_router);
    ev_json_num(&j, "plan_ms", run->ms_plan); ev_json_int(&j, "route_peak_bytes", (long long)run->peak_route);
    ev_json_int(&j, "sssp_runs", ev_router_sssp_runs(run->router));
    ev_json_end_obj(&j);
    if (cap) {
        ev_json_obj(&j, "capacity");
        ev_json_int(&j, "trips_needing", cap->trips_needing); ev_json_int(&j, "slots", cap->slots_total);
        ev_json_int(&j, "maxflow_ff_dfs", cap->flow_dfs); ev_json_int(&j, "maxflow_edmonds_karp", cap->flow_bfs);
        ev_json_int(&j, "bipartite_matching", cap->matching);
        ev_json_int(&j, "augmentations_dfs", cap->aug_dfs); ev_json_int(&j, "augmentations_bfs", cap->aug_bfs);
        ev_json_num(&j, "ms_dfs", cap->ms_dfs); ev_json_num(&j, "ms_bfs", cap->ms_bfs); ev_json_num(&j, "ms_match", cap->ms_match);
        ev_json_int(&j, "bottleneck_station_id", cap->bottleneck_station >= 0 ? sc->stations[cap->bottleneck_station].id : -1);
        ev_json_end_obj(&j);
    }
    if (ses) {
        ev_json_obj(&j, "sessions");
        ev_json_int(&j, "sessions", ses->sessions); ev_json_int(&j, "served_with_ports", ses->served);
        ev_json_int(&j, "overloaded_stations", ses->overloaded);
        ev_json_int(&j, "surge_from_h", ses->surge_lo); ev_json_int(&j, "surge_to_h", ses->surge_hi);
        ev_json_num(&j, "surge_excess_kwh", ses->surge_excess_kwh);
        ev_json_arr(&j, "hourly_kwh");
        for (int h = 0; h < sc->cfg.horizon_h; h++) ev_json_num(&j, NULL, ses->hourly_kwh[h]);
        ev_json_end_arr(&j);
        ev_json_arr(&j, "stations");
        for (int q = 0; q < ses->n; q++) {
            const ev_station_load *L = &ses->st[q];
            ev_json_obj(&j, NULL);
            ev_json_int(&j, "id", sc->stations[L->station].id);
            ev_json_int(&j, "ports", sc->stations[L->station].ports);
            ev_json_int(&j, "sessions", L->sessions);
            ev_json_int(&j, "chargers_needed", L->chargers_needed);
            ev_json_int(&j, "served", L->served);
            ev_json_end_obj(&j);
        }
        ev_json_end_arr(&j);
        ev_json_end_obj(&j);
    }
    ev_json_end_obj(&j);
    fclose(f);
    return 0;
}

void ev_print_run(FILE *o, const ev_scenario *sc, const ev_run *run, const ev_capacity *cap, const ev_sessions *ses) {
    const ev_kpi *k = &run->kpi;
    fprintf(o, "\n== Scenario '%s': %d nodes, %d roads, %d candidate sites, %d trips, budget %d\n",
            sc->cfg.name, sc->nn, sc->ne, sc->ns, sc->nt, sc->cfg.budget);
    fprintf(o, "-- Station selection [%s]: %d stations, cost %ld, value %ld%s, %.2f ms, %zu B peak\n",
            ev_sel_name(run->sel_mode), run->sel.n, run->sel.cost, run->sel.value,
            run->sel.optimal ? " (optimal)" : "", run->ms_select, run->peak_select);
    fprintf(o, "   sites:");
    for (int i = 0; i < run->sel.n && i < 20; i++) fprintf(o, " %d", sc->stations[run->sel.idx[i]].id);
    if (run->sel.n > 20) fprintf(o, " ... (+%d)", run->sel.n - 20);
    fprintf(o, "\n-- Trip planning [%s]: %d/%d feasible (%.1f%%); %d needed a charge, %d of those served\n",
            ev_route_name(run->route_mode), k->feasible, k->trips, k->feasible_pct, k->need_charge, k->charged_ok);
    fprintf(o, "   avg %.1f km, %.2f kWh, %.2f stops, %.1f min charging, %.1f km detour, %.1f min door-to-door\n",
            k->avg_km, k->avg_kwh, k->avg_stops, k->avg_charge_min, k->avg_detour_km, k->avg_travel_min);
    fprintf(o, "   router %.2f ms + planning %.2f ms, %ld SSSP trees, %zu B peak\n",
            run->ms_router, run->ms_plan, ev_router_sssp_runs(run->router), run->peak_route);
    if (cap) {
        fprintf(o, "-- Capacity (window %d min): %d trips need a charge, %ld port-slots\n",
                sc->cfg.window_min, cap->trips_needing, cap->slots_total);
        fprintf(o, "   max-flow FF/DFS=%ld (%ld aug, %.2f ms)  Edmonds-Karp=%ld (%ld aug, %.2f ms)  matching=%d (%.2f ms)%s\n",
                cap->flow_dfs, cap->aug_dfs, cap->ms_dfs, cap->flow_bfs, cap->aug_bfs, cap->ms_bfs,
                cap->matching, cap->ms_match,
                (cap->flow_dfs == cap->flow_bfs && cap->flow_bfs == cap->matching) ? "  [consistent]" : "  [MISMATCH]");
        if (cap->bottleneck_station >= 0) fprintf(o, "   bottleneck station: %d\n", sc->stations[cap->bottleneck_station].id);
    }
    if (ses) {
        fprintf(o, "-- Sessions: %ld planned, %ld servable with installed ports, %d overloaded stations\n",
                ses->sessions, ses->served, ses->overloaded);
        if (ses->surge_lo >= 0)
            fprintf(o, "   worst grid surge: hours %ld-%ld, %.1f kWh above supply\n", ses->surge_lo, ses->surge_hi, ses->surge_excess_kwh);
        else fprintf(o, "   grid supply covers every hour of the horizon\n");
    }
}

/* ======================================================================== compare */
int ev_compare(ev_scenario *sc, FILE *o, const char *csv_path) {
    FILE *csv = csv_path ? fopen(csv_path, "w") : NULL;
    if (csv) fprintf(csv, "selection,routing,stations,cost,value,optimal,select_ms,select_peak_bytes,search_nodes,"
                          "feasible,feasible_pct,avg_km,avg_kwh,avg_stops,router_ms,plan_ms,route_peak_bytes,sssp_runs\n");
    fprintf(o, "\n== Strategy comparison on '%s' (%d trips, budget %d)\n", sc->cfg.name, sc->nt, sc->cfg.budget);
    fprintf(o, "%-9s %-9s %4s %6s %7s %3s %9s %9s | %8s %6s %7s %7s %9s %10s\n", "select", "route", "st", "cost", "value",
            "opt", "sel_ms", "nodes", "feasible", "%", "avg_km", "avg_kWh", "route_ms", "route_KiB");
    for (int sm = 0; sm < EV_SEL_COUNT; sm++)
        for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) {
            if (rm == EV_ROUTE_FLOYD && sc->nn > 1600) continue; /* Θ(n^3) - skip on huge maps */
            ev_run run;
            ev_run_pipeline(sc, (ev_sel_mode)sm, (ev_route_mode)rm, &run);
            const ev_kpi *k = &run.kpi;
            fprintf(o, "%-9s %-9s %4d %6ld %7ld %3s %9.2f %9ld | %8d %6.1f %7.2f %7.3f %9.2f %10.1f\n",
                    ev_sel_name(sm), ev_route_name(rm), run.sel.n, run.sel.cost, run.sel.value,
                    run.sel.optimal ? "yes" : "no", run.ms_select, run.sel.search_nodes, k->feasible, k->feasible_pct,
                    k->avg_km, k->avg_kwh, run.ms_router + run.ms_plan, run.peak_route / 1024.0);
            if (csv)
                fprintf(csv, "%s,%s,%d,%ld,%ld,%d,%.4f,%zu,%ld,%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%zu,%ld\n",
                        ev_sel_name(sm), ev_route_name(rm), run.sel.n, run.sel.cost, run.sel.value, run.sel.optimal,
                        run.ms_select, run.peak_select, run.sel.search_nodes, k->feasible, k->feasible_pct, k->avg_km,
                        k->avg_kwh, k->avg_stops, run.ms_router, run.ms_plan, run.peak_route,
                        ev_router_sssp_runs(run.router));
            ev_run_free(&run);
        }
    if (csv) fclose(csv);
    return 0;
}

/* ======================================================================== analysis */
static int cmp_trip_depart(const void *a, const void *b) {
    const ev_trip *x = (const ev_trip *)a, *y = (const ev_trip *)b;
    return (x->depart_min > y->depart_min) - (x->depart_min < y->depart_min);
}
static int cmp_trip_depart_id(const void *a, const void *b) {
    int c = cmp_trip_depart(a, b);
    if (c) return c;
    const ev_trip *x = (const ev_trip *)a, *y = (const ev_trip *)b;
    return (x->id > y->id) - (x->id < y->id);
}
static int cmp_int_asc(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static const ev_scenario *g_sc;
static int cmp_station_cost(const void *a, const void *b) {
    int x = g_sc->stations[*(const int *)a].cost, y = g_sc->stations[*(const int *)b].cost;
    if (x != y) return (x > y) - (x < y);
    return *(const int *)a - *(const int *)b;
}

typedef struct { const ev_scenario *sc; const int *idx; } celeb_ctx;
static int station_rank_less(const ev_scenario *sc, int a, int b) {
    const ev_station *x = &sc->stations[a], *y = &sc->stations[b];
    if (x->ports != y->ports) return x->ports < y->ports;
    if (x->power_kw != y->power_kw) return x->power_kw < y->power_kw;
    if (x->value != y->value) return x->value < y->value;
    return a > b;
}
/* operator a forwards overflow to b iff b is strictly better equipped */
static int knows_fn(void *vctx, int a, int b) {
    celeb_ctx *c = (celeb_ctx *)vctx;
    return station_rank_less(c->sc, c->idx[a], c->idx[b]);
}

static int lower_cost_index(const ev_scenario *sc, const int *ord, int n, int maxcost) {
    /* first index whose cost > maxcost (binary search on the cost-sorted index) */
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sc->stations[ord[mid]].cost <= maxcost) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int ev_analyze(ev_scenario *sc, FILE *o, const char *json_path) {
    FILE *jf = json_path ? fopen(json_path, "w") : NULL;
    int rc = ev_analyze_fp(sc, o, jf);
    if (jf) fclose(jf);
    return rc;
}

int ev_analyze_fp(ev_scenario *sc, FILE *o, FILE *jf) {
    ev_json J;
    ev_json_open(&J, jf ? jf : NULL);
    ev_json *j = jf ? &J : NULL;
#define JOBJ(k) do { if (j) ev_json_obj(j, k); } while (0)
#define JEND() do { if (j) ev_json_end_obj(j); } while (0)
#define JNUM(k, v) do { if (j) ev_json_num(j, k, v); } while (0)
#define JINT(k, v) do { if (j) ev_json_int(j, k, (long long)(v)); } while (0)
#define JSTR(k, v) do { if (j) ev_json_str(j, k, v); } while (0)
#define JBOOL(k, v) do { if (j) ev_json_bool(j, k, v); } while (0)
    JOBJ(NULL);
    JSTR("scenario", sc->cfg.name);

    const ev_config *c = &sc->cfg;
    ev_run run;
    ev_run_pipeline(sc, EV_SEL_DP, EV_ROUTE_DIJKSTRA, &run);
    ev_capacity cap;
    ev_sessions ses;
    ev_capacity_analyze(run.router, sc, &run.sel, run.plans, &cap);
    ev_sessions_analyze(sc, &run.sel, run.plans, &ses);
    ev_print_run(o, sc, &run, &cap, &ses);
    int S = run.sel.n;
    double t0;

    /* ------------------------------------------------ 1. sorting, searching, indices */
    fprintf(o, "\n[1] Sorting, searching and domain indices\n");
    JOBJ("sorting_searching");
    {
        int n = sc->nt;
        ev_trip *a = (ev_trip *)ev_malloc(sizeof(ev_trip) * (size_t)(n ? n : 1));
        ev_trip *b = (ev_trip *)ev_malloc(sizeof(ev_trip) * (size_t)(n ? n : 1));
        memcpy(a, sc->trips, sizeof(ev_trip) * (size_t)n);
        memcpy(b, sc->trips, sizeof(ev_trip) * (size_t)n);
        t0 = ev_now_ms();
        ev_merge_sort(a, (size_t)n, sizeof(ev_trip), cmp_trip_depart);
        double ms_m = ev_now_ms() - t0;
        t0 = ev_now_ms();
        ev_quick_sort(b, (size_t)n, sizeof(ev_trip), cmp_trip_depart_id, EV_PIVOT_MEDIAN3);
        double ms_q = ev_now_ms() - t0;
        int sorted = 1, stable = 1, agree = 1;
        for (int i = 1; i < n; i++) {
            if (a[i - 1].depart_min > a[i].depart_min) sorted = 0;
            if (a[i - 1].depart_min == a[i].depart_min && a[i - 1].id > a[i].id) stable = 0; /* ids ascend in input */
        }
        for (int i = 0; i < n; i++) if (a[i].depart_min != b[i].depart_min) agree = 0;
        fprintf(o, "    dispatch queue (trips by departure): merge %.3f ms [sorted=%d stable=%d], quick %.3f ms [agrees=%d]\n",
                ms_m, sorted, stable, ms_q, agree);
        JNUM("merge_ms", ms_m); JNUM("quick_ms", ms_q); JBOOL("merge_sorted", sorted);
        JBOOL("merge_stable", stable); JBOOL("quick_agrees", agree);
        ev_free(a); ev_free(b);

        int *ord = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1));
        for (int i = 0; i < sc->ns; i++) ord[i] = i;
        g_sc = sc;
        ev_quick_sort(ord, (size_t)sc->ns, sizeof(int), cmp_station_cost, EV_PIVOT_RANDOM);
        int cut = c->budget / 10;
        int afford = lower_cost_index(sc, ord, sc->ns, cut);
        fprintf(o, "    cost index (quick sort): %d candidate sites cost <= %d (10%% of budget) via binary search\n", afford, cut);
        JINT("sites_under_10pct_budget", afford);
        ev_free(ord);

        long hit_i = 0, hit_r = 0;
        t0 = ev_now_ms();
        for (int t = 0; t < sc->nt; t++) {
            int id = sc->nodes[sc->trips[t].src].id;
            long k = ev_bsearch_iter(&id, sc->idx_ids, (size_t)sc->nn, sizeof(int), cmp_int_asc);
            if (k >= 0 && sc->idx_pos[k] == sc->trips[t].src) hit_i++;
        }
        double ms_i = ev_now_ms() - t0;
        t0 = ev_now_ms();
        for (int t = 0; t < sc->nt; t++) {
            int id = sc->nodes[sc->trips[t].dst].id;
            long k = ev_bsearch_rec(&id, sc->idx_ids, (size_t)sc->nn, sizeof(int), cmp_int_asc);
            if (k >= 0 && sc->idx_pos[k] == sc->trips[t].dst) hit_r++;
        }
        double ms_r = ev_now_ms() - t0;
        fprintf(o, "    node-id index: iterative binary search %ld/%d hits (%.3f ms), recursive %ld/%d hits (%.3f ms)\n",
                hit_i, sc->nt, ms_i, hit_r, sc->nt, ms_r);
        JINT("bsearch_iter_hits", hit_i); JINT("bsearch_rec_hits", hit_r);
    }
    JEND();

    /* ------------------------------------------------ 2. divide and conquer */
    fprintf(o, "\n[2] Divide and conquer\n");
    JOBJ("divide_conquer");
    {
        double *net = (double *)ev_malloc(sizeof(double) * (size_t)c->horizon_h);
        for (int h = 0; h < c->horizon_h; h++) net[h] = ses.hourly_kwh[h] - c->grid_kw;
        ev_subarray d = ev_max_subarray_dc(net, c->horizon_h), kd = ev_max_subarray_kadane(net, c->horizon_h);
        fprintf(o, "    max subarray of (charging load - grid supply) per hour: D&C sum %.1f [h%ld..h%ld], Kadane %.1f -> %s\n",
                d.sum, d.lo, d.hi, kd.sum, fabs(d.sum - kd.sum) < 1e-6 ? "agree" : "DISAGREE");
        JNUM("surge_sum_dc", d.sum); JINT("surge_lo", d.lo); JINT("surge_hi", d.hi); JNUM("surge_sum_kadane", kd.sum);
        ev_free(net);

        double fade = 0.0002;
        unsigned long long cycles = 1500;
        double cf = c->battery_kwh * ev_pow_fast(1 - fade, cycles), cn = c->battery_kwh * ev_pow_naive(1 - fade, cycles);
        fprintf(o, "    battery after %llu cycles at %.2f%%/cycle fade: fast power %.4f kWh, naive %.4f kWh (diff %.2e)\n",
                cycles, fade * 100, cf, cn, fabs(cf - cn));
        JNUM("battery_after_cycles_kwh", cf); JNUM("battery_pow_diff", fabs(cf - cn));

        if (S > 0) {
            celeb_ctx cc = { sc, run.sel.idx };
            long qd, qn;
            int cdc = ev_celebrity_dc(S, knows_fn, &cc, &qd), cnv = ev_celebrity_naive(S, knows_fn, &cc, &qn);
            fprintf(o, "    overflow hub (celebrity: everyone forwards to it, it forwards to nobody): station %d; "
                       "D&C %ld queries vs naive %ld\n", cdc >= 0 ? sc->stations[run.sel.idx[cdc]].id : -1, qd, qn);
            JINT("hub_station_id", cdc >= 0 ? sc->stations[run.sel.idx[cdc]].id : -1);
            JINT("celebrity_queries_dc", qd); JINT("celebrity_queries_naive", qn); JBOOL("celebrity_agree", cdc == cnv);
        }

        /* zone-to-zone demand transition matrix P; 4-step propagation P^4 = (P^2)^2 */
        int G = 8, Z = G * G;
        double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
        for (int i = 0; i < sc->nn; i++) {
            if (sc->nodes[i].x < minx) minx = sc->nodes[i].x;
            if (sc->nodes[i].x > maxx) maxx = sc->nodes[i].x;
            if (sc->nodes[i].y < miny) miny = sc->nodes[i].y;
            if (sc->nodes[i].y > maxy) maxy = sc->nodes[i].y;
        }
        int *zone = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn);
        for (int i = 0; i < sc->nn; i++) {
            int zx = (int)((sc->nodes[i].x - minx) / (maxx - minx + 1e-9) * G);
            int zy = (int)((sc->nodes[i].y - miny) / (maxy - miny + 1e-9) * G);
            if (zx >= G) zx = G - 1;
            if (zy >= G) zy = G - 1;
            zone[i] = zy * G + zx;
        }
        size_t ZZ = (size_t)Z * Z;
        double *P = (double *)ev_calloc(ZZ, sizeof(double));
        for (int t = 0; t < sc->nt; t++) P[(size_t)zone[sc->trips[t].src] * Z + zone[sc->trips[t].dst]] += 1;
        for (int r = 0; r < Z; r++) {
            double s = 0;
            for (int q = 0; q < Z; q++) s += P[(size_t)r * Z + q];
            if (s == 0) P[(size_t)r * Z + r] = 1;
            else for (int q = 0; q < Z; q++) P[(size_t)r * Z + q] /= s;
        }
        double *P2 = (double *)ev_malloc(sizeof(double) * ZZ), *P4c = (double *)ev_malloc(sizeof(double) * ZZ);
        double *Q2 = (double *)ev_malloc(sizeof(double) * ZZ), *P4s = (double *)ev_malloc(sizeof(double) * ZZ);
        t0 = ev_now_ms();
        ev_matmul_classic(P, P, P2, Z); ev_matmul_classic(P2, P2, P4c, Z);
        double ms_c = ev_now_ms() - t0;
        t0 = ev_now_ms();
        ev_matmul_strassen(P, P, Q2, Z, 8); ev_matmul_strassen(Q2, Q2, P4s, Z, 8);
        double ms_s = ev_now_ms() - t0;
        double md = 0;
        for (size_t i = 0; i < ZZ; i++) { double dd = fabs(P4c[i] - P4s[i]); if (dd > md) md = dd; }
        int topz = 0;
        double topm = -1;
        for (int q = 0; q < Z; q++) {
            double m = 0;
            for (int r = 0; r < Z; r++) m += P4c[(size_t)r * Z + q];
            if (m > topm) { topm = m; topz = q; }
        }
        fprintf(o, "    demand propagation P^4 over %dx%d zones: classical %.3f ms, Strassen %.3f ms, max |diff| %.2e; "
                   "strongest attractor zone %d (mass %.2f)\n", Z, Z, ms_c, ms_s, md, topz, topm / Z);
        JNUM("matmul_classic_ms", ms_c); JNUM("matmul_strassen_ms", ms_s); JNUM("matmul_max_diff", md);
        JINT("attractor_zone", topz);
        ev_free(zone); ev_free(P); ev_free(P2); ev_free(P4c); ev_free(Q2); ev_free(P4s);
    }
    JEND();

    /* ------------------------------------------------ 3. greedy */
    fprintf(o, "\n[3] Greedy\n");
    JOBJ("greedy");
    {
        int worst = -1;
        for (int q = 0; q < ses.n; q++) if (worst < 0 || ses.st[q].chargers_needed > ses.st[worst].chargers_needed) worst = q;
        if (worst >= 0) {
            const ev_station_load *L = &ses.st[worst];
            fprintf(o, "    interval partitioning: busiest station %d needs %d chargers for %d sessions (max overlap %d, has %d ports)\n",
                    sc->stations[L->station].id, L->chargers_needed, L->sessions, L->depth, sc->stations[L->station].ports);
            fprintf(o, "    activity selection per port: %d of %d sessions servable there; network-wide %ld/%ld\n",
                    L->served, L->sessions, ses.served, ses.sessions);
            JINT("busiest_station_id", sc->stations[L->station].id); JINT("chargers_needed", L->chargers_needed);
            JINT("max_overlap", L->depth); JINT("served_network", ses.served); JINT("sessions_network", ses.sessions);
        }
        if (sc->events_len > 0) {
            t0 = ev_now_ms();
            ev_huff *h = ev_huff_build((const unsigned char *)sc->events, (size_t)sc->events_len);
            unsigned char *bits;
            size_t nb = ev_huff_encode(h, (const unsigned char *)sc->events, (size_t)sc->events_len, &bits);
            unsigned char *dec = (unsigned char *)ev_malloc((size_t)sc->events_len);
            size_t nd = ev_huff_decode(h, bits, nb, dec, (size_t)sc->events_len);
            int rt = nd == (size_t)sc->events_len && memcmp(dec, sc->events, (size_t)sc->events_len) == 0;
            double ms_h = ev_now_ms() - t0;
            fprintf(o, "    Huffman on %ld-event station log: %d symbols, %.3f bits/event, %zu -> %zu bytes (%.1f%%), round-trip %s, %.3f ms\n",
                    sc->events_len, ev_huff_symbols(h), ev_huff_avg_bits(h), (size_t)sc->events_len, (nb + 7) / 8,
                    100.0 * (double)((nb + 7) / 8) / (double)sc->events_len, rt ? "ok" : "FAILED", ms_h);
            JINT("huffman_symbols", ev_huff_symbols(h)); JNUM("huffman_bits_per_event", ev_huff_avg_bits(h));
            JNUM("huffman_ratio", (double)((nb + 7) / 8) / (double)sc->events_len); JBOOL("huffman_roundtrip", rt);
            ev_free(dec); ev_free(bits); ev_huff_free(h);
        }
        fprintf(o, "    Dijkstra: %ld shortest-path trees built for planning\n", ev_router_sssp_runs(run.router));
        if (S >= 2) {
            double *W = (double *)ev_malloc(sizeof(double) * (size_t)S * S);
            int m = S * (S - 1) / 2, e = 0;
            int *eu = (int *)ev_malloc(sizeof(int) * (size_t)m), *evv = (int *)ev_malloc(sizeof(int) * (size_t)m);
            double *ew = (double *)ev_malloc(sizeof(double) * (size_t)m);
            for (int a = 0; a < S; a++) {
                W[(size_t)a * S + a] = 0;
                for (int b = a + 1; b < S; b++) {
                    double k1, k2, x1, x2;
                    int na = sc->stations[run.sel.idx[a]].node, nbn = sc->stations[run.sel.idx[b]].node;
                    int ok1 = ev_router_leg(run.router, na, nbn, &k1, &x1), ok2 = ev_router_leg(run.router, nbn, na, &k2, &x2);
                    double w = EV_INF;
                    if (ok1) w = k1;
                    if (ok2 && k2 < w) w = k2;
                    W[(size_t)a * S + b] = W[(size_t)b * S + a] = w;
                    eu[e] = a; evv[e] = b; ew[e] = w; e++;
                }
            }
            int *par = (int *)ev_malloc(sizeof(int) * (size_t)S), *ch = (int *)ev_malloc(sizeof(int) * (size_t)S), nch;
            t0 = ev_now_ms();
            double wp = ev_prim_dense(S, W, par);
            double ms_p = ev_now_ms() - t0;
            t0 = ev_now_ms();
            double wk = ev_kruskal(S, m, eu, evv, ew, ch, &nch);
            double ms_k = ev_now_ms() - t0;
            fprintf(o, "    grid-backbone MST over %d stations: Prim %.2f km (%.3f ms), Kruskal %.2f km (%.3f ms) -> %s\n",
                    S, wp, ms_p, wk, ms_k, fabs(wp - wk) < 1e-6 ? "agree" : "DISAGREE");
            JNUM("mst_prim_km", wp); JNUM("mst_kruskal_km", wk); JNUM("prim_ms", ms_p); JNUM("kruskal_ms", ms_k);
            ev_free(W); ev_free(eu); ev_free(evv); ev_free(ew); ev_free(par); ev_free(ch);
        }
    }
    JEND();

    /* ------------------------------------------------ 4. dynamic programming */
    fprintf(o, "\n[4] Dynamic programming\n");
    JOBJ("dynamic_programming");
    {
        ev_selection g;
        ev_select(sc, EV_SEL_GREEDY, &g);
        fprintf(o, "    0/1 knapsack station selection: DP value %ld (cost %ld) vs greedy ratio %ld (cost %ld) -> greedy reaches %.1f%%\n",
                run.sel.value, run.sel.cost, g.value, g.cost, run.sel.value ? 100.0 * g.value / run.sel.value : 100.0);
        JINT("knapsack_dp_value", run.sel.value); JINT("knapsack_greedy_value", g.value);
        ev_selection_free(&g);

        long dims[6] = { sc->nn, 64, 64, 64, 64, sc->nn };
        int split[25];
        long long best = ev_matrix_chain(5, dims, split);
        char paren[128];
        ev_matrix_chain_paren(5, split, paren, sizeof paren);
        long long lft = ev_matrix_chain_left(5, dims), rgt = ev_matrix_chain_right(5, dims);
        fprintf(o, "    matrix chain (node->zone, 3 zone transitions, zone->node): optimal %s = %lld mults; left-to-right %lld, right-to-left %lld\n",
                paren, best, lft, rgt);
        JSTR("matrix_chain_order", paren); JINT("matrix_chain_cost", best); JINT("matrix_chain_left", lft);

        /* route adherence: LCS of planned node sequence vs a perturbed GPS trace */
        ev_rng rng;
        ev_rng_seed(&rng, 2026);
        int *plan_nodes = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn * 2);
        int *gps = (int *)ev_malloc(sizeof(int) * (size_t)sc->nn * 4);
        int cnt = 0;
        double adh = 0;
        for (int t = 0; t < sc->nt && cnt < 50; t++) {
            if (!run.plans[t].feasible) continue;
            int len = ev_router_path(run.router, sc->trips[t].src, sc->trips[t].dst, plan_nodes, sc->nn * 2);
            if (len < 3) continue;
            int gl = 0;
            for (int i = 0; i < len; i++) {
                double u = ev_rng_unit(&rng);
                if (u < 0.08) continue;                                      /* GPS dropout */
                gps[gl++] = plan_nodes[i];
                if (u > 0.92) gps[gl++] = (int)ev_rng_range(&rng, 0, sc->nn - 1); /* off-route fix */
            }
            int l = ev_lcs(plan_nodes, len, gps, gl, NULL);
            adh += (double)l / len;
            cnt++;
        }
        if (cnt) adh /= cnt;
        fprintf(o, "    LCS route adherence over %d trips (planned vs simulated GPS): %.1f%%\n", cnt, 100 * adh);
        JNUM("lcs_adherence", adh);
        ev_free(plan_nodes); ev_free(gps);

        if (S > 0) {
            /* optimal BST over selected stations keyed by id, p ~ service value */
            int *ids = (int *)ev_malloc(sizeof(int) * (size_t)S), *ordv = (int *)ev_malloc(sizeof(int) * (size_t)S);
            for (int q = 0; q < S; q++) ordv[q] = run.sel.idx[q];
            g_sc = sc;
            /* sort by station id */
            for (int q = 0; q < S; q++) ids[q] = sc->stations[ordv[q]].id;
            ev_merge_sort(ids, (size_t)S, sizeof(int), cmp_int_asc);
            double *p = (double *)ev_calloc((size_t)S + 1, sizeof(double)), *qq = (double *)ev_calloc((size_t)S + 1, sizeof(double));
            double tot = 0;
            for (int q = 0; q < S; q++)
                for (int r = 0; r < S; r++)
                    if (sc->stations[ordv[r]].id == ids[q]) { p[q + 1] = sc->stations[ordv[r]].value + 1.0; tot += p[q + 1]; }
            for (int q = 1; q <= S; q++) p[q] /= tot;
            double eo = ev_optimal_bst(S, p, qq, NULL), eb = ev_balanced_bst_cost(S, p, qq);
            fprintf(o, "    optimal BST station lookup: expected %.3f probes vs balanced %.3f (%.1f%% fewer)\n",
                    eo, eb, eb > 0 ? 100 * (eb - eo) / eb : 0);
            JNUM("obst_expected_probes", eo); JNUM("balanced_expected_probes", eb);
            ev_free(ids); ev_free(ordv); ev_free(p); ev_free(qq);

            /* maintenance tour: depot = highest-demand node + top-value stations */
            int depot = 0;
            for (int i = 1; i < sc->nn; i++) if (sc->nodes[i].demand > sc->nodes[depot].demand) depot = i;
            int k = S < 9 ? S : 9;
            int *pick = (int *)ev_malloc(sizeof(int) * (size_t)S);
            for (int q = 0; q < S; q++) pick[q] = run.sel.idx[q];
            for (int a = 0; a < k; a++)           /* partial selection of top-k by value */
                for (int b = a + 1; b < S; b++)
                    if (sc->stations[pick[b]].value > sc->stations[pick[a]].value) { int tmp = pick[a]; pick[a] = pick[b]; pick[b] = tmp; }
            int n = k + 1;
            double *D = (double *)ev_malloc(sizeof(double) * (size_t)n * n);
            int pts[16];
            pts[0] = depot;
            for (int q = 0; q < k; q++) pts[q + 1] = sc->stations[pick[q]].node;
            for (int a = 0; a < n; a++)
                for (int b = 0; b < n; b++) {
                    double km, e;
                    D[(size_t)a * n + b] = ev_router_leg(run.router, pts[a], pts[b], &km, &e) ? km : 1e300;
                }
            int tour[24], tb[24];
            t0 = ev_now_ms();
            double hk = ev_tsp_held_karp(n, D, tour);
            double ms_hk = ev_now_ms() - t0;
            t0 = ev_now_ms();
            double bf = ev_tsp_brute(n, D, tb);
            double ms_bf = ev_now_ms() - t0;
            fprintf(o, "    maintenance van TSP (depot + %d stations): Held-Karp %.2f km (%.3f ms), brute force %.2f km (%.3f ms) -> %s\n",
                    k, hk, ms_hk, bf, ms_bf, fabs(hk - bf) < 1e-6 ? "agree" : "DISAGREE");
            fprintf(o, "      tour:");
            for (int q = 0; q <= n && hk < 1e299; q++)
                fprintf(o, " %s", q == 0 || q == n ? "DEPOT" : sc->nodes[pts[tour[q]]].name);
            fprintf(o, "\n");
            JNUM("tsp_held_karp_km", hk); JNUM("tsp_brute_km", bf); JNUM("tsp_hk_ms", ms_hk); JNUM("tsp_brute_ms", ms_bf);
            ev_free(D); ev_free(pick);
        }
        fprintf(o, "    Bellman-Ford / Floyd-Warshall: available as routing strategies (see 'compare')\n");
    }
    JEND();

    /* ------------------------------------------------ 5. backtracking + branch and bound */
    fprintf(o, "\n[5] Backtracking and branch-and-bound\n");
    JOBJ("backtracking_bnb");
    {
        long nodes;
        int first[8];
        long q8 = ev_nqueens_count(8, first, &nodes);
        fprintf(o, "    N-Queens(8): %ld solutions (expected 92) in %ld nodes -> framework %s\n", q8, nodes, q8 == 92 ? "validated" : "BROKEN");
        JINT("nqueens8", q8);

        int *costs = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1)), *take = (int *)ev_malloc(sizeof(int) * (size_t)(sc->ns ? sc->ns : 1));
        unsigned char *in = (unsigned char *)ev_calloc((size_t)(sc->ns ? sc->ns : 1), 1);
        for (int q = 0; q < S; q++) in[run.sel.idx[q]] = 1;
        int m = 0;
        for (int i = 0; i < sc->ns; i++) if (!in[i]) costs[m++] = sc->stations[i].cost;
        long left = c->budget - run.sel.cost;
        long sn;
        int ss = ev_subset_sum(costs, m, left, 2000000, take, &sn);
        int ssdp = ev_subset_sum_dp(costs, m, left);
        fprintf(o, "    subset sum: spend the leftover budget %ld exactly on unselected sites? backtracking=%s (%ld nodes), DP oracle=%s\n",
                left, ss == 1 ? "yes" : ss == 0 ? "no" : "aborted", sn, ssdp == 1 ? "yes" : ssdp == 0 ? "no" : "n/a");
        JINT("leftover_budget", left); JINT("subset_sum_backtracking", ss); JINT("subset_sum_dp", ssdp);
        ev_free(costs); ev_free(take); ev_free(in);

        int n = sc->ns;
        int *w = (int *)ev_malloc(sizeof(int) * (size_t)(n ? n : 1));
        long *v = (long *)ev_malloc(sizeof(long) * (size_t)(n ? n : 1));
        for (int i = 0; i < n; i++) { w[i] = sc->stations[i].cost; v[i] = sc->stations[i].value; }
        long opt = ev_knapsack01(n, w, v, c->budget, NULL);
        JOBJ("bnb");
        for (int md = 0; md < 3; md++) {
            ev_bnb_stats st;
            t0 = ev_now_ms();
            long bv = ev_knapsack_bnb(n, w, v, c->budget, (ev_bnb_mode)md, 0, NULL, &st);
            double ms = ev_now_ms() - t0;
            fprintf(o, "    knapsack B&B %-4s: value %ld (DP %ld, %s), generated %ld, expanded %ld, peak frontier %ld, %.3f ms%s\n",
                    ev_bnb_mode_name((ev_bnb_mode)md), bv, opt, bv == opt ? "optimal" : "SUBOPTIMAL", st.generated,
                    st.expanded, st.max_frontier, ms, st.complete ? "" : " [node limit]");
            JOBJ(ev_bnb_mode_name((ev_bnb_mode)md));
            JINT("value", bv); JINT("generated", st.generated); JINT("expanded", st.expanded);
            JINT("max_frontier", st.max_frontier); JNUM("ms", ms); JBOOL("complete", st.complete);
            JEND();
        }
        JEND();
        ev_free(w); ev_free(v);
    }
    JEND();

    /* ------------------------------------------------ 6. flow */
    fprintf(o, "\n[6] Network flow and matching\n");
    fprintf(o, "    see Capacity line above: Ford-Fulkerson (DFS), Edmonds-Karp (BFS) and bipartite matching "
               "all report %ld/%ld/%d trips served\n", cap.flow_dfs, cap.flow_bfs, cap.matching);
    JOBJ("flow");
    JINT("ff_dfs", cap.flow_dfs); JINT("edmonds_karp", cap.flow_bfs); JINT("matching", cap.matching);
    JINT("trips_needing", cap.trips_needing); JINT("slots", cap.slots_total);
    JEND();

    /* ------------------------------------------------ 7. number theory + strings */
    fprintf(o, "\n[7] Chinese Remainder Theorem and string matching\n");
    JOBJ("crt_strings");
    if (S >= 2) {
        int k = S < 4 ? S : 4;
        long long a[4], m[4];
        fprintf(o, "    maintenance windows of the %d highest-value stations:", k);
        int *pick = (int *)ev_malloc(sizeof(int) * (size_t)S);
        for (int q = 0; q < S; q++) pick[q] = run.sel.idx[q];
        for (int x = 0; x < k; x++)
            for (int y = x + 1; y < S; y++)
                if (sc->stations[pick[y]].value > sc->stations[pick[x]].value) { int tmp = pick[x]; pick[x] = pick[y]; pick[y] = tmp; }
        for (int q = 0; q < k; q++) {
            a[q] = sc->stations[pick[q]].maint_offset_h;
            m[q] = sc->stations[pick[q]].maint_period_h;
            fprintf(o, " #%d(h=%lld mod %lld)", sc->stations[pick[q]].id, a[q], m[q]);
        }
        long long x = 0, M = 0;
        int r = ev_crt(k, a, m, &x, &M);
        if (r == 1) fprintf(o, "\n    CRT: all %d go down together first at hour %lld, then every %lld h (%.1f days) -> stagger them\n", k, x, M, M / 24.0);
        else if (r == 0) fprintf(o, "\n    CRT: the windows never coincide (inconsistent congruences) -> safe schedule\n");
        else fprintf(o, "\n    CRT: invalid input or overflow\n");
        JINT("crt_result", r); JINT("crt_first_hour", r == 1 ? x : -1); JINT("crt_period", r == 1 ? M : -1);
        ev_free(pick);
    }
    if (sc->events_len > 0) {
        const char *pats[2] = { "FOR", "FFFF" };
        if (j) ev_json_arr(j, "matching");
        for (int pi = 0; pi < 2; pi++) {
            const char *p = pats[pi];
            long m = (long)strlen(p);
            ev_match_stats s1, s2, s3, s4;
            double tm[4];
            t0 = ev_now_ms(); ev_match_naive(sc->events, sc->events_len, p, m, NULL, 0, &s1); tm[0] = ev_now_ms() - t0;
            t0 = ev_now_ms(); ev_match_rabin_karp(sc->events, sc->events_len, p, m, NULL, 0, &s2); tm[1] = ev_now_ms() - t0;
            t0 = ev_now_ms(); ev_match_fa(sc->events, sc->events_len, p, m, NULL, 0, &s3); tm[2] = ev_now_ms() - t0;
            t0 = ev_now_ms(); ev_match_kmp(sc->events, sc->events_len, p, m, NULL, 0, &s4); tm[3] = ev_now_ms() - t0;
            int agree = s1.matches == s2.matches && s2.matches == s3.matches && s3.matches == s4.matches;
            fprintf(o, "    pattern \"%s\" (%s): %ld hits | naive %lld cmp %.3f ms | Rabin-Karp %lld cmp %.3f ms | "
                       "automaton %lld steps %.3f ms | KMP %lld cmp %.3f ms -> %s\n",
                    p, pi == 0 ? "fault->offline->reset" : "fault burst", s1.matches, s1.comparisons, tm[0],
                    s2.comparisons, tm[1], s3.comparisons, tm[2], s4.comparisons, tm[3], agree ? "agree" : "DISAGREE");
            if (j) {
                ev_json_obj(j, NULL);
                ev_json_str(j, "pattern", p); ev_json_int(j, "hits", s1.matches); ev_json_bool(j, "agree", agree);
                ev_json_int(j, "naive_cmp", s1.comparisons); ev_json_int(j, "rk_cmp", s2.comparisons);
                ev_json_int(j, "fa_steps", s3.comparisons); ev_json_int(j, "kmp_cmp", s4.comparisons);
                ev_json_end_obj(j);
            }
        }
        if (j) ev_json_end_arr(j);
    }
    JEND();

    JEND();
    ev_capacity_free(&cap);
    ev_sessions_free(&ses);
    ev_run_free(&run);
#undef JOBJ
#undef JEND
#undef JNUM
#undef JINT
#undef JSTR
#undef JBOOL
    return 0;
}
