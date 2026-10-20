/* ev_plan.h - station selection, energy-feasible routing, capacity and session analysis.
 *
 * Pipeline: values -> select stations (budget knapsack) -> router (SSSP/APSP)
 *           -> per-trip plan over the "charging leg graph" -> KPIs
 *           -> capacity allocation (max-flow / bipartite matching)
 *           -> charger sessions (interval partitioning / activity selection) -> surge window
 */
#ifndef EV_PLAN_H
#define EV_PLAN_H

#include <stddef.h>
#include "ev_model.h"

typedef enum { EV_SEL_GREEDY = 0, EV_SEL_DP, EV_SEL_BNB_LC, EV_SEL_BNB_FIFO, EV_SEL_BNB_LIFO, EV_SEL_COUNT } ev_sel_mode;
typedef enum { EV_ROUTE_DIJKSTRA = 0, EV_ROUTE_FLOYD, EV_ROUTE_BELLMAN, EV_ROUTE_COUNT } ev_route_mode;

const char *ev_sel_name(ev_sel_mode m);
const char *ev_route_name(ev_route_mode m);
int ev_sel_parse(const char *s);    /* -1 if unknown */
int ev_route_parse(const char *s);  /* -1 if unknown */

/* ---------------------------------------------------------------- selection */
typedef struct {
    ev_sel_mode mode;
    int n;          /* number of selected stations */
    int *idx;       /* station indices, ascending */
    long value, cost;
    double ms;
    size_t peak_bytes;
    long search_nodes;   /* BnB nodes generated (0 otherwise) */
    int optimal;         /* 1 if provably optimal for the knapsack objective */
    int over_budget;     /* locked sites alone cost more than the budget */
} ev_selection;

/* Weighted count of shortest-path trip traversals per station node (trips that need
 * a charge weigh 4, others 1). Idempotent. */
void ev_station_values(ev_scenario *sc);
int  ev_select(ev_scenario *sc, ev_sel_mode mode, ev_selection *sel);

/* What-if constraints on selection (station indices). Locked sites are always built and their
 * cost comes off the budget first; banned sites are never built. */
typedef struct { int nlock, nban; const int *lock, *ban; } ev_constraints;
int  ev_select_ex(ev_scenario *sc, ev_sel_mode mode, const ev_constraints *con, ev_selection *sel);
void ev_selection_free(ev_selection *sel);

/* ---------------------------------------------------------------- routing */
typedef struct ev_router ev_router;

/* stations may be NULL/0 for a plain router. */
ev_router *ev_router_create(const ev_scenario *sc, ev_route_mode mode, const int *station_idx, int ns);
/* Leg a->b along the mode's path (km-shortest for Dijkstra/Floyd, kWh-minimal for Bellman).
 * Returns 1 if reachable. */
int    ev_router_leg(ev_router *r, int a, int b, double *km, double *kwh);
int    ev_router_path(ev_router *r, int a, int b, int *out, int cap); /* node count, 0 if none */
size_t ev_router_bytes(const ev_router *r);
long   ev_router_sssp_runs(const ev_router *r);
void   ev_router_free(ev_router *r);

/* ---------------------------------------------------------------- trip plans */
#define EV_MAX_STOPS 12
typedef struct {
    int trip;
    int feasible;
    int needs_charge;            /* direct leg exceeds initial usable energy */
    double km, kwh, direct_km;
    double depart_min, arrive_min, charge_min;
    int n_stops;
    int stop[EV_MAX_STOPS];      /* station indices */
    double stop_arrive_min[EV_MAX_STOPS], stop_charge_min[EV_MAX_STOPS], stop_arrive_kwh[EV_MAX_STOPS];
} ev_plan;

typedef struct {
    int trips, feasible, need_charge, charged_ok;
    double feasible_pct, avg_km, avg_kwh, avg_stops, avg_charge_min, avg_detour_km, avg_travel_min;
} ev_kpi;

int  ev_plan_trip(ev_router *r, const ev_scenario *sc, int t, ev_plan *p);
void ev_plan_all(ev_router *r, const ev_scenario *sc, ev_plan *plans, ev_kpi *kpi);

/* ---------------------------------------------------------------- capacity */
typedef struct {
    int trips_needing;           /* trips whose plan has >= 1 stop, or that need but failed */
    long slots_total;            /* sum over stations of ports * floor(window/session) */
    long flow_dfs, flow_bfs;     /* Ford-Fulkerson (DFS) and Edmonds-Karp (BFS) */
    long aug_dfs, aug_bfs;
    int matching;                /* bipartite trips <-> port-slots */
    double ms_dfs, ms_bfs, ms_match;
    int bottleneck_station;      /* station index with the most rejected demand (-1 none) */
    int *assigned;               /* per selected station: trips allocated by max-flow */
} ev_capacity;

int  ev_capacity_analyze(ev_router *r, const ev_scenario *sc, const ev_selection *sel,
                         const ev_plan *plans, ev_capacity *out);
void ev_capacity_free(ev_capacity *c);

/* ---------------------------------------------------------------- sessions */
typedef struct {
    int station;         /* station index */
    int sessions;        /* planned charging sessions */
    int chargers_needed; /* interval partitioning result */
    int depth;           /* max overlap (must equal chargers_needed) */
    int served;          /* sessions served with the real port count (repeated activity selection) */
} ev_station_load;

typedef struct {
    int n;                    /* = selection size */
    ev_station_load *st;
    int overloaded;           /* stations with chargers_needed > ports */
    long sessions, served;
    double *hourly_kwh;       /* horizon_h bins */
    long surge_lo, surge_hi;  /* worst consecutive overload window (hours), -1 if none */
    double surge_excess_kwh;
} ev_sessions;

int  ev_sessions_analyze(const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans, ev_sessions *out);
void ev_sessions_free(ev_sessions *s);

/* ---------------------------------------------------------------- full pipeline */
typedef struct {
    ev_sel_mode sel_mode;
    ev_route_mode route_mode;
    ev_selection sel;
    ev_router *router;
    ev_plan *plans;
    ev_kpi kpi;
    double ms_values, ms_select, ms_router, ms_plan;
    size_t peak_select, peak_route;
} ev_run;

int  ev_run_pipeline(ev_scenario *sc, ev_sel_mode sm, ev_route_mode rm, ev_run *run);
int  ev_run_pipeline_ex(ev_scenario *sc, ev_sel_mode sm, ev_route_mode rm, const ev_constraints *con, ev_run *run);
void ev_run_free(ev_run *run);

#endif
