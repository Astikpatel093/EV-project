// plan_builder.cpp - exact replay of a plan and shared helpers.
#include "plan_builder.hpp"

#include <cstdio>
#include <stdexcept>

namespace ev {

const char *to_string(PlanStatus s) {
    switch (s) {
    case PlanStatus::ZeroLength: return "zero-length";
    case PlanStatus::Direct: return "direct";
    case PlanStatus::WithCharging: return "with-charging";
    case PlanStatus::NoRoadConnection: return "no-road-connection";
    case PlanStatus::StartBelowReserve: return "start-below-reserve";
    case PlanStatus::OutOfRange: return "out-of-range";
    default: return "charging-insufficient";
    }
}

std::vector<ChargerSite> make_charger_sites(const Scenario &s, const std::vector<int> &built,
                                            const std::map<int, int> &ports_override) {
    std::vector<ChargerSite> out;
    auto ports_for = [&](int id, int base) {
        auto it = ports_override.find(id);
        return it == ports_override.end() ? base : it->second;
    };
    for (std::size_t i = 0; i < s.stations.size(); i++) {
        const ChargingStation &st = s.stations[i];
        ChargerSite c;
        c.kind = ChargerSite::Kind::Existing;
        c.source = static_cast<int>(i);
        c.id = st.id;
        c.name = st.name.empty() ? "Station " + std::to_string(st.id) : st.name;
        c.node = s.network.index_of(st.node);
        c.power_kw = st.power_kw;
        c.ports = ports_for(st.id, st.ports);
        c.connector = st.connector;
        c.available = st.status == StationStatus::Available && c.ports > 0;
        out.push_back(c);
    }
    for (int ci : built) {
        if (ci < 0 || ci >= static_cast<int>(s.candidates.size())) throw std::out_of_range("built candidate index out of range");
        const CandidateStation &cd = s.candidates[static_cast<std::size_t>(ci)];
        ChargerSite c;
        c.kind = ChargerSite::Kind::New;
        c.source = ci;
        c.id = cd.id;
        c.name = cd.name.empty() ? "Site " + std::to_string(cd.id) : cd.name;
        c.node = s.network.index_of(cd.node);
        c.power_kw = cd.power_kw;
        c.ports = ports_for(cd.id, cd.ports);
        c.connector = cd.connector;
        c.available = c.ports > 0;
        out.push_back(c);
    }
    return out;
}

bool usable_by(const ChargerSite &site, const ElectricVehicle &v) {
    return site.available && connectors_compatible(site.connector, v.connector);
}

namespace detail {

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string node_label(const Scenario &s, int pos) {
    const RoadNode &n = s.network.node(pos);
    std::string l = "node " + std::to_string(n.id);
    if (!n.name.empty()) l += " (" + n.name + ")";
    return l;
}

bool build_plan(TripPlan &p, const std::vector<PlanStep> &steps, int from_pos, const EnergyModel &model,
                const std::vector<ChargerSite> &sites, const ElectricVehicle &v, double start_kwh, double depart_min,
                double target_soc) {
    const double s = model.scale(v), target = target_soc * v.battery_kwh;
    double x = start_kwh, clock = depart_min;
    p.route.assign(1, from_pos);
    p.route_arcs.clear();
    p.stops.clear();
    p.distance_km = p.energy_kwh = p.drive_min = p.charge_min = 0;
    p.depart_min = depart_min;
    p.start_kwh = start_kwh;
    p.min_kwh = x;
    bool ok = x >= v.reserve_kwh - 1e-9;
    for (const PlanStep &st : steps) {
        if (st.arc >= 0) {
            const RoadArc &a = model.arcs()[static_cast<std::size_t>(st.arc)];
            const double e = a.energy_ref * s;
            x -= e;
            if (x > v.battery_kwh) x = v.battery_kwh;
            if (x < v.reserve_kwh - 1e-9) ok = false;
            if (x < p.min_kwh) p.min_kwh = x;
            p.distance_km += a.km;
            p.energy_kwh += e;
            p.drive_min += a.minutes;
            clock += a.minutes;
            p.route.push_back(a.to);
            p.route_arcs.push_back(st.arc);
        } else {
            const ChargerSite &site = sites[static_cast<std::size_t>(st.site)];
            ChargingStop cs;
            cs.site = st.site;
            cs.station_id = site.id;
            cs.name = site.name;
            cs.node = site.node;
            cs.arrive_min = clock;
            cs.arrive_kwh = x;
            double rate = site.power_kw;
            if (v.max_charge_kw > 0 && v.max_charge_kw < rate) rate = v.max_charge_kw;
            const double need = target - x;
            cs.charge_min = need > 0 ? need / rate * 60.0 : 0.0;
            if (x < target) x = target;
            cs.depart_kwh = x;
            cs.route_pos = p.route.size() - 1;
            clock += cs.charge_min;
            p.charge_min += cs.charge_min;
            p.stops.push_back(cs);
        }
    }
    p.arrive_min = clock;
    p.arrive_kwh = x;
    return ok;
}

} // namespace detail
} // namespace ev
