/* ev_sim.h - discrete-event simulation of one day on the network.
 *
 * The planner assumes a free port at every stop. The simulation replays the day in time
 * order with the real port counts: events (arrive at a station, finish charging) come off a
 * binary min-heap; a vehicle that finds every port busy joins that station's FIFO queue, and
 * its whole remaining itinerary shifts later. Complexity O(E log E) for E events.
 *
 * Output is a per-vehicle timeline of segments (drive / wait / charge) that a viewer can play
 * back at any speed, plus per-station queue statistics.
 */
#ifndef EV_SIM_H
#define EV_SIM_H

#include "ev_plan.h"

typedef enum { EV_SEG_DRIVE = 0, EV_SEG_WAIT = 1, EV_SEG_CHARGE = 2 } ev_seg_kind;

typedef struct {
    int vehicle;       /* index into ev_sim.trip (simulated vehicles) */
    int kind;          /* ev_seg_kind */
    double t0, t1;     /* minutes since midnight of the simulated day (may exceed 1440) */
    int p0, p1;        /* drive: index range into the vehicle's path; wait/charge: p0 == p1 */
    int station;       /* station index for wait/charge, -1 for drive */
    double e0, e1;     /* battery energy (kWh) at t0 and t1 */
} ev_sim_seg;

typedef struct {
    int station;       /* station index */
    int sessions;      /* charging sessions served */
    int waited;        /* sessions that had to queue */
    double wait_min;   /* total minutes spent queueing */
    int max_queue;     /* longest queue observed */
    int diverted_away; /* drivers who left for another station instead of queueing */
} ev_sim_station;

/* What a driver does when every port is busy. */
typedef enum { EV_POLICY_WAIT = 0, EV_POLICY_DIVERT = 1 } ev_sim_policy;

typedef struct {
    ev_sim_policy policy;
    double divert_after_min;  /* DIVERT: switch when the estimated wait exceeds this (default 20) */
} ev_sim_opts;

typedef struct {
    int nv;            /* simulated vehicles (= feasible trips) */
    int *trip;         /* nv: scenario trip index of each vehicle */
    int *path_off;     /* nv+1: offsets into path */
    int *path;         /* concatenated node paths */
    int nseg;
    ev_sim_seg *seg;   /* grouped by vehicle, in time order */
    int *seg_off;      /* nv+1 */
    int nst;
    ev_sim_station *st;
    long events;
    int diverted;             /* station switches made by drivers (DIVERT policy) */
    int delayed;              /* vehicles that queued at least once */
    double total_wait, max_wait, mean_extra_min;
    double end_min;           /* last arrival */
    double ms;
} ev_sim;

int  ev_simulate(ev_router *r, const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans, ev_sim *out);
/* opts may be NULL (= wait in line). A diverting driver picks the built station that minimises
 * drive time + estimated wait among those reachable on the current battery and from which the
 * next stop (or destination) is reachable after charging; at most one switch per planned stop. */
int  ev_simulate_ex(ev_router *r, const ev_scenario *sc, const ev_selection *sel, const ev_plan *plans,
                    const ev_sim_opts *opts, ev_sim *out);
void ev_sim_free(ev_sim *s);

/* Node path of a plan (origin, stops, destination). leg_end (may be NULL, size n_stops+1)
 * receives the index in out[] where each leg ends. Returns node count, 0 on failure. */
int ev_plan_path(ev_router *r, const ev_scenario *sc, const ev_plan *p, int *out, int cap, int *leg_end);

#endif
