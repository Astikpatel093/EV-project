// leg_planner.cpp - charging-leg graph planner.
#include "ev/planner/leg_planner.hpp"

#include <limits>
#include <stdexcept>

#include "plan_builder.hpp"

namespace ev {

using detail::fmt;
using detail::node_label;

LegPlanner::LegPlanner(const Scenario &scenario, Router &router, std::vector<ChargerSite> sites)
    : sc_(scenario), router_(router), sites_(std::move(sites)) {
    const std::size_t S = sites_.size();
    legs_.resize(S * S);
    leg_done_.assign(S * S, 0);
    leg_ok_.resize(scenario.vehicles.size());
}

namespace {
std::int64_t metres_of(const EnergyModel &m, const std::vector<int> &arcs) {
    std::int64_t t = 0;
    for (int a : arcs) t += m.arcs()[static_cast<std::size_t>(a)].metres;
    return t;
}
} // namespace

Router &LegPlanner::energy_router() {
    if (router_.method() == RoutingMethod::BellmanFord) return router_;   // already energy-minimal
    if (!energy_) energy_ = std::make_unique<Router>(router_.model(), RoutingMethod::BellmanFord);
    return *energy_;
}

bool LegPlanner::drivable(int a, int b, double start_kwh, const ElectricVehicle &v, Path &out) {
    const EnergyModel &model = router_.model();
    out.reachable = router_.leg(a, b, out.arcs);
    if (!out.reachable) return false;
    if (model.simulate(out.arcs, start_kwh, v).ok) {
        out.metres = metres_of(model, out.arcs);
        return true;
    }
    Router &e = energy_router();
    if (&e == &router_) return false;
    std::vector<int> alt;
    if (e.leg(a, b, alt) && model.simulate(alt, start_kwh, v).ok) {
        out.arcs = std::move(alt);
        out.metres = metres_of(model, out.arcs);
        return true;
    }
    return false;
}

const LegPlanner::Leg &LegPlanner::site_leg(int i, int j) {
    const std::size_t k = static_cast<std::size_t>(i) * sites_.size() + static_cast<std::size_t>(j);
    if (!leg_done_[k]) {
        Leg &L = legs_[k];
        L.main.reachable = router_.leg(sites_[static_cast<std::size_t>(i)].node, sites_[static_cast<std::size_t>(j)].node, L.main.arcs);
        L.main.metres = metres_of(router_.model(), L.main.arcs);
        leg_done_[k] = 1;
    }
    return legs_[k];
}

const LegPlanner::Path &LegPlanner::site_leg_alt(int i, int j) {
    site_leg(i, j);
    Leg &L = legs_[static_cast<std::size_t>(i) * sites_.size() + static_cast<std::size_t>(j)];
    if (!L.alt_done) {
        Router &e = energy_router();
        if (&e != &router_) {
            L.alt.reachable = e.leg(sites_[static_cast<std::size_t>(i)].node, sites_[static_cast<std::size_t>(j)].node, L.alt.arcs);
            L.alt.metres = metres_of(router_.model(), L.alt.arcs);
        }
        L.alt_done = true;
    }
    return L.alt;
}

int LegPlanner::site_leg_ok(int vi, int i, int j, const ElectricVehicle &v) {
    auto &cache = leg_ok_[static_cast<std::size_t>(vi)];
    if (cache.empty()) cache.assign(sites_.size() * sites_.size(), -1);
    signed char &c = cache[static_cast<std::size_t>(i) * sites_.size() + static_cast<std::size_t>(j)];
    if (c < 0) {
        const double start = sc_.config.target_soc * v.battery_kwh;
        const Leg &L = site_leg(i, j);
        c = 0;
        if (L.main.reachable && router_.model().simulate(L.main.arcs, start, v).ok) c = 1;
        else if (L.main.reachable) {
            const Path &A = site_leg_alt(i, j);
            if (A.reachable && router_.model().simulate(A.arcs, start, v).ok) c = 2;
        }
    }
    return c;
}

TripPlan LegPlanner::plan(const TripRequest &t) {
    const int vi = sc_.vehicle_index(t.vehicle_id);
    if (vi < 0) throw std::invalid_argument("trip " + std::to_string(t.id) + " uses unknown vehicle '" + t.vehicle_id + "'");
    const int a = sc_.network.index_of(t.from), b = sc_.network.index_of(t.to);
    if (a < 0 || b < 0) throw std::invalid_argument("trip " + std::to_string(t.id) + " refers to an unknown node");
    const ElectricVehicle &v = sc_.vehicles[static_cast<std::size_t>(vi)];
    return plan(a, b, v, t.start_charge_kwh.value_or(v.charge_kwh), t.depart_min, t.id);
}

TripPlan LegPlanner::plan(int from, int to, const ElectricVehicle &v, double start_kwh, double depart_min, int trip_id) {
    const EnergyModel &model = router_.model();
    TripPlan p;
    if (!(start_kwh >= 0) || start_kwh > v.battery_kwh + 1e-9)
        throw std::invalid_argument("start charge must be between 0 and the battery capacity of vehicle " + v.id);
    p.trip_id = trip_id;
    p.vehicle_id = v.id;
    p.depart_min = p.arrive_min = depart_min;
    p.start_kwh = p.arrive_kwh = p.min_kwh = start_kwh;
    p.route.assign(1, from);
    int vi = sc_.vehicle_index(v.id);
    if (vi < 0) {   // a vehicle that is not part of the scenario: use a private cache slot
        vi = static_cast<int>(leg_ok_.size());
        leg_ok_.emplace_back();
    }
    if (from == to) {
        p.status = PlanStatus::ZeroLength;
        p.shortest_km = 0;
        p.shortest_is_feasible = true;
        p.explanation = "Origin and destination are the same node: nothing to drive.";
        return p;
    }
    std::vector<int> direct;
    const bool connected = router_.leg(from, to, direct);
    if (connected) {
        EnergyModel::Trace tr = model.simulate(direct, start_kwh, v);   // the plain shortest route
        p.shortest_km = tr.km;
        p.shortest_kwh = tr.energy_used;
        p.shortest_is_feasible = tr.ok;
    }
    p.needs_charge = !p.shortest_is_feasible;
    if (!connected) {
        p.status = PlanStatus::NoRoadConnection;
        p.explanation = "No road connects " + node_label(sc_, from) + " to " + node_label(sc_, to) +
                        ": they are in different parts of the road network.";
        return p;
    }
    if (start_kwh < v.reserve_kwh - 1e-9) {
        p.status = PlanStatus::StartBelowReserve;
        p.explanation = "Vehicle " + v.id + " starts with " + fmt(start_kwh, 2) + " kWh, below its reserve of " +
                        fmt(v.reserve_kwh, 2) + " kWh. Charge before departing.";
        return p;
    }

    // ---- the charging-leg graph: 0 = origin, 1..S = sites, S+1 = destination
    const int S = static_cast<int>(sites_.size()), K = S + 2, DST = S + 1;
    std::vector<char> usable(static_cast<std::size_t>(S), 0), ok_from_origin(static_cast<std::size_t>(S), 0),
        ok_to_dest(static_cast<std::size_t>(S), 0);
    std::vector<Path> leg_s(static_cast<std::size_t>(S)), leg_d(static_cast<std::size_t>(S));
    const double target_kwh = sc_.config.target_soc * v.battery_kwh;
    Path direct_path;
    const bool direct_ok = drivable(from, to, start_kwh, v, direct_path);   // shortest route, else energy-minimal
    int reachable_sites = 0;
    for (int j = 0; j < S; j++) {
        const ChargerSite &c = sites_[static_cast<std::size_t>(j)];
        if (!usable_by(c, v)) continue;
        usable[static_cast<std::size_t>(j)] = 1;
        if (drivable(from, c.node, start_kwh, v, leg_s[static_cast<std::size_t>(j)])) {
            ok_from_origin[static_cast<std::size_t>(j)] = 1;
            reachable_sites++;
        }
        if (target_kwh >= v.reserve_kwh && drivable(c.node, to, target_kwh, v, leg_d[static_cast<std::size_t>(j)]))
            ok_to_dest[static_cast<std::size_t>(j)] = 1;
    }
    const std::int64_t INF = std::numeric_limits<std::int64_t>::max();
    std::vector<std::int64_t> dist(static_cast<std::size_t>(K), INF);
    std::vector<int> stops(static_cast<std::size_t>(K), 0), pred(static_cast<std::size_t>(K), -1);
    std::vector<char> done(static_cast<std::size_t>(K), 0);
    dist[0] = 0;
    auto better = [&](std::int64_t m, int k, int v2) {
        return m < dist[static_cast<std::size_t>(v2)] || (m == dist[static_cast<std::size_t>(v2)] && k < stops[static_cast<std::size_t>(v2)]);
    };
    for (;;) {
        int u = -1;   // dense Dijkstra: O(K) scan for the closest unfinished vertex
        for (int i = 0; i < K; i++)
            if (!done[static_cast<std::size_t>(i)] && dist[static_cast<std::size_t>(i)] < INF &&
                (u < 0 || dist[static_cast<std::size_t>(i)] < dist[static_cast<std::size_t>(u)] ||
                 (dist[static_cast<std::size_t>(i)] == dist[static_cast<std::size_t>(u)] && stops[static_cast<std::size_t>(i)] < stops[static_cast<std::size_t>(u)])))
                u = i;
        if (u < 0 || u == DST) break;
        done[static_cast<std::size_t>(u)] = 1;
        const std::int64_t du = dist[static_cast<std::size_t>(u)];
        const int su = stops[static_cast<std::size_t>(u)];
        auto relax = [&](int w, std::int64_t len, int add_stop) {
            if (done[static_cast<std::size_t>(w)]) return;
            const std::int64_t m = du + len;
            const int k = su + add_stop;
            if (better(m, k, w)) { dist[static_cast<std::size_t>(w)] = m; stops[static_cast<std::size_t>(w)] = k; pred[static_cast<std::size_t>(w)] = u; }
        };
        if (u == 0) {
            if (direct_ok) relax(DST, direct_path.metres, 0);
            for (int j = 0; j < S; j++)
                if (ok_from_origin[static_cast<std::size_t>(j)]) relax(j + 1, leg_s[static_cast<std::size_t>(j)].metres, 1);
        } else {
            const int i = u - 1;
            if (ok_to_dest[static_cast<std::size_t>(i)]) relax(DST, leg_d[static_cast<std::size_t>(i)].metres, 0);
            for (int j = 0; j < S; j++) {
                if (j == i || !usable[static_cast<std::size_t>(j)] || done[static_cast<std::size_t>(j) + 1]) continue;
                const int which = site_leg_ok(vi, i, j, v);
                if (which) relax(j + 1, which == 1 ? site_leg(i, j).main.metres : site_leg_alt(i, j).metres, 1);
            }
        }
    }
    if (dist[static_cast<std::size_t>(DST)] == INF) {
        std::string base = "The shortest road route from " + node_label(sc_, from) + " to " + node_label(sc_, to) + " is " +
                           fmt(p.shortest_km, 1) + " km and needs " + fmt(p.shortest_kwh, 2) + " kWh, but vehicle " + v.id +
                           " has only " + fmt(start_kwh - v.reserve_kwh, 2) + " kWh usable above its reserve.";
        if (reachable_sites == 0) {
            p.status = PlanStatus::OutOfRange;
            p.explanation = base + " No usable charging station is reachable on the starting charge (checked along shortest and energy-minimal routes).";
        } else {
            p.status = PlanStatus::ChargingInsufficient;
            p.explanation = base + " " + std::to_string(reachable_sites) + " usable charging station" + (reachable_sites > 1 ? "s are" : " is") +
                            " reachable, but no chain of charging legs reaches the destination (checked along shortest and energy-minimal routes).";
        }
        return p;
    }
    // ---- rebuild: sequence of visited sites, then the concatenated road arcs
    std::vector<int> seq;
    for (int x = pred[static_cast<std::size_t>(DST)]; x > 0; x = pred[static_cast<std::size_t>(x)]) seq.push_back(x - 1);
    std::vector<detail::PlanStep> steps;
    int prev = -1;   // -1 = origin
    for (std::size_t k = seq.size(); k-- > 0;) {
        const int j = seq[k];
        const int which = prev < 0 ? 1 : site_leg_ok(vi, prev, j, v);
        const std::vector<int> &arcs = prev < 0 ? leg_s[static_cast<std::size_t>(j)].arcs
                                     : which == 1 ? site_leg(prev, j).main.arcs : site_leg_alt(prev, j).arcs;
        for (int a : arcs) steps.push_back({a, -1});
        steps.push_back({-1, j});
        prev = j;
    }
    const std::vector<int> &last = prev < 0 ? direct_path.arcs : leg_d[static_cast<std::size_t>(prev)].arcs;
    for (int a : last) steps.push_back({a, -1});
    if (!detail::build_plan(p, steps, from, model, sites_, v, start_kwh, depart_min, sc_.config.target_soc))
        throw std::logic_error("leg planner produced a plan that fails the exact battery check");
    p.status = p.stops.empty() ? PlanStatus::Direct : PlanStatus::WithCharging;
    if (p.stops.empty()) {
        p.explanation = (p.shortest_is_feasible ? "Reachable without charging along the " + std::string(to_string(router_.method())) + " route: "
                                                : std::string("Reachable without charging by avoiding the shortest route, which the battery cannot cover: ")) +
                        fmt(p.distance_km, 1) + " km using " + fmt(p.energy_kwh, 2) + " kWh.";
    } else {
        p.explanation = "Plan with " + std::to_string(p.stops.size()) + " charging stop" + (p.stops.size() > 1 ? "s" : "") + " (";
        for (std::size_t k = 0; k < p.stops.size(); k++) p.explanation += (k ? ", " : "") + p.stops[k].name;
        p.explanation += "): " + fmt(p.distance_km, 1) + " km, " + fmt(p.charge_min, 0) + " min charging.";
    }
    return p;
}

} // namespace ev
