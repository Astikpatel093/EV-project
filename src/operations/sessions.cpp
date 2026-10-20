// sessions.cpp - per-station session analysis and grid surge window.
#include "ev/operations/sessions.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/algorithms/divide_conquer.hpp"

namespace ev {

// Serve as many intervals as possible on `bays` bays: activity selection per bay on what is left.
static std::vector<std::size_t> serve_with_bays(const std::vector<Interval> &iv, int bays) {
    std::vector<std::size_t> remaining(iv.size()), served;
    for (std::size_t i = 0; i < iv.size(); i++) remaining[i] = i;
    for (int b = 0; b < bays && !remaining.empty(); b++) {
        std::vector<Interval> sub;
        for (std::size_t i : remaining) sub.push_back(iv[i]);
        std::vector<std::size_t> pick = activity_selection(sub);
        std::vector<char> taken(sub.size(), 0);
        for (std::size_t k : pick) { taken[k] = 1; served.push_back(remaining[k]); }
        std::vector<std::size_t> rest;
        for (std::size_t k = 0; k < sub.size(); k++)
            if (!taken[k]) rest.push_back(remaining[k]);
        remaining.swap(rest);
    }
    return served;
}

SessionAnalysis analyze_sessions(const PlanningRun &run) {
    const Scenario &sc = run.scenario();
    const ScenarioConfig &c = sc.config;
    SessionAnalysis out;
    const std::size_t S = run.sites.size();
    out.sites.resize(S);
    out.hourly_kwh.assign(static_cast<std::size_t>(c.horizon_h), 0.0);
    std::vector<std::vector<Interval>> per(S);
    for (std::size_t t = 0; t < run.plans.size(); t++) {
        const TripPlan &p = run.plans[t];
        if (!p.feasible()) continue;
        const ElectricVehicle &v = sc.vehicles[static_cast<std::size_t>(sc.vehicle_index(p.vehicle_id))];
        for (const ChargingStop &st : p.stops) {
            double d = st.charge_min < c.min_session_min ? c.min_session_min : st.charge_min;
            if (d <= 0) d = 1e-6;   // a stop always occupies the bay for a moment
            per[static_cast<std::size_t>(st.site)].push_back({p.trip_id, st.arrive_min, st.arrive_min + d});
            const ChargerSite &site = run.sites[static_cast<std::size_t>(st.site)];
            double rate = site.power_kw;
            if (v.max_charge_kw > 0 && v.max_charge_kw < rate) rate = v.max_charge_kw;
            int h = static_cast<int>(std::floor(st.arrive_min / 60.0)) % c.horizon_h;
            if (h < 0) h += c.horizon_h;
            out.hourly_kwh[static_cast<std::size_t>(h)] += st.charge_min / 60.0 * rate;
        }
    }
    for (std::size_t j = 0; j < S; j++) {
        SiteLoad &L = out.sites[j];
        L.site = static_cast<int>(j);
        L.sessions = static_cast<int>(per[j].size());
        out.sessions += L.sessions;
        if (per[j].empty()) continue;
        L.chargers_needed = interval_partitioning(per[j]).resources;
        L.depth = max_overlap(per[j]);
        const int ports = run.sites[j].available ? run.sites[j].ports : 0;
        if (L.chargers_needed > ports) out.overloaded++;
        L.served = static_cast<int>(serve_with_bays(per[j], ports).size());
        out.served += L.served;
    }
    std::vector<double> net(out.hourly_kwh.size());
    for (std::size_t h = 0; h < net.size(); h++) net[h] = out.hourly_kwh[h] - c.grid_kw;
    SubarrayResult m = max_subarray_dc(net);
    if (m.sum > 0) {
        out.surge_lo = static_cast<int>(m.lo);
        out.surge_hi = static_cast<int>(m.hi);
        out.surge_excess_kwh = m.sum;
    }
    return out;
}

StationSchedule schedule_station(const Scenario &s, int station_id) {
    StationSchedule r;
    r.station_id = station_id;
    const int si = s.station_index(station_id), ci = s.candidate_index(station_id);
    if (si < 0 && ci < 0) throw std::invalid_argument("unknown station id " + std::to_string(station_id));
    r.ports = si >= 0 ? s.stations[static_cast<std::size_t>(si)].ports : s.candidates[static_cast<std::size_t>(ci)].ports;
    std::vector<Interval> iv;
    for (const ChargingSession &x : s.sessions)
        if (x.station_id == station_id) {
            r.sessions.push_back(x);
            iv.push_back({x.id, x.start_min, x.end_min});
        }
    r.one_bay = activity_selection(iv);
    r.bays = interval_partitioning(iv);
    r.depth = max_overlap(iv);
    r.served_with_ports = serve_with_bays(iv, r.ports);
    return r;
}

} // namespace ev
