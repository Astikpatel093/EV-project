// scenario.hpp - everything one planning run needs: road network, existing stations, candidate
// sites, vehicles, trips, booked charging sessions and the configuration.
#pragma once

#include <string>
#include <vector>

#include "ev/core/money.hpp"
#include "ev/models/road_network.hpp"
#include "ev/models/station.hpp"
#include "ev/models/trip.hpp"
#include "ev/models/vehicle.hpp"

namespace ev {

// Where the data came from. Synthetic data is generated and must never be presented as real.
enum class DataSource { Synthetic, RealWorld, Mixed };
const char *to_string(DataSource s);                 // "synthetic", "real", "mixed"
DataSource parse_data_source(const std::string &s);  // throws std::invalid_argument

struct ScenarioConfig {
    std::string name = "scenario";
    DataSource source = DataSource::Synthetic;
    std::string source_note;             // free text: generator command, or the real data source
    Paise budget = 0;                    // installation budget for NEW stations
    double target_soc = 0.8;             // a charging stop fills the battery to this fraction
    double regen_efficiency = 0.6;       // share of potential energy recovered downhill, [0, 1]
    double climb_kwh_per_m = 0.0045;     // energy per metre of climb for the reference vehicle
    double reference_consumption = 0.18; // kWh/km of the reference vehicle (scales the two above)
    double speed_kmh = 40;               // used when a road has no travel time
    double battery_step_kwh = 0.25;      // battery discretisation of the exact planner
    double max_detour_km = 15;           // capacity analysis: acceptable detour to a station
    double grid_kw = 120;                // grid supply for the surge analysis (kWh per hour)
    int window_min = 120;                // capacity window
    int session_min = 30;                // nominal session length for slot counting
    int horizon_h = 24;                  // hours in the load profile
    double min_session_min = 5;          // plug-in / hand-over time added to every session
};

// Throws std::invalid_argument naming the bad key.
void validate(const ScenarioConfig &c);

struct Scenario {
    ScenarioConfig config;
    RoadNetwork network;
    std::vector<ChargingStation> stations;      // existing
    std::vector<CandidateStation> candidates;   // possible new sites
    std::vector<ElectricVehicle> vehicles;
    std::vector<TripRequest> trips;
    std::vector<ChargingSession> sessions;
    std::string events;                         // optional station event log (one char per event)

    int vehicle_index(const std::string &id) const;   // -1 if unknown
    int station_index(int id) const;                  // existing station with this id, -1 if none
    int candidate_index(int id) const;                // candidate with this id, -1 if none

    // Cross-reference checks: station/candidate nodes exist, ids are unique across stations and
    // candidates, trips name known vehicles and nodes, sessions name known stations, start
    // charges are within the vehicle's capacity. Throws std::invalid_argument.
    void validate() const;
};

} // namespace ev
