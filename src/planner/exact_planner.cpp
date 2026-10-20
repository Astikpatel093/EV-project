// exact_planner.cpp - Dijkstra over (node, discretised battery level) states.
#include "ev/planner/exact_planner.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/algorithms/heap.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "plan_builder.hpp"

namespace ev {

using detail::fmt;
using detail::node_label;

ExactPlanner::ExactPlanner(const Scenario &scenario, const EnergyModel &model, std::vector<ChargerSite> sites)
    : sc_(scenario), model_(model), sites_(std::move(sites)), km_graph_(model.node_count(), true) {
    for (const RoadArc &a : model.arcs()) km_graph_.add_edge(a.from, a.to, a.km);
}

TripPlan ExactPlanner::plan(const TripRequest &t) {
    const int vi = sc_.vehicle_index(t.vehicle_id);
    if (vi < 0) throw std::invalid_argument("trip " + std::to_string(t.id) + " uses unknown vehicle '" + t.vehicle_id + "'");
    const int a = sc_.network.index_of(t.from), b = sc_.network.index_of(t.to);
    if (a < 0 || b < 0) throw std::invalid_argument("trip " + std::to_string(t.id) + " refers to an unknown node");
    const ElectricVehicle &v = sc_.vehicles[static_cast<std::size_t>(vi)];
    return plan(a, b, v, t.start_charge_kwh.value_or(v.charge_kwh), t.depart_min, t.id);
}

ExactPlanner::SearchResult ExactPlanner::search(int from, int to, const ElectricVehicle &v, double start_kwh, bool allow_charging) {
    SearchResult res;
    stats_ = Stats();
    const double step = sc_.config.battery_step_kwh;
    const int L = static_cast<int>(std::floor(v.battery_kwh / step + 1e-9));
    const int reserve = static_cast<int>(std::ceil(v.reserve_kwh / step - 1e-9));
    const int target = static_cast<int>(std::floor(sc_.config.target_soc * v.battery_kwh / step + 1e-9));
    const int start = static_cast<int>(std::floor(start_kwh / step + 1e-9));
    const std::int64_t n = model_.node_count();
    const std::int64_t per = L + 1;
    if (start < reserve) return res;

    // (re)size the reusable buffers
    const std::size_t total = static_cast<std::size_t>(n * per);
    if (stamp_.size() < total || levels_ != per) {
        stamp_.assign(total, 0);
        metres_.resize(total);
        stops_.resize(total);
        parent_.resize(total);
        action_.resize(total);
        settled_.resize(total);
        levels_ = static_cast<int>(per);
        generation_ = 0;
    }
    if (++generation_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); generation_ = 1; }

    // best usable site at each node for this vehicle (highest power, then lowest index)
    std::vector<int> site_at(static_cast<std::size_t>(n), -1);
    if (allow_charging)
        for (std::size_t k = 0; k < sites_.size(); k++) {
            const ChargerSite &c = sites_[k];
            if (!usable_by(c, v)) continue;
            int &cur = site_at[static_cast<std::size_t>(c.node)];
            if (cur < 0 || c.power_kw > sites_[static_cast<std::size_t>(cur)].power_kw) cur = static_cast<int>(k);
        }
    if (!allow_charging) res.node_reached.assign(static_cast<std::size_t>(n), 0);

    struct Item { std::int64_t metres; int stops; std::int64_t state; };
    struct Better {
        bool operator()(const Item &a, const Item &b) const {
            if (a.metres != b.metres) return a.metres < b.metres;
            if (a.stops != b.stops) return a.stops < b.stops;
            return a.state < b.state;
        }
    };
    BinaryHeap<Item, Better> heap;
    auto relax = [&](std::int64_t st, std::int64_t m, int k, std::int64_t parent, int action) {
        const std::size_t i = static_cast<std::size_t>(st);
        if (stamp_[i] == generation_ && (metres_[i] < m || (metres_[i] == m && stops_[i] <= k))) return;
        stamp_[i] = generation_;
        metres_[i] = m;
        stops_[i] = k;
        parent_[i] = parent;
        action_[i] = action;
        settled_[i] = 0;
        heap.push({m, k, st});
        stats_.states_pushed++;
    };
    const double scale = model_.scale(v);
    relax(from * per + start, 0, 0, -1, 0);
    while (!heap.empty()) {
        Item it = heap.pop();
        const std::size_t i = static_cast<std::size_t>(it.state);
        if (settled_[i] || metres_[i] != it.metres || stops_[i] != it.stops) continue;   // stale entry
        settled_[i] = 1;
        stats_.states_settled++;
        const int u = static_cast<int>(it.state / per), b = static_cast<int>(it.state % per);
        if (!allow_charging) res.node_reached[static_cast<std::size_t>(u)] = 1;
        if (u == to && allow_charging) { res.found = true; res.goal = it.state; return res; }
        if (allow_charging && site_at[static_cast<std::size_t>(u)] >= 0 && b < target && target >= reserve)
            relax(u * per + target, it.metres, it.stops + 1, it.state, -(site_at[static_cast<std::size_t>(u)] + 1));
        for (int k = model_.first(u); k < model_.first(u + 1); k++) {
            const RoadArc &a = model_.arcs()[static_cast<std::size_t>(k)];
            const double e = a.energy_ref * scale;
            int nb;
            if (e >= 0) nb = b - static_cast<int>(std::ceil(e / step - 1e-9));                 // consumption rounds up
            else nb = std::min(L, b + static_cast<int>(std::floor(-e / step + 1e-9)));          // recovery rounds down
            if (nb < reserve) continue;
            relax(a.to * per + nb, it.metres + a.metres, it.stops, it.state, k);
        }
    }
    return res;
}

std::vector<int> ExactPlanner::reachable_sites(int from, const ElectricVehicle &v, double start_kwh) {
    std::vector<int> out;
    SearchResult r = search(from, -1, v, start_kwh, false);
    if (r.node_reached.empty()) return out;
    for (std::size_t k = 0; k < sites_.size(); k++)
        if (usable_by(sites_[k], v) && r.node_reached[static_cast<std::size_t>(sites_[k].node)]) out.push_back(static_cast<int>(k));
    return out;
}

TripPlan ExactPlanner::plan(int from, int to, const ElectricVehicle &v, double start_kwh, double depart_min, int trip_id) {
    km_graph_.check_vertex(from);
    km_graph_.check_vertex(to);
    if (!(start_kwh >= 0) || start_kwh > v.battery_kwh + 1e-9)
        throw std::invalid_argument("start charge must be between 0 and the battery capacity of vehicle " + v.id);
    TripPlan p;
    p.trip_id = trip_id;
    p.vehicle_id = v.id;
    p.depart_min = p.arrive_min = depart_min;
    p.start_kwh = p.arrive_kwh = p.min_kwh = start_kwh;
    p.route.assign(1, from);

    if (from == to) {
        p.status = PlanStatus::ZeroLength;
        p.shortest_km = 0;
        p.shortest_is_feasible = true;
        p.explanation = "Origin and destination are the same node: nothing to drive.";
        return p;
    }
    // facts about the plain shortest road route
    ShortestPathTree t = dijkstra(km_graph_, from);
    if (t.reachable(to)) {
        std::vector<int> arcs = t.edges_to(to);
        EnergyModel::Trace tr = model_.simulate(arcs, start_kwh, v);
        p.shortest_km = tr.km;
        p.shortest_kwh = tr.energy_used;
        p.shortest_is_feasible = tr.ok;
    }
    p.needs_charge = !p.shortest_is_feasible;
    if (!t.reachable(to)) {
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
    SearchResult r = search(from, to, v, start_kwh, true);
    if (!r.found) {
        explain_failure(p, from, to, v, start_kwh);
        return p;
    }
    // rebuild the action sequence from the goal state back to the start
    std::vector<detail::PlanStep> rev;
    for (std::int64_t st = r.goal; parent_[static_cast<std::size_t>(st)] >= 0; st = parent_[static_cast<std::size_t>(st)]) {
        int act = action_[static_cast<std::size_t>(st)];
        detail::PlanStep step;
        if (act >= 0) step.arc = act;
        else step.site = -act - 1;
        rev.push_back(step);
    }
    std::vector<detail::PlanStep> steps(rev.rbegin(), rev.rend());
    if (!detail::build_plan(p, steps, from, model_, sites_, v, start_kwh, depart_min, sc_.config.target_soc))
        throw std::logic_error("exact planner produced a plan that fails the exact battery check");
    p.status = p.stops.empty() ? PlanStatus::Direct : PlanStatus::WithCharging;
    if (p.stops.empty()) {
        p.explanation = "Reachable without charging: " + fmt(p.distance_km, 1) + " km using " + fmt(p.energy_kwh, 2) +
                        " kWh; arrives with " + fmt(p.arrive_kwh, 2) + " kWh (reserve " + fmt(v.reserve_kwh, 2) + " kWh).";
        if (p.distance_km > p.shortest_km + 1e-6)
            p.explanation += " The shortest road route (" + fmt(p.shortest_km, 1) + " km) is not energy-feasible, so a longer one is used.";
    } else {
        p.explanation = "Needs " + std::to_string(p.stops.size()) + " charging stop" + (p.stops.size() > 1 ? "s" : "") + " (";
        for (std::size_t k = 0; k < p.stops.size(); k++) p.explanation += (k ? ", " : "") + p.stops[k].name;
        p.explanation += "): " + fmt(p.distance_km, 1) + " km, " + fmt(p.charge_min, 0) + " min charging; the shortest road route (" +
                         fmt(p.shortest_km, 1) + " km) needs " + fmt(p.shortest_kwh, 2) + " kWh but only " +
                         fmt(start_kwh - v.reserve_kwh, 2) + " kWh is usable at departure.";
    }
    return p;
}

void ExactPlanner::explain_failure(TripPlan &p, int from, int to, const ElectricVehicle &v, double start_kwh) {
    std::vector<int> reach = reachable_sites(from, v, start_kwh);
    // stations that are in reach but cannot be used by this vehicle (offline / wrong connector)
    SearchResult r = search(from, -1, v, start_kwh, false);
    int blocked = 0;
    for (const ChargerSite &c : sites_)
        if (!usable_by(c, v) && !r.node_reached.empty() && r.node_reached[static_cast<std::size_t>(c.node)]) blocked++;
    std::string base = "The shortest road route from " + node_label(sc_, from) + " to " + node_label(sc_, to) + " is " +
                       fmt(p.shortest_km, 1) + " km and needs " + fmt(p.shortest_kwh, 2) + " kWh, but vehicle " + v.id +
                       " has only " + fmt(start_kwh - v.reserve_kwh, 2) + " kWh usable above its reserve, and no route works.";
    std::string blocked_note;
    if (blocked > 0)
        blocked_note = " " + std::to_string(blocked) + " station" + (blocked > 1 ? "s are" : " is") +
                       " within reach but unusable for this vehicle (offline, or no " +
                       (v.connector.empty() ? std::string("compatible") : v.connector) + " connector).";
    if (reach.empty()) {
        p.status = PlanStatus::OutOfRange;
        p.explanation = base + " No usable charging station can be reached on the starting charge." + blocked_note;
    } else {
        p.status = PlanStatus::ChargingInsufficient;
        p.explanation = base + " " + std::to_string(reach.size()) + " usable charging station" + (reach.size() > 1 ? "s are" : " is") +
                        " reachable (e.g. " + sites_[static_cast<std::size_t>(reach[0])].name +
                        "), but even charging to " + fmt(sc_.config.target_soc * v.battery_kwh, 1) +
                        " kWh at each stop the destination stays out of range: on every route some stretch between chargers is longer than one charge allows." +
                        blocked_note;
    }
}

} // namespace ev
