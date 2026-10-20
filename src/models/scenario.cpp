// scenario.cpp - configuration and cross-reference validation.
#include "ev/models/scenario.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace ev {

const char *to_string(DataSource s) {
    switch (s) {
    case DataSource::Synthetic: return "synthetic";
    case DataSource::RealWorld: return "real";
    default: return "mixed";
    }
}

DataSource parse_data_source(const std::string &s) {
    if (s == "synthetic") return DataSource::Synthetic;
    if (s == "real") return DataSource::RealWorld;
    if (s == "mixed") return DataSource::Mixed;
    throw std::invalid_argument("data_source must be synthetic, real or mixed");
}

void validate(const ScenarioConfig &c) {
    auto bad = [](const std::string &key, const std::string &why) { throw std::invalid_argument(key + " " + why); };
    if (c.budget < 0) bad("budget_inr", "must not be negative");
    if (!(c.target_soc > 0 && c.target_soc <= 1)) bad("target_soc", "must be in (0, 1]");
    if (!(c.regen_efficiency >= 0 && c.regen_efficiency <= 1)) bad("regen_efficiency", "must be in [0, 1]");
    if (!(c.climb_kwh_per_m >= 0) || !std::isfinite(c.climb_kwh_per_m)) bad("climb_kwh_per_m", "must be >= 0");
    if (!(c.reference_consumption > 0) || !std::isfinite(c.reference_consumption)) bad("reference_consumption", "must be > 0");
    if (!(c.speed_kmh > 0) || !std::isfinite(c.speed_kmh)) bad("speed_kmh", "must be > 0");
    if (!(c.battery_step_kwh > 0) || !std::isfinite(c.battery_step_kwh)) bad("battery_step_kwh", "must be > 0");
    if (!(c.max_detour_km >= 0)) bad("max_detour_km", "must be >= 0");
    if (!(c.grid_kw >= 0)) bad("grid_kw", "must be >= 0");
    if (c.window_min < 1) bad("window_min", "must be >= 1");
    if (c.session_min < 1) bad("session_min", "must be >= 1");
    if (c.horizon_h < 1 || c.horizon_h > 24 * 14) bad("horizon_h", "must be in [1, 336]");
    if (!(c.min_session_min >= 0)) bad("min_session_min", "must be >= 0");
}

int Scenario::vehicle_index(const std::string &id) const {
    for (std::size_t i = 0; i < vehicles.size(); i++)
        if (vehicles[i].id == id) return static_cast<int>(i);
    return -1;
}

int Scenario::station_index(int id) const {
    for (std::size_t i = 0; i < stations.size(); i++)
        if (stations[i].id == id) return static_cast<int>(i);
    return -1;
}

int Scenario::candidate_index(int id) const {
    for (std::size_t i = 0; i < candidates.size(); i++)
        if (candidates[i].id == id) return static_cast<int>(i);
    return -1;
}

void Scenario::validate() const {
    ev::validate(config);
    std::unordered_set<int> site_ids;
    for (const ChargingStation &s : stations) {
        ev::validate(s);
        if (!site_ids.insert(s.id).second) throw std::invalid_argument("duplicate station id " + std::to_string(s.id));
        if (!network.has_node(s.node))
            throw std::invalid_argument("station " + std::to_string(s.id) + " is at unknown node " + std::to_string(s.node));
    }
    for (const CandidateStation &c : candidates) {
        ev::validate(c);
        if (!site_ids.insert(c.id).second)
            throw std::invalid_argument("candidate id " + std::to_string(c.id) + " is already used by another station or candidate");
        if (!network.has_node(c.node))
            throw std::invalid_argument("candidate " + std::to_string(c.id) + " is at unknown node " + std::to_string(c.node));
    }
    std::unordered_set<std::string> vids;
    for (const ElectricVehicle &v : vehicles) {
        ev::validate(v);
        if (!vids.insert(v.id).second) throw std::invalid_argument("duplicate vehicle id " + v.id);
    }
    std::unordered_set<int> tids;
    for (const TripRequest &t : trips) {
        const std::string who = "trip " + std::to_string(t.id);
        if (!tids.insert(t.id).second) throw std::invalid_argument("duplicate trip id " + std::to_string(t.id));
        int vi = vehicle_index(t.vehicle_id);
        if (vi < 0) throw std::invalid_argument(who + " uses unknown vehicle '" + t.vehicle_id + "'");
        if (!network.has_node(t.from)) throw std::invalid_argument(who + " starts at unknown node " + std::to_string(t.from));
        if (!network.has_node(t.to)) throw std::invalid_argument(who + " ends at unknown node " + std::to_string(t.to));
        if (!std::isfinite(t.depart_min) || t.depart_min < 0) throw std::invalid_argument(who + ": departure time must be >= 0");
        if (t.start_charge_kwh) {
            const ElectricVehicle &v = vehicles[static_cast<std::size_t>(vi)];
            if (!std::isfinite(*t.start_charge_kwh) || *t.start_charge_kwh < 0)
                throw std::invalid_argument(who + ": start charge must not be negative");
            if (*t.start_charge_kwh > v.battery_kwh)
                throw std::invalid_argument(who + ": start charge exceeds the battery capacity of vehicle " + v.id);
        }
    }
    std::unordered_set<int> sids;
    for (const ChargingSession &s : sessions) {
        const std::string who = "session " + std::to_string(s.id);
        if (!sids.insert(s.id).second) throw std::invalid_argument("duplicate session id " + std::to_string(s.id));
        if (station_index(s.station_id) < 0 && candidate_index(s.station_id) < 0)
            throw std::invalid_argument(who + " refers to unknown station " + std::to_string(s.station_id));
        if (!std::isfinite(s.start_min) || !std::isfinite(s.end_min) || s.start_min < 0)
            throw std::invalid_argument(who + ": times must be finite and >= 0");
        if (!(s.end_min > s.start_min)) throw std::invalid_argument(who + " must end after it starts");
    }
}

} // namespace ev
