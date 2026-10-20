// capacity.cpp - max-flow / matching capacity check.
#include "ev/operations/capacity.hpp"

#include "ev/core/timer.hpp"

namespace ev {

CapacityResult analyze_capacity(PlanningRun &run) {
    const Scenario &sc = run.scenario();
    const ScenarioConfig &c = sc.config;
    const EnergyModel &model = run.router().model();
    CapacityResult out;
    const int S = static_cast<int>(run.sites.size());
    out.assigned.assign(static_cast<std::size_t>(S), 0);
    out.eligible.assign(static_cast<std::size_t>(S), 0);
    if (S == 0) return out;

    std::vector<int> need;
    for (std::size_t t = 0; t < run.plans.size(); t++) {
        const TripPlan &p = run.plans[t];
        if (!p.stops.empty() || (p.needs_charge && !p.feasible() && p.status != PlanStatus::NoRoadConnection)) need.push_back(static_cast<int>(t));
    }
    const int T = static_cast<int>(need.size());
    out.trips_needing = T;
    std::vector<std::vector<int>> elig(static_cast<std::size_t>(T));
    std::vector<int> arcs1, arcs2;
    for (int q = 0; q < T; q++) {
        const TripRequest &tr = sc.trips[static_cast<std::size_t>(need[static_cast<std::size_t>(q)])];
        const TripPlan &p = run.plans[static_cast<std::size_t>(need[static_cast<std::size_t>(q)])];
        const ElectricVehicle &v = sc.vehicles[static_cast<std::size_t>(sc.vehicle_index(tr.vehicle_id))];
        const double x0 = tr.start_charge_kwh.value_or(v.charge_kwh);
        const int src = sc.network.index_of(tr.from), dst = sc.network.index_of(tr.to);
        const int first = p.stops.empty() ? -1 : p.stops.front().site;
        for (int j = 0; j < S; j++) {
            const ChargerSite &site = run.sites[static_cast<std::size_t>(j)];
            bool ok = j == first;
            if (!ok && usable_by(site, v) && run.router().leg(src, site.node, arcs1) && model.simulate(arcs1, x0, v).ok &&
                run.router().leg(site.node, dst, arcs2)) {
                double k1 = 0, k2 = 0;
                for (int a : arcs1) k1 += model.arcs()[static_cast<std::size_t>(a)].km;
                for (int a : arcs2) k2 += model.arcs()[static_cast<std::size_t>(a)].km;
                const double detour = p.shortest_km >= 0 ? k1 + k2 - p.shortest_km : 0;
                ok = detour <= c.max_detour_km;
            }
            if (ok) {
                elig[static_cast<std::size_t>(q)].push_back(j);
                out.eligible[static_cast<std::size_t>(j)]++;
            }
        }
    }
    std::int64_t per = c.window_min / c.session_min;
    if (per < 1) per = 1;
    std::vector<std::int64_t> slots(static_cast<std::size_t>(S));
    for (int j = 0; j < S; j++) {
        slots[static_cast<std::size_t>(j)] = run.sites[static_cast<std::size_t>(j)].available ? run.sites[static_cast<std::size_t>(j)].ports * per : 0;
        out.slots_total += slots[static_cast<std::size_t>(j)];
    }
    // ---- max flow
    const int V = T + S + 2, source = 0, sink = T + S + 1;
    FlowNetwork f(V);
    for (int q = 0; q < T; q++) f.add_edge(source, 1 + q, 1);
    for (int q = 0; q < T; q++)
        for (int j : elig[static_cast<std::size_t>(q)]) f.add_edge(1 + q, 1 + T + j, 1);
    std::vector<int> sink_edge(static_cast<std::size_t>(S));
    for (int j = 0; j < S; j++) sink_edge[static_cast<std::size_t>(j)] = f.add_edge(1 + T + j, sink, slots[static_cast<std::size_t>(j)]);
    Stopwatch w;
    out.dfs = f.max_flow(source, sink, FlowMethod::FordFulkersonDfs);
    out.ms_dfs = w.elapsed_ms();
    f.reset();
    w.restart();
    out.bfs = f.max_flow(source, sink, FlowMethod::EdmondsKarpBfs);
    out.ms_bfs = w.elapsed_ms();
    out.min_cut = f.cut_capacity(f.min_cut_source_side(source));
    int worst_elig = -1;
    for (int j = 0; j < S; j++) {
        out.assigned[static_cast<std::size_t>(j)] = static_cast<int>(f.flow(sink_edge[static_cast<std::size_t>(j)]));
        if (out.assigned[static_cast<std::size_t>(j)] >= slots[static_cast<std::size_t>(j)] &&
            out.eligible[static_cast<std::size_t>(j)] > slots[static_cast<std::size_t>(j)] && out.eligible[static_cast<std::size_t>(j)] > worst_elig) {
            out.bottleneck_site = j;
            worst_elig = out.eligible[static_cast<std::size_t>(j)];
        }
    }
    // ---- bipartite matching: trips <-> individual port-slots
    std::vector<std::int64_t> slot_off(static_cast<std::size_t>(S) + 1, 0);
    for (int j = 0; j < S; j++) {
        std::int64_t use = slots[static_cast<std::size_t>(j)] < T ? slots[static_cast<std::size_t>(j)] : T;   // more slots than trips are never used
        slot_off[static_cast<std::size_t>(j) + 1] = slot_off[static_cast<std::size_t>(j)] + use;
    }
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(T));
    for (int q = 0; q < T; q++)
        for (int j : elig[static_cast<std::size_t>(q)])
            for (std::int64_t sl = slot_off[static_cast<std::size_t>(j)]; sl < slot_off[static_cast<std::size_t>(j) + 1]; sl++)
                adj[static_cast<std::size_t>(q)].push_back(static_cast<int>(sl));
    w.restart();
    out.matching = bipartite_matching(T, static_cast<int>(slot_off[static_cast<std::size_t>(S)]), adj);
    out.ms_match = w.elapsed_ms();
    return out;
}

} // namespace ev
