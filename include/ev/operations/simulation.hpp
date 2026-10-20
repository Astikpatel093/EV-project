// simulation.hpp - discrete-event simulation of one day with the real port counts.
//
// The planner assumes a free port at every stop. The simulation replays the day in time order:
// events (arrive at a charger, finish charging) come off a binary min-heap ordered by
// (time, sequence number); a vehicle that finds every port busy joins the site's FIFO queue and
// its remaining itinerary shifts later. O(E log E) for E events.
// Driver policies when all ports are busy:
//   Wait    join the queue
//   Divert  if the estimated wait exceeds `divert_after_min`, drive to the reachable charger with
//           the smallest (drive time + estimated wait), provided the vehicle can reach it above its
//           reserve and continue from it after charging; at most one switch per planned stop.
#pragma once

#include <vector>

#include "ev/analysis/pipeline.hpp"

namespace ev {

enum class SegmentKind { Drive = 0, Wait = 1, Charge = 2 };
enum class DriverPolicy { Wait = 0, Divert = 1 };

struct SimOptions {
    DriverPolicy policy = DriverPolicy::Wait;
    double divert_after_min = 20;
};

struct SimSegment {
    int vehicle = 0;               // index into SimResult::trip
    SegmentKind kind = SegmentKind::Drive;
    double t0 = 0, t1 = 0;         // minutes since midnight (may exceed 1440)
    int p0 = 0, p1 = 0;            // drive: index range into the vehicle's path; wait/charge: p0 == p1
    int site = -1;                 // charger site for wait/charge, -1 for drive
    double e0 = 0, e1 = 0;         // battery kWh at t0 and t1
};

struct SimSite {
    int site = 0;
    int sessions = 0;              // charging sessions served
    int waited = 0;                // sessions that had to queue
    double wait_min = 0;           // total minutes in the queue
    int max_queue = 0;
    int diverted_away = 0;         // drivers who left for another charger
};

struct SimResult {
    std::vector<int> trip;         // per simulated vehicle: index into PlanningRun::plans (feasible plans only)
    std::vector<double> battery;   // per simulated vehicle: capacity in kWh
    std::vector<int> path_off;     // nv+1 offsets into path
    std::vector<int> path;         // concatenated node positions
    std::vector<SimSegment> seg;   // grouped by vehicle, in time order
    std::vector<int> seg_off;      // nv+1
    std::vector<SimSite> sites;    // one per charger site of the run
    long events = 0;
    int diverted = 0;              // station switches
    int delayed = 0;               // vehicles that queued at least once
    double total_wait = 0, max_wait = 0, mean_extra_min = 0;
    double end_min = 0;            // last arrival
    double ms = 0;
};

SimResult simulate_day(PlanningRun &run, const SimOptions &options = {});

} // namespace ev
