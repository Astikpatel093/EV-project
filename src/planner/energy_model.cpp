// energy_model.cpp - directed road arcs with travel time and reference energy.
#include "ev/planner/energy_model.hpp"

#include <cmath>

namespace ev {

EnergyModel::EnergyModel(const Scenario &s) {
    const RoadNetwork &net = s.network;
    const ScenarioConfig &c = s.config;
    n_ = net.node_count();
    ref_consumption_ = c.reference_consumption;
    // two arcs per road, grouped by tail (counting sort keeps input order inside each group)
    std::vector<RoadArc> raw;
    raw.reserve(static_cast<std::size_t>(net.edge_count()) * 2);
    for (int k = 0; k < net.edge_count(); k++) {
        const RoadEdge &e = net.edges()[static_cast<std::size_t>(k)];
        const int a = net.from_index(k), b = net.to_index(k);
        const double minutes = e.travel_time_min ? *e.travel_time_min : e.distance_km / c.speed_kmh * 60.0;
        const double flat = e.energy_kwh ? *e.energy_kwh : e.distance_km * c.reference_consumption;
        for (int dir = 0; dir < 2; dir++) {
            RoadArc arc;
            arc.from = dir ? b : a;
            arc.to = dir ? a : b;
            arc.edge = k;
            arc.km = e.distance_km;
            arc.metres = std::llround(e.distance_km * 1000.0);
            arc.minutes = minutes;
            const double dh = net.node(arc.to).elevation_m - net.node(arc.from).elevation_m;
            const double climb = dh > 0 ? dh * c.climb_kwh_per_m : dh * c.climb_kwh_per_m * c.regen_efficiency;
            arc.energy_ref = flat + climb;
            raw.push_back(arc);
        }
    }
    head_.assign(static_cast<std::size_t>(n_) + 1, 0);
    for (const RoadArc &a : raw) head_[static_cast<std::size_t>(a.from) + 1]++;
    for (int u = 0; u < n_; u++) head_[static_cast<std::size_t>(u) + 1] += head_[static_cast<std::size_t>(u)];
    arcs_.resize(raw.size());
    std::vector<int> fill(head_.begin(), head_.end() - 1);
    for (const RoadArc &a : raw) arcs_[static_cast<std::size_t>(fill[static_cast<std::size_t>(a.from)]++)] = a;
    components_ = net.components(&component_);
}

EnergyModel::Trace EnergyModel::simulate(const std::vector<int> &arc_ids, double start_kwh, const ElectricVehicle &v) const {
    Trace t;
    const double s = scale(v);
    double x = start_kwh;
    t.min_kwh = x;
    if (x < v.reserve_kwh - 1e-9) { t.ok = false; t.failed_arc = 0; }
    // keep going after a failure so the totals (km, minutes, energy) describe the whole path
    for (std::size_t i = 0; i < arc_ids.size(); i++) {
        const RoadArc &a = arcs_[static_cast<std::size_t>(arc_ids[i])];
        const double e = a.energy_ref * s;
        x -= e;
        if (x > v.battery_kwh) x = v.battery_kwh;   // a full battery cannot store more regenerated energy
        t.km += a.km;
        t.minutes += a.minutes;
        t.energy_used += e;
        if (x < t.min_kwh) t.min_kwh = x;
        if (x < v.reserve_kwh - 1e-9 && t.ok) { t.ok = false; t.failed_arc = i; }
    }
    t.end_kwh = x;
    return t;
}

} // namespace ev
