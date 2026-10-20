// vehicle.hpp - electric vehicle specification and battery state.
//
// Every vehicle has its own battery size and consumption: EV33 never assumes that all vehicles
// use the same energy per km.
#pragma once

#include <string>

namespace ev {

struct ElectricVehicle {
    std::string id;                    // e.g. "V1"
    std::string model;                 // description, e.g. "Compact hatchback (synthetic)"
    double battery_kwh = 0;            // usable capacity, > 0
    double charge_kwh = 0;             // current charge, 0 <= charge <= capacity
    double consumption_kwh_per_km = 0; // flat-road consumption, > 0
    double reserve_kwh = 0;            // never plan below this, 0 <= reserve < capacity
    std::string connector;             // e.g. "CCS2" (empty = not recorded)
    double max_charge_kw = 0;          // vehicle's charging limit; 0 = limited only by the station

    // energy available for driving right now (may be negative if below the reserve)
    double usable_now() const { return charge_kwh - reserve_kwh; }
    double range_now_km() const { return usable_now() / consumption_kwh_per_km; }
};

// Throws std::invalid_argument describing the first problem (zero capacity, zero consumption,
// charge above capacity, reserve >= capacity, ...).
void validate(const ElectricVehicle &v);

} // namespace ev
