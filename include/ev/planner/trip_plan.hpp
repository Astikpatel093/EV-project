// trip_plan.hpp - charging sites available to the planners, and the trip plan they return.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "ev/models/scenario.hpp"

namespace ev {

// One place where vehicles can charge during a planning run: an existing station, or a candidate
// site that the placement step decided to build.
struct ChargerSite {
    enum class Kind { Existing, New };
    Kind kind = Kind::Existing;
    int source = 0;           // index into Scenario::stations (Existing) or Scenario::candidates (New)
    int id = 0;               // station / candidate id
    std::string name;
    int node = 0;             // node POSITION in the road network
    double power_kw = 0;
    int ports = 1;
    std::string connector;
    bool available = true;    // offline / maintenance stations cannot be used
};

// Existing stations first (in file order), then the built candidates (ascending candidate index).
// `ports_override` maps a station/candidate id to a different port count (what-if editing).
std::vector<ChargerSite> make_charger_sites(const Scenario &s, const std::vector<int> &built_candidates,
                                            const std::map<int, int> &ports_override = {});

// Can vehicle v use this site at all?
bool usable_by(const ChargerSite &site, const ElectricVehicle &v);

enum class PlanStatus {
    ZeroLength,           // origin == destination: nothing to drive (feasible)
    Direct,               // feasible without charging (feasible)
    WithCharging,         // feasible with one or more charging stops (feasible)
    NoRoadConnection,     // no road path at all between the two nodes
    StartBelowReserve,    // battery already below the vehicle's reserve at departure
    OutOfRange,           // too far for the battery and no usable charger within reach
    ChargingInsufficient  // chargers are reachable but cannot close the gaps to the destination
};
const char *to_string(PlanStatus s);

struct ChargingStop {
    int site = 0;              // index into the ChargerSite list used for planning
    int station_id = 0;
    std::string name;
    int node = 0;              // node position
    double arrive_min = 0;     // clock time on arrival (minutes after midnight)
    double arrive_kwh = 0;
    double charge_min = 0;     // time spent charging (excluding any queue)
    double depart_kwh = 0;
    std::size_t route_pos = 0; // index of the stop's node inside TripPlan::route
};

struct TripPlan {
    int trip_id = -1;
    std::string vehicle_id;
    PlanStatus status = PlanStatus::NoRoadConnection;
    std::string explanation;          // one-paragraph reason, always filled
    bool feasible() const {
        return status == PlanStatus::Direct || status == PlanStatus::WithCharging || status == PlanStatus::ZeroLength;
    }

    // facts about the plain shortest road route (ignoring the battery)
    double shortest_km = -1;          // -1 when no road path exists
    double shortest_kwh = 0;          // energy along it for this vehicle
    bool shortest_is_feasible = false;  // could it be driven without charging?
    bool needs_charge = false;        // the direct trip is not possible on the starting charge

    // the plan (when feasible)
    std::vector<int> route;           // node positions from origin to destination
    std::vector<int> route_arcs;      // EnergyModel arc ids along the route
    std::vector<ChargingStop> stops;
    double distance_km = 0, energy_kwh = 0, drive_min = 0, charge_min = 0;
    double depart_min = 0, arrive_min = 0, start_kwh = 0, arrive_kwh = 0, min_kwh = 0;
};

} // namespace ev
