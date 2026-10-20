// station_vehicle.cpp - validation and helpers for stations, candidates and vehicles.
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "ev/models/station.hpp"
#include "ev/models/vehicle.hpp"

namespace ev {

static std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(const std::string &s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

const char *to_string(StationStatus s) {
    switch (s) {
    case StationStatus::Available: return "available";
    case StationStatus::Offline: return "offline";
    default: return "maintenance";
    }
}

StationStatus parse_station_status(const std::string &text) {
    std::string t = lower(trim(text));
    if (t == "available" || t == "open" || t == "online") return StationStatus::Available;
    if (t == "offline" || t == "closed" || t == "out_of_service") return StationStatus::Offline;
    if (t == "maintenance") return StationStatus::Maintenance;
    throw std::invalid_argument("unknown station status '" + text + "' (use available, offline or maintenance)");
}

std::vector<std::string> split_connectors(const std::string &list) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : list) {
        if (c == '|' || c == ';' || c == '/') {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::string t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

bool connectors_compatible(const std::string &station_list, const std::string &vehicle_connector) {
    std::string v = lower(trim(vehicle_connector));
    std::vector<std::string> s = split_connectors(station_list);
    if (v.empty() || s.empty()) return true;
    for (const std::string &c : s)
        if (lower(c) == v) return true;
    return false;
}

static void check_common(const std::string &what, int id, double power_kw, int ports, int period, int offset) {
    const std::string who = what + " " + std::to_string(id);
    if (!std::isfinite(power_kw) || power_kw <= 0) throw std::invalid_argument(who + ": charging power must be positive");
    if (ports < 1) throw std::invalid_argument(who + ": needs at least one port");
    if (period < 0) throw std::invalid_argument(who + ": maintenance period must not be negative");
    if (period > 0 && (offset < 0 || offset >= period))
        throw std::invalid_argument(who + ": maintenance offset must be in [0, period)");
}

void validate(const ChargingStation &s) {
    check_common("station", s.id, s.power_kw, s.ports, s.maint_period_h, s.maint_offset_h);
    if (s.install_cost < 0) throw std::invalid_argument("station " + std::to_string(s.id) + ": installation cost must not be negative");
}

void validate(const CandidateStation &c) {
    check_common("candidate", c.id, c.power_kw, c.ports, c.maint_period_h, c.maint_offset_h);
    if (c.install_cost <= 0) throw std::invalid_argument("candidate " + std::to_string(c.id) + ": installation cost must be positive");
    if (c.benefit && *c.benefit < 0) throw std::invalid_argument("candidate " + std::to_string(c.id) + ": benefit must not be negative");
    if (c.benefit && *c.benefit > 1000000000000LL)
        throw std::invalid_argument("candidate " + std::to_string(c.id) + ": benefit is unrealistically large (over 10^12)");
    if (c.suitability && (!std::isfinite(*c.suitability) || *c.suitability < 0 || *c.suitability > 1))
        throw std::invalid_argument("candidate " + std::to_string(c.id) + ": suitability must be in [0, 1]");
}

void validate(const ElectricVehicle &v) {
    const std::string who = "vehicle " + v.id;
    if (v.id.empty()) throw std::invalid_argument("vehicle id must not be empty");
    if (!std::isfinite(v.battery_kwh) || v.battery_kwh <= 0) throw std::invalid_argument(who + ": battery capacity must be positive");
    if (!std::isfinite(v.consumption_kwh_per_km) || v.consumption_kwh_per_km <= 0)
        throw std::invalid_argument(who + ": energy consumption must be positive (a vehicle cannot drive for free)");
    if (!std::isfinite(v.charge_kwh) || v.charge_kwh < 0) throw std::invalid_argument(who + ": current charge must not be negative");
    if (v.charge_kwh > v.battery_kwh) throw std::invalid_argument(who + ": current charge exceeds the battery capacity");
    if (!std::isfinite(v.reserve_kwh) || v.reserve_kwh < 0 || v.reserve_kwh >= v.battery_kwh)
        throw std::invalid_argument(who + ": minimum reserve must be in [0, capacity)");
    if (!std::isfinite(v.max_charge_kw) || v.max_charge_kw < 0) throw std::invalid_argument(who + ": charging limit must not be negative");
}

} // namespace ev
