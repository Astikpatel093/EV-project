// energy_model.hpp - how much battery energy each road costs a particular vehicle.
//
// Simplified, documented model (docs/algorithms.md, "Energy model"):
//
//   E_ref(arc) = flat_ref + climb_ref
//     flat_ref  = road.energy_kwh if the data gives it, otherwise distance_km × reference_consumption
//     climb_ref = Δh × climb_kwh_per_m                      when going uphill   (Δh > 0)
//               = Δh × climb_kwh_per_m × regen_efficiency   when going downhill (Δh < 0, energy recovered)
//   E_vehicle(arc) = E_ref(arc) × (vehicle consumption / reference_consumption)
//
// With flat roads this is exactly "energy = distance × consumption", using each vehicle's own
// consumption. E can be negative on steep descents (regenerative braking), but around any closed
// loop the climbs equal the descents, so a cycle's energy is ≥ 0 whenever regen_efficiency ≤ 1:
// there are no negative cycles and Bellman-Ford is valid on the energy graph (Dijkstra is not).
// Simplifications: no speed, temperature, payload or auxiliary-load effects; the scaling of the
// climb term by consumption is a proxy for vehicle mass.
#pragma once

#include <vector>

#include "ev/models/scenario.hpp"

namespace ev {

struct RoadArc {
    int from = 0, to = 0;     // node positions
    int edge = 0;             // road (edge position) this arc belongs to
    double km = 0;
    std::int64_t metres = 0;  // km rounded to whole metres (exact integer path lengths)
    double minutes = 0;       // travel time
    double energy_ref = 0;    // kWh for the reference vehicle (may be negative)
};

class EnergyModel {
public:
    explicit EnergyModel(const Scenario &s);

    int node_count() const { return n_; }
    const std::vector<RoadArc> &arcs() const { return arcs_; }
    // arcs leaving node u are arcs()[first(u) .. first(u+1)-1]
    int first(int u) const { return head_[static_cast<std::size_t>(u)]; }

    // vehicle multiplier: consumption / reference consumption
    double scale(const ElectricVehicle &v) const { return v.consumption_kwh_per_km / ref_consumption_; }
    double energy(int arc, const ElectricVehicle &v) const { return arcs_[static_cast<std::size_t>(arc)].energy_ref * scale(v); }

    // Battery level after following `arc_ids` from `start_kwh`, capping at capacity on downhill
    // stretches. `ok` is false if the level drops below the vehicle's reserve at any node.
    struct Trace {
        bool ok = true;
        double end_kwh = 0;
        double min_kwh = 0;           // lowest level reached at a node
        std::size_t failed_arc = 0;   // index into arc_ids where the reserve was broken (when !ok)
        double km = 0, minutes = 0, energy_used = 0;
    };
    Trace simulate(const std::vector<int> &arc_ids, double start_kwh, const ElectricVehicle &v) const;

    // Component label of every node (roads are two-way) and the number of components.
    const std::vector<int> &component() const { return component_; }
    int components() const { return components_; }

private:
    int n_ = 0;
    double ref_consumption_ = 0.18;
    std::vector<RoadArc> arcs_;
    std::vector<int> head_;
    std::vector<int> component_;
    int components_ = 0;
};

} // namespace ev
