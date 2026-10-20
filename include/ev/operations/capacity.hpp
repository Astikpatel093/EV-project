// capacity.hpp - can the installed ports absorb the trips that need a charge?
//
// Flow network: source -> trip (capacity 1) -> eligible charger site (1) -> sink (port-slots in the
// window = ports × floor(window_min / session_min)). A trip is eligible at a site it plans to stop
// at first, or at any site it can reach on its starting charge with a detour <= max_detour_km.
// Solved three ways that must agree: Ford-Fulkerson (DFS), Edmonds-Karp (BFS), and bipartite
// matching of trips to individual port-slots.
#pragma once

#include <cstdint>
#include <vector>

#include "ev/analysis/pipeline.hpp"
#include "ev/graph/max_flow.hpp"

namespace ev {

struct CapacityResult {
    int trips_needing = 0;            // plans with a stop, or trips that needed a charge and failed
    std::int64_t slots_total = 0;
    FlowResult dfs, bfs;
    std::int64_t min_cut = 0;         // capacity of the minimum cut (equals the max flow)
    int matching = 0;
    double ms_dfs = 0, ms_bfs = 0, ms_match = 0;
    int bottleneck_site = -1;         // saturated site with the most eligible demand
    std::vector<int> assigned;        // per site: trips allocated by the max flow
    std::vector<int> eligible;        // per site: trips that could use it
    bool consistent() const { return dfs.value == bfs.value && bfs.value == matching && min_cut == bfs.value; }
};

CapacityResult analyze_capacity(PlanningRun &run);

} // namespace ev
