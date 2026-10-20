/* ev_model.h - typed records for the EV charging domain and the scenario loader.
 *
 * Scenario directory layout (all CSV with a header row; '#' lines are comments):
 *   scenario.cfg   key = value pairs (see ev_config)
 *   nodes.csv      id,name,x_km,y_km,elev_m,demand
 *   edges.csv      u,v,km,capacity              (undirected road segments; u,v are node ids)
 *   stations.csv   id,node,cost,ports,power_kw[,maint_period_h,maint_offset_h]
 *   trips.csv      id,src,dst,depart_min,soc0   (soc0 = initial state of charge in (0,1])
 *   events.txt     optional station event log, one character per event
 */
#ifndef EV_MODEL_H
#define EV_MODEL_H

#include <stddef.h>
#include "ev_graph.h"

#define EV_NAME_LEN 32

typedef struct { int id; char name[EV_NAME_LEN]; double x, y, elev; int demand; } ev_node;
typedef struct { int u, v; double km; int capacity; } ev_edge;      /* u,v = node INDICES */
typedef struct {
    int id, node;              /* node = node INDEX */
    int cost;                  /* installation cost (budget units, e.g. lakh INR) */
    int ports;                 /* simultaneous charging sessions */
    double power_kw;           /* per-port charging power */
    int maint_period_h, maint_offset_h; /* periodic maintenance window (hour = offset mod period) */
    int value;                 /* computed service value (trip traversals, weighted) */
} ev_station;
typedef struct { int id, src, dst; int depart_min; double soc0; } ev_trip; /* src,dst = node INDICES */

typedef struct {
    char name[64];
    int budget;
    double battery_kwh;     /* usable pack size */
    double cons_kwh_km;     /* flat-road consumption */
    double reserve_frac;    /* never plan below this SoC */
    double target_frac;     /* charge-to level at a station */
    double regen_eff;       /* fraction of potential energy recovered downhill, [0,1] */
    double elev_kwh_per_m;  /* energy per metre of climb */
    double speed_kmh;       /* average speed for timing */
    double max_detour_km;   /* eligibility for capacity allocation */
    double grid_kw;         /* grid supply per hour for surge analysis (kWh per hour) */
    int window_min;         /* capacity-allocation window */
    int session_min;        /* nominal session length for slot counting */
    int horizon_h;          /* hours in the load profile */
} ev_config;

typedef struct {
    ev_config cfg;
    int nn, ne, ns, nt;
    ev_node *nodes;
    ev_edge *edges;
    ev_station *stations;
    ev_trip *trips;
    char *events;
    long events_len;
    /* domain index: node ids sorted (merge sort) for O(log n) binary search lookup */
    int *idx_ids, *idx_pos;
    /* road network: 2*ne directed arcs; arc k uses arc_src[k] -> arc_dst[k] */
    int na;
    int *arc_src, *arc_dst;
    double *arc_km, *arc_kwh;
    ev_graph *g_km, *g_kwh;  /* same arc numbering, weights = km and kWh */
    int values_ready;
} ev_scenario;

/* In-memory scenario sources (NUL-terminated file contents). events may be NULL. */
typedef struct {
    const char *label;  /* used in error messages, e.g. "data/medium" */
    const char *cfg, *nodes, *edges, *stations, *trips, *events;
} ev_sources;

void ev_config_defaults(ev_config *c);
/* Load and validate. Returns 0 on success; on failure fills err and returns -1. */
int  ev_scenario_load(const char *dir, ev_scenario *sc, char *err, size_t errcap);
/* Same validation, from text already in memory (the file loader reads the files, then calls this). */
int  ev_scenario_load_mem(const ev_sources *src, ev_scenario *sc, char *err, size_t errcap);
void ev_scenario_free(ev_scenario *sc);
/* Node index for an external id via the binary-search index, -1 if absent. */
int  ev_node_index(const ev_scenario *sc, int id);
/* Energy (kWh) to traverse u->v given distance and elevation change. */
double ev_arc_energy(const ev_config *c, double km, double dz_m);

#endif
