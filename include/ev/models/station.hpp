// station.hpp - existing charging stations and candidate sites for new ones.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ev/core/money.hpp"

namespace ev {

enum class StationStatus { Available, Offline, Maintenance };
const char *to_string(StationStatus s);                 // "available", "offline", "maintenance"
StationStatus parse_station_status(const std::string &text);   // case-insensitive; throws std::invalid_argument

// Connector lists are written like "CCS2|Type2". An empty list means "not recorded".
std::vector<std::string> split_connectors(const std::string &list);
// True when the vehicle's connector appears in the station's list (case-insensitive). If either
// side is not recorded the pair is treated as compatible - an assumption stated in the docs.
bool connectors_compatible(const std::string &station_list, const std::string &vehicle_connector);

// A station that already exists on the network.
struct ChargingStation {
    int id = 0;
    std::string name;
    int node = 0;                      // road node id where it is located
    double power_kw = 0;               // per port, > 0
    int ports = 1;                     // simultaneous sessions, >= 1
    std::string connector;             // e.g. "CCS2|Type2"
    StationStatus status = StationStatus::Available;
    Paise install_cost = 0;            // historical cost (not part of the new-installation budget)
    int maint_period_h = 0;            // optional maintenance cycle (0 = none), hour ≡ offset (mod period)
    int maint_offset_h = 0;
};

// A location where a new station could be installed.
struct CandidateStation {
    int id = 0;
    std::string name;
    int node = 0;                      // road node id
    Paise install_cost = 0;            // > 0
    std::optional<std::int64_t> benefit;     // expected benefit / demand-coverage score (>= 0);
                                             // when absent EV33 estimates it from the trips
    std::optional<double> suitability;       // optional factor in [0, 1] applied to the estimate
    double power_kw = 0;
    int ports = 1;
    std::string connector;
    int maint_period_h = 0;
    int maint_offset_h = 0;
};

// Throw std::invalid_argument with a reason when a field is out of range.
void validate(const ChargingStation &s);
void validate(const CandidateStation &c);

} // namespace ev
