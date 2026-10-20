// content.cpp - see content.h.
#include "content.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "fmt.h"

namespace ev33 {

const int SEL_ORDER[SEL_COUNT] = { SEL_DP, SEL_BNB_LC, SEL_BNB_FIFO, SEL_BNB_LIFO, SEL_GREEDY };

std::string lowerLabel(const City &c) {
    std::string s = c.label;
    for (auto &ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

const char *checkLabel(int c) {
    switch (c) {
    case CHECK_OK: return "Agrees";
    case CHECK_BAD: return "Differs";
    case CHECK_SINGLE: return "Single method";
    case CHECK_STRATEGIES: return "See strategies";
    default: return "Not run";
    }
}

/* ======================================================================== algorithms */
std::vector<AlgoRow> algorithmRows(const City &city, const Run *R, const Analysis &A) {
    std::vector<AlgoRow> rows;
    const Json &D = A.data;
    const Json &Ss = D["sorting_searching"], &C = D["divide_conquer"], &G = D["greedy"], &Pd = D["dynamic_programming"];
    const Json &B = D["backtracking_bnb"], &F = D["flow"], &N = D["crt_strings"];
    std::string nTrips = fint(city.sc.nt);
    auto ok = [](bool v) { return v ? CHECK_OK : CHECK_BAD; };
    auto n = [](const Json &o, const char *k) { return o[k].num(); };
    auto fam = [&](const char *t) { AlgoRow r; r.family = true; r.decision = t; rows.push_back(r); };
    auto row = [&](const char *d, const char *a, const std::string &res, int chk) {
        AlgoRow r; r.decision = d; r.algorithm = a; r.result = res; r.check = chk; rows.push_back(r);
    };

    fam("Sorting and searching");
    if (Ss.has("merge_ms")) row("Order the dispatch queue by departure time", "Merge sort, checked against quick sort",
                               nTrips + " trips sorted; merge " + fms(n(Ss, "merge_ms")) + " and stable, quick " + fms(n(Ss, "quick_ms")),
                               ok(Ss["merge_sorted"].truthy() && Ss["merge_stable"].truthy() && Ss["quick_agrees"].truthy()));
    if (Ss.has("bsearch_iter_hits")) row("Look up junctions by id", "Iterative and recursive binary search",
                                        fint(n(Ss, "bsearch_iter_hits")) + " of " + nTrips + " origins found by both", ok(n(Ss, "bsearch_iter_hits") == n(Ss, "bsearch_rec_hits")));
    if (Ss.has("sites_under_10pct_budget")) row("Find sites under a cost ceiling", "Quick sort on cost, then binary search",
                                               fint(n(Ss, "sites_under_10pct_budget")) + " candidate sites cost at most 10% of the budget", CHECK_SINGLE);
    fam("Divide and conquer");
    if (C.has("surge_sum_dc")) row("Find the worst grid-overload window", "Maximum subarray, checked against Kadane",
                                  n(C, "surge_sum_dc") > 0 ? fclock(n(C, "surge_lo") * 60) + " to " + fclock((n(C, "surge_hi") + 1) * 60) + ", " + fint(n(C, "surge_sum_dc")) + " kWh over supply"
                                                           : std::string("No hour exceeds grid supply"),
                                  ok(std::fabs(n(C, "surge_sum_dc") - n(C, "surge_sum_kadane")) < 1e-6));
    if (C.has("battery_after_cycles_kwh")) row("Project battery fade", "Fast powering, checked against repeated multiplication",
                                              "A 30 kWh pack keeps " + f2(n(C, "battery_after_cycles_kwh")) + " kWh after 1,500 cycles", ok(n(C, "battery_pow_diff") < 1e-9));
    if (C.has("hub_station_id")) row("Pick the overflow hub every station forwards to", "Celebrity elimination, checked against pairwise search",
                                    "Station " + fint(n(C, "hub_station_id")) + ", found with " + fint(n(C, "celebrity_queries_dc")) + " queries instead of " + fint(n(C, "celebrity_queries_naive")),
                                    ok(C["celebrity_agree"].truthy()));
    if (C.has("matmul_max_diff")) {
        char e[32];
        std::snprintf(e, sizeof e, "%.1e", n(C, "matmul_max_diff"));
        row("Propagate demand between 64 zones", "Strassen, checked against classical multiplication",
            "Four-step demand in " + fms(n(C, "matmul_strassen_ms")) + " against " + fms(n(C, "matmul_classic_ms")) + ", largest difference " + e, ok(n(C, "matmul_max_diff") < 1e-9));
    }
    fam("Greedy");
    if (G.has("chargers_needed")) row("Size chargers at each station", "Interval partitioning, checked against maximum overlap",
                                     "Station " + fint(n(G, "busiest_station_id")) + " needs " + fint(n(G, "chargers_needed")) + " chargers; " + fint(n(G, "served_network")) + " of " +
                                         fint(n(G, "sessions_network")) + " sessions fit the installed ports", ok(n(G, "chargers_needed") == n(G, "max_overlap")));
    if (G.has("huffman_bits_per_event")) row("Compress the station event log", "Huffman coding, decoded back to verify",
                                            f2(n(G, "huffman_bits_per_event")) + " bits per event, " + f1(n(G, "huffman_ratio") * 100) + "% of the raw size", ok(G["huffman_roundtrip"].truthy()));
    if (G.has("mst_prim_km")) row("Lay the cable backbone between stations", "Prim, checked against Kruskal", f1(n(G, "mst_prim_km")) + " km of cable either way",
                                 ok(std::fabs(n(G, "mst_prim_km") - n(G, "mst_kruskal_km")) < 1e-6));
    row("Route each leg by distance", "Dijkstra with a binary heap", R ? fint((double)R->run.sssp) + " shortest-path trees for this plan" : "\xE2\x80\x93", CHECK_STRATEGIES);
    fam("Dynamic programming");
    if (Pd.has("knapsack_dp_value")) row("Choose sites within the budget", "0/1 knapsack, compared with greedy",
                                        "Exact value " + fint(n(Pd, "knapsack_dp_value")) + ", greedy " + fint(n(Pd, "knapsack_greedy_value")) + " (" +
                                            f1(n(Pd, "knapsack_greedy_value") / std::max(1.0, n(Pd, "knapsack_dp_value")) * 100) + "%)",
                                        ok(n(Pd, "knapsack_dp_value") >= n(Pd, "knapsack_greedy_value")));
    if (Pd.has("matrix_chain_cost")) row("Order the demand projection pipeline", "Matrix-chain ordering",
                                        Pd["matrix_chain_order"].str() + " needs " + f1((1 - n(Pd, "matrix_chain_cost") / n(Pd, "matrix_chain_left")) * 100) + "% fewer multiplications than left to right",
                                        ok(n(Pd, "matrix_chain_cost") <= n(Pd, "matrix_chain_left")));
    if (Pd.has("lcs_adherence")) row("Measure how closely drivers follow the plan", "Longest common subsequence",
                                    f1(n(Pd, "lcs_adherence") * 100) + "% of planned junctions appear in the simulated GPS traces", CHECK_SINGLE);
    if (Pd.has("obst_expected_probes")) row("Index stations for lookups by id", "Optimal binary search tree, compared with a balanced tree",
                                           f2(n(Pd, "obst_expected_probes")) + " expected probes against " + f2(n(Pd, "balanced_expected_probes")),
                                           ok(n(Pd, "obst_expected_probes") <= n(Pd, "balanced_expected_probes") + 1e-9));
    if (Pd.has("tsp_held_karp_km")) row("Plan the maintenance van tour", "Held\xE2\x80\x93Karp, checked against brute force",
                                       f1(n(Pd, "tsp_held_karp_km")) + " km round trip in " + fms(n(Pd, "tsp_hk_ms")) + " against " + fms(n(Pd, "tsp_brute_ms")),
                                       ok(std::fabs(n(Pd, "tsp_held_karp_km") - n(Pd, "tsp_brute_km")) < 1e-6));
    row("Route each leg by energy", "Bellman\xE2\x80\x93" "Ford and Floyd\xE2\x80\x93Warshall", "Offered as routing strategies in the plan", CHECK_STRATEGIES);
    fam("Backtracking and branch and bound");
    if (B.has("nqueens8")) row("Validate the backtracking engine", "N-Queens on an 8\xC3\x97" "8 board", fint(n(B, "nqueens8")) + " solutions, the known answer is 92", ok(n(B, "nqueens8") == 92));
    if (B.has("leftover_budget")) {
        double sb = n(B, "subset_sum_backtracking");
        row("Spend the leftover budget exactly", "Subset-sum backtracking, checked against DP",
            rupees(n(B, "leftover_budget")) + " left: " + (sb == 1 ? "a set of unbuilt sites matches it exactly" : sb == 0 ? "no set of unbuilt sites matches it" : "search stopped at its node limit"),
            ok(sb == -1 || sb == n(B, "subset_sum_dp")));
    }
    if (B["bnb"].has("LC")) {
        const Json &b = B["bnb"];
        row("Search for the exact site selection", "Branch and bound: FIFO, LIFO and least cost",
            "Search nodes " + fint(n(b["FIFO"], "generated")) + ", " + fint(n(b["LIFO"], "generated")) + " and " + fint(n(b["LC"], "generated")) + "; all reach value " + fint(n(b["LC"], "value")),
            ok(n(b["FIFO"], "value") == n(b["LC"], "value") && n(b["LIFO"], "value") == n(b["LC"], "value") && n(b["LC"], "value") == n(Pd, "knapsack_dp_value")));
    }
    fam("Network flow");
    if (F.has("ff_dfs")) row("Allocate port slots to trips", "Ford\xE2\x80\x93" "Fulkerson, Edmonds\xE2\x80\x93Karp and bipartite matching",
                            fint(n(F, "ff_dfs")) + " of " + fint(n(F, "trips_needing")) + " charge-needing trips placed in " + fint(n(F, "slots")) + " slots",
                            ok(n(F, "ff_dfs") == n(F, "edmonds_karp") && n(F, "edmonds_karp") == n(F, "matching")));
    fam("Number theory and strings");
    if (N.has("crt_result")) {
        double r = n(N, "crt_result");
        row("Check whether maintenance windows collide", "Chinese remainder theorem",
            r == 1 ? "The four busiest stations go down together at hour " + fint(n(N, "crt_first_hour")) + ", then every " + fint(n(N, "crt_period")) + " hours"
                   : r == 0 ? std::string("The four busiest stations never go down at the same hour") : std::string("Not solvable with these inputs"),
            CHECK_SINGLE);
    }
    if (N["matching"].size()) {
        std::string res;
        bool all = true;
        for (size_t k = 0; k < N["matching"].size(); k++) {
            const Json &m = N["matching"][k];
            if (!res.empty()) res += "\n";
            res += "\xE2\x80\x9C" + m["pattern"].str() + "\xE2\x80\x9D: " + fint(m["hits"].num()) + " hits, KMP " + fsci(m["kmp_cmp"].num()) + " comparisons";
            all = all && m["agree"].truthy();
        }
        row("Detect fault patterns in the event log", "Naive, Rabin\xE2\x80\x93Karp, finite automaton and KMP", res, ok(all));
    }
    return rows;
}

/* ======================================================================== data model */
static std::string fixed(double v, int decimals) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

static std::string rec(std::initializer_list<std::pair<const char *, std::string>> kv) {
    std::string s = "{ ";
    bool first = true;
    for (auto &p : kv) { if (!first) s += ", "; s += std::string(p.first) + ": " + p.second; first = false; }
    return s + " }";
}

std::vector<RecordRow> modelRecords(const Engine &E, const Run &R, const TripDetail *D) {
    const City &city = E.cities[(size_t)E.plan.city];
    const Scene &sc = city.sc;
    const CfgV &cfg = sc.cfg;
    const NodeV &n0 = sc.nodes[0];
    const EdgeV &e0 = sc.edges[0];
    const SiteV *ex = sc.nexist ? &sc.stations[0] : nullptr;
    const SiteV &s0 = sc.stations[(size_t)(R.run.sel.n ? R.run.sel.idx[0] : sc.nexist < sc.ns ? sc.nexist : 0)];
    const TripV &tr = sc.trips[(size_t)(D && D->valid ? D->trip : (R.samples.empty() ? 0 : R.samples[0].trip))];
    const VehicleV &ve = sc.vehicles[(size_t)tr.vehicle];
    std::vector<RecordRow> v;
    v.push_back({ "Road node (junction)", "ev::RoadNode", "id, latitude/longitude, elevation, type, name", fint(sc.nn) + " nodes",
                  rec({ { "id", std::to_string(n0.id) }, { "name", "\"" + n0.name + "\"" }, { "lat", fixed(n0.lat, 4) }, { "lon", fixed(n0.lon, 4) }, { "elev", fint(n0.elev) + " m" } }),
                  "include/ev/models/road_network.hpp" });
    v.push_back({ "Road segment", "ev::RoadEdge", "two junctions, length in km, optional travel time and energy", fint(sc.ne) + " roads",
                  rec({ { "id", std::to_string(e0.id) }, { "from", sc.nodes[(size_t)e0.u].name }, { "to", sc.nodes[(size_t)e0.v].name }, { "km", f2(e0.km) } }),
                  "include/ev/models/road_network.hpp" });
    v.push_back({ "Existing charging station", "ev::ChargingStation", "junction, power per port, ports, connector, status (available, offline, maintenance)",
                  fint(sc.nexist) + " stations",
                  ex ? rec({ { "id", std::to_string(ex->id) }, { "node", sc.nodes[(size_t)ex->node].name }, { "power", fint(ex->power_kw) + " kW" }, { "ports", std::to_string(ex->ports) },
                             { "connector", ex->connector }, { "status", ex->status } })
                     : std::string("\xE2\x80\x93 (none in this city)"),
                  "include/ev/models/station.hpp" });
    v.push_back({ "Candidate site", "ev::CandidateStation", "junction, installation cost in exact paise, benefit score, ports, power, connector", fint(sc.candidates()) + " sites",
                  rec({ { "id", std::to_string(s0.id) }, { "node", sc.nodes[(size_t)s0.node].name }, { "cost", rupees(s0.cost) }, { "benefit", fint((double)s0.value) + (s0.estimated ? " (estimated)" : "") },
                        { "ports", std::to_string(s0.ports) }, { "power", fint(s0.power_kw) + " kW" } }),
                  "include/ev/models/station.hpp" });
    v.push_back({ "Electric vehicle", "ev::ElectricVehicle", "battery capacity, consumption per km, reserve, connector, charging power", fint(sc.nveh) + " vehicle models",
                  rec({ { "id", ve.id }, { "battery", f1(ve.battery_kwh) + " kWh" }, { "use", f2(ve.consumption) + " kWh/km" }, { "reserve", f1(ve.reserve_kwh) + " kWh" },
                        { "connector", ve.connector } }),
                  "include/ev/models/vehicle.hpp" });
    v.push_back({ "Trip", "ev::TripRequest", "vehicle, origin, destination, departure time, starting charge", fint(sc.nt) + " trips a day",
                  rec({ { "id", std::to_string(tr.id) }, { "vehicle", tr.vehicle_id }, { "from", sc.nodes[(size_t)tr.src].name }, { "to", sc.nodes[(size_t)tr.dst].name },
                        { "depart", fclock(tr.depart_min) }, { "charge", f1(tr.start_kwh) + " kWh" } }),
                  "include/ev/models/trip.hpp" });
    v.push_back({ "Scenario settings", "ev::ScenarioConfig", "budget, charge target, regeneration, climb energy, speed, grid supply, data source", "one per city",
                  rec({ { "budget", rupees(city.default_budget) }, { "target", fint(cfg.target_frac * 100) + "%" }, { "regen", fint(cfg.regen_eff * 100) + "%" },
                        { "climb", fixed(cfg.elev_kwh_per_m, 4) + " kWh/m" }, { "source", cfg.source } }),
                  "include/ev/models/scenario.hpp" });
    v.push_back({ "Trip plan", "ev::TripPlan", "status and reason, route, charging stops with arrival time and battery, totals", fint(R.run.kpi.feasible) + " feasible plans",
                  D && D->valid ? rec({ { "status", D->plan.status_name }, { "km", f1(D->plan.km) }, { "kWh", f2(D->plan.kwh) }, { "stops", std::to_string(D->plan.n_stops) },
                                        { "arrive", fclock(D->plan.arrive_min) } })
                                : std::string("\xE2\x80\x93"),
                  "include/ev/planner/trip_plan.hpp" });
    return v;
}

std::vector<StructureRow> modelStructures(const Engine &E, const Run &R, const Analysis &A) {
    const Scene &sc = E.cities[(size_t)E.plan.city].sc;
    const Json &G = A.data["greedy"], &Pd = A.data["dynamic_programming"], &Bn = A.data["backtracking_bnb"]["bnb"]["LC"];
    int arcs = 2 * sc.ne;
    int sites = (int)R.run.sites.size();
    std::vector<StructureRow> v;
    v.push_back({ "Vectors of records", "every record above", fint(sc.nn + sc.ne + sc.ns + sc.nt + sc.nveh) + " records in std::vector",
                  "Each kind of record is a std::vector inside ev::Scenario; node ids map to positions through an unordered_map index.", "include/ev/models/scenario.hpp" });
    v.push_back({ "Road graph (CSR arcs)", "nodes, roads, elevation", fint(sc.nn) + " vertices, " + fint(arcs) + " arcs (each road both ways)",
                  "Arcs grouped by tail with travel time and reference energy. Downhill arcs can be negative (regenerative braking), so energy routing uses Bellman\xE2\x80\x93" "Ford.",
                  "include/ev/planner/energy_model.hpp" });
    v.push_back({ "Weighted graph", "the road arcs", fint(sc.nn) + " vertices, " + fint(arcs) + " arcs",
                  "Adjacency lists with edge ids. Dijkstra and Floyd\xE2\x80\x93Warshall route by distance on it; Bellman\xE2\x80\x93" "Ford by energy.", "include/ev/graph/graph.hpp" });
    v.push_back({ "Charging-leg graph", "trip ends and charger sites", "up to " + fint(sites + 2) + " vertices per trip",
                  "Edges join points the vehicle can drive between above its reserve; the best plan is a shortest path here.", "include/ev/planner/leg_planner.hpp" });
    v.push_back({ "Battery-state graph", "junctions \xC3\x97 battery levels", fint(sc.nn) + " junctions \xC3\x97 battery steps of " + f2(sc.cfg.battery_step_kwh) + " kWh",
                  "The exact planner runs Dijkstra over (junction, battery level) states; used to verify the leg planner.", "include/ev/planner/exact_planner.hpp" });
    v.push_back({ "Capacitated graph", "trips, charger sites, ports", fint(R.cap.trips_needing + sites + 2) + " vertices; " + fint((double)R.cap.slots_total) + " port slots of capacity",
                  "Source to each trip (capacity 1), trip to each reachable site, site to sink (its port slots). Max flow: " + fint((double)R.cap.flow_dfs) +
                      " trips, matched by Ford\xE2\x80\x93" "Fulkerson, Edmonds\xE2\x80\x93Karp and bipartite matching.", "include/ev/graph/max_flow.hpp" });
    v.push_back({ "Binary heap (tree)", "arc distances; simulation events", fint((double)R.run.sssp) + " shortest-path trees for this plan",
                  "Hand-written priority queue for Dijkstra and Prim; the day simulation keeps its arrive and finish events in the same heap.", "include/ev/algorithms/heap.hpp" });
    v.push_back({ "Branch-and-bound tree", "candidate costs and benefits, budget",
                  Bn.has("generated") ? fint(Bn["generated"].num()) + " nodes generated, " + fint(Bn["expanded"].num()) + " expanded (least cost)" : "computed with the report",
                  "Search tree over build or skip decisions, pruned by a fractional-knapsack bound.", "include/ev/algorithms/branch_and_bound.hpp" });
    v.push_back({ "Huffman tree", "station event log",
                  G.has("huffman_symbols") ? fint(G["huffman_symbols"].num()) + " symbols, " + f2(G["huffman_bits_per_event"].num()) + " bits per event" : "computed with the report",
                  "Compresses the event log; decoding it back is checked.", "include/ev/algorithms/greedy.hpp" });
    v.push_back({ "Optimal binary search tree", "station ids weighted by use",
                  Pd.has("obst_expected_probes") ? f2(Pd["obst_expected_probes"].num()) + " expected probes (balanced tree " + f2(Pd["balanced_expected_probes"].num()) + ")" : "computed with the report",
                  "Lookup index for station ids, cheaper for the busiest sites.", "include/ev/algorithms/dynamic_programming.hpp" });
    v.push_back({ "Union\xE2\x80\x93" "find forest", "charger sites, distances between them",
                  G.has("mst_kruskal_km") ? "spanning tree of " + f1(G["mst_kruskal_km"].num()) + " km" : "computed with the report",
                  "Kruskal joins sites into a cable backbone; Prim checks the same length.", "include/ev/graph/spanning_tree.hpp" });
    return v;
}

/* ======================================================================== trips */
std::string tripFacts(const Scene &sc, const TripDetail &D) {
    const TripV &tr = sc.trips[(size_t)D.trip];
    std::string facts = "Vehicle **" + tr.vehicle_id + "** (" + f1(tr.battery_kwh) + " kWh) departs **" + fclock(tr.depart_min) + "** at **" + fint(tr.soc0 * 100) + "%** charge. ";
    if (D.plan.feasible) {
        facts += "**" + f1(D.plan.km) + " km**, " + f1(D.plan.kwh) + " kWh. ";
        facts += D.plan.n_stops ? "**" + std::to_string(D.plan.n_stops) + (D.plan.n_stops > 1 ? " stops**, " : " stop**, ") + fint(D.plan.charge_min) + " min charging. "
                                : std::string("**No charging needed.** ");
        facts += "Arrives **" + fclock(D.plan.arrive_min) + "**.";
    } else facts += "**Not possible:** " + D.plan.status_name + ".";
    return facts;
}

std::vector<std::pair<std::string, std::string>> tripItinerary(const Scene &sc, const TripDetail &D) {
    std::vector<std::pair<std::string, std::string>> v;
    const TripV &tr = sc.trips[(size_t)D.trip];
    const std::string &src = sc.nodes[(size_t)tr.src].name, &dst = sc.nodes[(size_t)tr.dst].name;
    v.push_back({ fclock(tr.depart_min), "Leave **" + src + "** in vehicle " + tr.vehicle_id + " with " + f1(tr.start_kwh) + " of " + f1(tr.battery_kwh) + " kWh" });
    for (int q = 0; q < D.plan.n_stops; q++) {
        const SiteV &st = sc.stations[(size_t)D.plan.stop[(size_t)q]];
        v.push_back({ fclock(D.plan.stop_arrive_min[(size_t)q]), "Stop " + std::to_string(q + 1) + " at **station " + std::to_string(st.id) + "** (" + sc.nodes[(size_t)st.node].name +
                                                                     "), arriving with " + f1(D.plan.stop_arrive_kwh[(size_t)q]) + " kWh. Charge " + fint(D.plan.stop_charge_min[(size_t)q]) +
                                                                     " min at " + fint(st.power_kw) + " kW to " + f1(D.plan.stop_depart_kwh[(size_t)q]) + " kWh." });
    }
    if (D.plan.feasible) v.push_back({ fclock(D.plan.arrive_min), "Arrive at **" + dst + "** with " + f1(D.plan.arrive_kwh) + " kWh, " + f1(D.plan.km) + " km after departure" });
    else v.push_back({ "", "**" + D.plan.status_name + ".** " + D.plan.explanation });
    return v;
}

/* ======================================================================== live day */
std::string liveSummary(Engine &E, const SimView &cur, const SimView *other, int policy) {
    const SimV &sm = cur.sim;
    std::string sum = sm.delayed
        ? "Over the day **" + fint(sm.delayed) + " of " + fint(sm.nv) + " drivers** queue for a port at least once, **" + fdur(sm.total_wait) +
              "** of waiting in all. The longest single wait is **" + fdur(sm.max_wait) + "**. Simulated in " + fms(sm.ms) + " from " + fint((double)sm.events) + " events."
        : "With the ports in this plan nobody has to queue: all " + fint(sm.nv) + " drivers charge on arrival. Simulated in " + fms(sm.ms) + " from " +
              fint((double)sm.events) + " events.";
    if (other) {
        const SimV &w = policy == 0 ? sm : other->sim, &d = policy == 0 ? other->sim : sm;
        if (w.total_wait > 0)
            sum += d.diverted ? " Letting drivers switch station after 20 minutes moves " + fint(d.diverted) + " of them and changes total waiting from " +
                                    fdur(w.total_wait) + " to " + fdur(d.total_wait) + "; the longest wait goes from " + fdur(w.max_wait) + " to " + fdur(d.max_wait) + "."
                              : std::string(" No queue here is long enough for a switch to another station to pay off.");
    }
    if (E.baseline.set && E.baseline.plan.city == E.plan.city) {
        double bw = E.baselineWait(policy);
        sum += " Against your baseline (" + E.baseline.label + "): " + fdur(bw) + " of waiting then, " + fdur(sm.total_wait) + " now.";
    }
    return sum;
}

/* ======================================================================== strategies */
const CompareRow *findCompare(const Compare &C, int sm, int rm) {
    for (auto &r : C.rows) if (r.sel == sm && r.route == rm) return &r;
    return nullptr;
}

std::string strategiesFinding(const Compare &C) {
    std::string fnd;
    const CompareRow *dpD = findCompare(C, SEL_DP, ROUTE_DIJKSTRA), *grD = findCompare(C, SEL_GREEDY, ROUTE_DIJKSTRA);
    const CompareRow *flD = findCompare(C, SEL_DP, ROUTE_FLOYD), *beD = findCompare(C, SEL_DP, ROUTE_BELLMAN);
    if (dpD && grD) {
        int diff = dpD->feasible - grD->feasible;
        fnd += diff > 0 ? "Exact selection completes **" + fint(diff) + " more trips** than greedy (" + fint(dpD->feasible) + " against " + fint(grD->feasible) + "). "
             : diff < 0 ? "Greedy happens to complete " + fint(-diff) + " more trips here, because the knapsack value only approximates trips served. "
                        : std::string("Greedy reaches the same plan as exact selection on this city. ");
    }
    if (dpD && flD) fnd += "Floyd\xE2\x80\x93Warshall plans the same routes as Dijkstra in **" + f1(flD->route_ms / std::max(1e-9, dpD->route_ms)) + "\xC3\x97** the time. ";
    if (dpD && beD) {
        double save = (1 - beD->avg_kwh / dpD->avg_kwh) * 100;
        fnd += save > 0.005 ? "Routing on energy with Bellman\xE2\x80\x93" "Ford uses **" + f2(save) + "% less energy** per trip."
                            : std::string("Routing on energy with Bellman\xE2\x80\x93" "Ford matches Dijkstra on energy here.");
    }
    return fnd;
}

/* ======================================================================== capacity */
std::string surgeNote(const SessV &se) {
    return se.surge_lo >= 0
        ? "From **" + fclock(se.surge_lo * 60.0) + " to " + fclock((se.surge_hi + 1) * 60.0) + "** the plan draws **" + fint(se.surge_excess_kwh) +
              " kWh** more than the grid supplies. Staggering sessions or adding storage at the busiest stations closes the gap."
        : std::string("Grid supply covers every hour of the plan.");
}

std::string flowNote(const Scene &sc, const CapV &c) {
    std::string t = fint((double)c.flow_dfs) + " of the " + fint(c.trips_needing) + " trips that need a charge fit into the " + fint((double)c.slots_total) +
                    " port slots available in a two-hour window.";
    if (c.bottleneck_station >= 0) t += " The tightest station is **" + std::to_string(sc.stations[c.bottleneck_station].id) + "**.";
    return t;
}

/* ======================================================================== benchmarks */
const std::vector<Family> FAMILIES = {
    { "Sorting", { "sorting" } }, { "Searching", { "searching" } }, { "Shortest paths", { "shortest_path" } }, { "Spanning trees", { "spanning_tree" } },
    { "Dense graphs", { "dense_graph" } }, { "Station placement", { "placement" } }, { "Session scheduling", { "scheduling" } }, { "Matrix multiply", { "matrix" } },
};

std::string algName(const std::string &a) {
    static const std::map<std::string, std::string> M = {
        { "merge_sort", "Merge sort" }, { "quick_sort_median3", "Quick sort, median-of-3" }, { "quick_sort_first", "Quick sort, first pivot" },
        { "quick_sort_random", "Quick sort, random pivot" }, { "binary_search_iterative", "Binary search, iterative" },
        { "binary_search_recursive", "Binary search, recursive" }, { "dijkstra", "Dijkstra" }, { "bellman_ford", "Bellman\xE2\x80\x93" "Ford" },
        { "floyd_warshall", "Floyd\xE2\x80\x93Warshall" }, { "dijkstra_all_sources", "Dijkstra from every source" },
        { "prim", "Prim (binary heap)" }, { "prim_dense", "Prim (matrix, O(V\xC2\xB2))" }, { "kruskal", "Kruskal" }, { "knapsack_dp", "Knapsack DP (exact)" },
        { "knapsack_bnb_lc", "Branch and bound, least cost (exact)" }, { "greedy_ratio", "Greedy ratio (heuristic)" },
        { "activity_selection", "Activity selection" }, { "interval_partitioning", "Interval partitioning" }, { "matmul_classical", "Classical" },
        { "matmul_strassen_cutoff64", "Strassen, cutoff 64" }, { "exact_state_space", "Exact planner (battery states)" },
        { "leg_planner_dijkstra", "Leg planner (Dijkstra legs)" },
    };
    auto it = M.find(a);
    return it == M.end() ? a : it->second;
}

// One curve per algorithm: keep the typical input of each family and leave the special cases
// (sorted input, equal weights, disconnected graphs, other budgets) to the notes beside the chart.
bool benchSkip(const BenchRow &r) {
    if (r.family == "sorting") return r.variant != "random";
    if (r.family == "shortest_path" || r.family == "spanning_tree") return r.variant != "connected-varied";
    if (r.family == "placement") return r.variant != "budget 25% of total cost";
    return r.family == "trip_planning";
}

std::vector<std::pair<std::string, std::string>> adversarialNotes(const std::vector<BenchRow> &rows) {
    std::vector<std::pair<std::string, std::string>> v;
    auto find = [&](const char *alg, const char *variant, double n) -> const BenchRow * {
        const BenchRow *best = nullptr;
        for (auto &r : rows)
            if (r.alg == alg && r.variant == variant && (n <= 0 ? (!best || r.n > best->n) : r.n == n)) best = &r;
        return best;
    };
    auto findDs = [&](const char *alg, const std::string &ds) -> const BenchRow * {
        for (auto &r : rows) if (r.alg == alg && r.dataset == ds) return &r;
        return nullptr;
    };
    const BenchRow *qf = find("quick_sort_first", "sorted", 0);
    if (qf) {
        const BenchRow *qm = find("quick_sort_median3", "sorted", qf->n), *ms = find("merge_sort", "sorted", qf->n);
        if (qm) v.push_back({ "Quick sort on a list that is already sorted", "With the first element as pivot, " + fint(qf->n) + " sorted keys take " + fms(qf->ms) +
                                                                               " (the n\xC2\xB2 worst case). A median-of-three pivot needs " + fms(qm->ms) +
                                                                               (ms ? ", merge sort " + fms(ms->ms) : std::string()) + "." });
    }
    const BenchRow *fw = find("floyd_warshall", "connected-varied", 0);
    if (fw) {
        const BenchRow *dj = find("dijkstra", "connected-varied", fw->n), *bf = find("bellman_ford", "connected-varied", fw->n);
        if (dj) v.push_back({ "All pairs or one source", "Floyd\xE2\x80\x93Warshall fills the whole " + fint(fw->n) + "\xC3\x97" + fint(fw->n) + " table in " + fms(fw->ms) +
                                                             " and " + fbytes((double)fw->peak) + "; one Dijkstra tree takes " + fms(dj->ms) +
                                                             (bf ? " and Bellman\xE2\x80\x93" "Ford " + fms(bf->ms) : std::string()) + ". Above 1,600 junctions the app does not offer Floyd\xE2\x80\x93Warshall." });
    }
    const BenchRow *fd = find("floyd_warshall", "dense", 0);
    if (fd) {
        const BenchRow *da = find("dijkstra_all_sources", "dense", fd->n);
        if (da) v.push_back({ "Dense graphs turn the tables", "With half of all junction pairs joined (" + fint(fd->n) + " junctions), Floyd\xE2\x80\x93Warshall needs " + fms(fd->ms) +
                                                                 " for every distance, while Dijkstra from every source needs " + fms(da->ms) + ": the simple triple loop wins when E is close to V\xC2\xB2." });
    }
    const BenchRow *pd = find("prim", "disconnected", 0);
    if (pd) v.push_back({ "Four separate road networks", "On " + fint(pd->n) + " junctions in four unconnected parts, Prim and Kruskal return a " + pd->note.substr(0, pd->note.find(',')) +
                                                             " instead of failing; Dijkstra reports the other parts as unreachable." });
    const BenchRow *gr = find("greedy_ratio", "budget 25% of total cost", 0);
    if (gr) {
        const BenchRow *dp = find("knapsack_dp", "budget 25% of total cost", gr->n), *lc = find("knapsack_bnb_lc", "budget 25% of total cost", gr->n);
        if (dp && lc) v.push_back({ "Choosing among " + fint(gr->n) + " candidate sites", "Greedy takes " + fms(gr->ms) + " and reaches " + gr->note.substr(gr->note.find('=') == std::string::npos ? 0 : gr->note.find('=') + 2) +
                                                                                              ". The exact methods agree on the optimum: dynamic programming in " + fms(dp->ms) + ", least-cost branch and bound in " +
                                                                                              fms(lc->ms) + " (" + lc->note + ")." });
    }
    const BenchRow *ip = find("interval_partitioning", "one day", 0);
    if (ip) v.push_back({ "A million sessions in one day", "Interval partitioning sizes the bays in " + fms(ip->ms) + ": " + ip->note + "." });
    const BenchRow *ex = findDs("exact_state_space", "scenario-large"), *lg = findDs("leg_planner_dijkstra", "scenario-large");
    if (ex && lg) v.push_back({ "Exact planner against leg planner", "On 200 trips of the large city the exact battery-state planner takes " + fms(ex->ms) + " (" + ex->note.substr(0, ex->note.find(" (")) +
                                                                          "); the leg planner used by the app takes " + fms(lg->ms) + " (" + lg->note.substr(0, lg->note.find(" (")) +
                                                                          "). Both only report plans they can verify." });
    return v;
}

} // namespace ev33
