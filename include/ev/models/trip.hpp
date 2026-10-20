// trip.hpp - trip requests and charging sessions (inputs). Trip *plans* (outputs) are defined in
// ev/planner/trip_plan.hpp.
#pragma once

#include <optional>
#include <string>

namespace ev {

struct TripRequest {
    int id = 0;
    std::string vehicle_id;                 // must name a vehicle in the scenario
    int from = 0, to = 0;                   // road node ids (may be equal: a zero-length trip)
    double depart_min = 0;                  // minutes after midnight, >= 0
    std::optional<double> start_charge_kwh; // overrides the vehicle's current charge when present
};

// A booked charging session at a station, used by the scheduling algorithms.
struct ChargingSession {
    int id = 0;
    int station_id = 0;                     // an existing station or a candidate site id
    double start_min = 0, end_min = 0;      // half-open [start, end), end > start
};

} // namespace ev
