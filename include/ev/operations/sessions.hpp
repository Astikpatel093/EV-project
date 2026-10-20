// sessions.hpp - charging-session scheduling and grid load.
//
// Two questions, two greedy algorithms (see greedy.hpp):
//  * How many bays does a station need so that EVERY session is served?  -> interval partitioning
//  * With the bays it actually has, how many sessions can be served?      -> activity selection,
//    repeated once per bay on the sessions still unserved (a greedy heuristic for several bays:
//    optimal for one bay, not guaranteed optimal for k > 1 - stated in the docs).
// Planned stops become sessions [arrival, arrival + max(charge time, min_session_min)).
// Charging energy is binned per hour; the maximum subarray of (load - grid supply) gives the
// worst consecutive overload window.
#pragma once

#include <vector>

#include "ev/algorithms/greedy.hpp"
#include "ev/analysis/pipeline.hpp"

namespace ev {

struct SiteLoad {
    int site = 0;              // index into PlanningRun::sites
    int sessions = 0;
    int chargers_needed = 0;   // interval partitioning
    int depth = 0;             // maximum overlap (must equal chargers_needed)
    int served = 0;            // with the installed ports (repeated activity selection)
};

struct SessionAnalysis {
    std::vector<SiteLoad> sites;      // one per site of the run
    int overloaded = 0;               // sites needing more bays than they have
    long sessions = 0, served = 0;
    std::vector<double> hourly_kwh;   // horizon_h bins
    int surge_lo = -1, surge_hi = -1; // worst overload window (hours), -1 when none
    double surge_excess_kwh = 0;
};
SessionAnalysis analyze_sessions(const PlanningRun &run);

// Booked sessions from sessions.csv at one station (existing or candidate id).
struct StationSchedule {
    int station_id = 0;
    int ports = 0;
    std::vector<ChargingSession> sessions;
    std::vector<std::size_t> one_bay;   // indices into `sessions` chosen by activity selection
    PartitionResult bays;               // interval partitioning of all sessions
    int depth = 0;
    std::vector<std::size_t> served_with_ports;   // repeated activity selection over `ports` bays
};
// Throws std::invalid_argument for an unknown station id.
StationSchedule schedule_station(const Scenario &s, int station_id);

} // namespace ev
