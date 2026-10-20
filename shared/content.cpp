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

const int SEL_ORDER[EV_SEL_COUNT] = { EV_SEL_DP, EV_SEL_BNB_LC, EV_SEL_BNB_FIFO, EV_SEL_BNB_LIFO, EV_SEL_GREEDY };

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
    row("Route each leg by distance", "Dijkstra with a binary heap", R ? fint((double)ev_router_sssp_runs(R->run.router)) + " shortest-path trees for this plan" : "\xE2\x80\x93", CHECK_STRATEGIES);
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
static std::string rec(std::initializer_list<std::pair<const char *, std::string>> kv) {
    std::string s = "{ ";
    bool first = true;
    for (auto &p : kv) { if (!first) s += ", "; s += std::string(p.first) + ": " + p.second; first = false; }
    return s + " }";
}

std::vector<RecordRow> modelRecords(const Engine &E, const Run &R, const TripDetail *D) {
    const City &city = E.cities[(size_t)E.plan.city];
    const ev_scenario &sc = city.sc;
    const ev_config &cfg = sc.cfg;
    const ev_node &n0 = sc.nodes[0];
    const ev_edge &e0 = sc.edges[0];
    const ev_station &s0 = sc.stations[R.run.sel.n ? R.run.sel.idx[0] : 0];
    const ev_trip &tr = sc.trips[D && D->valid ? D->trip : (R.samples.empty() ? 0 : R.samples[0].trip)];
    std::vector<RecordRow> v;
    v.push_back({ "Road node (junction)", "ev_node", "position in km, elevation in m, trip demand", fint(sc.nn) + " nodes",
                  rec({ { "name", std::string("\"") + n0.name + "\"" }, { "x", f2(n0.x) + " km" }, { "y", f2(n0.y) + " km" }, { "elev", fint(n0.elev) + " m" }, { "demand", fint(n0.demand) } }),
                  "include/ev_model.h" });
    v.push_back({ "Road segment", "ev_edge", "two junctions, length in km, capacity", fint(sc.ne) + " roads",
                  rec({ { "u", sc.nodes[e0.u].name }, { "v", sc.nodes[e0.v].name }, { "km", f2(e0.km) }, { "capacity", fint(e0.capacity) } }), "include/ev_model.h" });
    v.push_back({ "Charging station site", "ev_station", "junction, installation cost, ports, power per port, maintenance window, service value", fint(sc.ns) + " candidate sites",
                  rec({ { "id", std::to_string(s0.id) }, { "node", sc.nodes[s0.node].name }, { "cost", rupees(s0.cost) }, { "ports", std::to_string(s0.ports) },
                        { "power", fint(s0.power_kw) + " kW" }, { "value", fint(s0.value) } }), "include/ev_model.h" });
    v.push_back({ "Trip", "ev_trip", "origin, destination, departure time, starting battery level", fint(sc.nt) + " trips a day",
                  rec({ { "id", std::to_string(tr.id) }, { "src", sc.nodes[tr.src].name }, { "dst", sc.nodes[tr.dst].name }, { "depart", fclock(tr.depart_min) }, { "soc0", fint(tr.soc0 * 100) + "%" } }),
                  "include/ev_model.h" });
    v.push_back({ "Battery and budget", "ev_config", "pack size, consumption, reserve, charge target, regeneration, speed, grid supply, installation budget", "one per city",
                  rec({ { "battery", f1(cfg.battery_kwh) + " kWh" }, { "use", f2(cfg.cons_kwh_km) + " kWh/km" }, { "reserve", fint(cfg.reserve_frac * 100) + "%" },
                        { "target", fint(cfg.target_frac * 100) + "%" }, { "regen", fint(cfg.regen_eff * 100) + "%" }, { "budget", rupees(city.default_budget) } }),
                  "include/ev_model.h" });
    v.push_back({ "Planned trip", "ev_plan", "route, charging stops with arrival time and battery, totals", fint(R.run.kpi.feasible) + " feasible plans",
                  D && D->valid ? rec({ { "km", f1(D->plan.km) }, { "kWh", f2(D->plan.kwh) }, { "stops", std::to_string(D->plan.n_stops) }, { "charge", f1(D->plan.charge_min) + " min" },
                                        { "arrive", fclock(D->plan.arrive_min) } })
                                : std::string("\xE2\x80\x93"),
                  "include/ev_plan.h" });
    return v;
}

std::vector<StructureRow> modelStructures(const Engine &E, const Run &R, const Analysis &A) {
    const ev_scenario &sc = E.cities[(size_t)E.plan.city].sc;
    const Json &G = A.data["greedy"], &Pd = A.data["dynamic_programming"], &Bn = A.data["backtracking_bnb"]["bnb"]["LC"];
    int arcs = 2 * sc.ne;
    std::vector<StructureRow> v;
    v.push_back({ "Arrays of records", "every record above", fint(sc.nn + sc.ne + sc.ns + sc.nt) + " records in contiguous arrays",
                  "All data lives in typed arrays inside ev_scenario; junction ids are kept in a merge-sorted index searched by binary search.", "include/ev_model.h" });
    v.push_back({ "Weighted graph, distance", "nodes and roads", fint(sc.nn) + " vertices, " + fint(arcs) + " arcs (each road both ways)",
                  "Adjacency in compressed (CSR) form, weight = km. Dijkstra and Floyd\xE2\x80\x93Warshall route on it.", "include/ev_graph.h" });
    v.push_back({ "Weighted graph, energy", "nodes, roads, elevation, battery config", fint(sc.nn) + " vertices, " + fint(arcs) + " arcs",
                  "Same arcs, weight = kWh. Downhill arcs are negative because of regenerative braking, so Bellman\xE2\x80\x93" "Ford routes on it.", "src/domain/ev_model.c" });
    v.push_back({ "Charging-leg graph", "trip ends and built stations", "up to " + fint(R.run.sel.n + 2) + " vertices per trip",
                  "Edges join points the battery can reach between reserve and target charge; the best plan is a shortest path here.", "src/domain/ev_plan.c" });
    v.push_back({ "Capacitated graph", "trips, built stations, ports", fint(R.cap.trips_needing + R.run.sel.n + 2) + " vertices; " + fint((double)R.cap.slots_total) + " port slots of capacity",
                  "Source to each trip (capacity 1), trip to each reachable station, station to sink (its port slots). Max flow: " + fint((double)R.cap.flow_dfs) +
                      " trips, matched by Ford\xE2\x80\x93" "Fulkerson, Edmonds\xE2\x80\x93Karp and bipartite matching.", "include/ev_flow.h" });
    v.push_back({ "Binary heap (tree)", "arc distances; simulation events", fint((double)ev_router_sssp_runs(R.run.router)) + " shortest-path trees for this plan",
                  "Priority queue for Dijkstra; the day simulation keeps its arrive and finish events in the same kind of heap.", "src/lib/ev_graph.c" });
    v.push_back({ "Branch-and-bound tree", "station costs and values, budget",
                  Bn.has("generated") ? fint(Bn["generated"].num()) + " nodes generated, " + fint(Bn["expanded"].num()) + " expanded (least cost)" : "computed with the report",
                  "Search tree over build or skip decisions, pruned by a fractional-knapsack bound.", "src/lib/ev_search.c" });
    v.push_back({ "Huffman tree", "station event log",
                  G.has("huffman_symbols") ? fint(G["huffman_symbols"].num()) + " symbols, " + f2(G["huffman_bits_per_event"].num()) + " bits per event" : "computed with the report",
                  "Compresses the event log; decoding it back is checked.", "src/lib/ev_greedy.c" });
    v.push_back({ "Optimal binary search tree", "station ids weighted by value",
                  Pd.has("obst_expected_probes") ? f2(Pd["obst_expected_probes"].num()) + " expected probes (balanced tree " + f2(Pd["balanced_expected_probes"].num()) + ")" : "computed with the report",
                  "Lookup index for station ids, cheaper for the busiest sites.", "src/lib/ev_dp.c" });
    v.push_back({ "Union\xE2\x80\x93" "find forest", "built stations, distances between them",
                  G.has("mst_kruskal_km") ? "spanning tree of " + f1(G["mst_kruskal_km"].num()) + " km" : "computed with the report",
                  "Kruskal joins stations into a cable backbone; Prim checks the same length.", "src/lib/ev_graph.c" });
    return v;
}

/* ======================================================================== trips */
std::string tripFacts(const ev_scenario &sc, const TripDetail &D) {
    const ev_trip &tr = sc.trips[D.trip];
    std::string facts = "Departs **" + fclock(tr.depart_min) + "** at **" + fint(tr.soc0 * 100) + "%** charge. ";
    if (D.plan.feasible) {
        facts += "**" + f1(D.plan.km) + " km**, " + f1(D.plan.kwh) + " kWh. ";
        facts += D.plan.n_stops ? "**" + std::to_string(D.plan.n_stops) + (D.plan.n_stops > 1 ? " stops**, " : " stop**, ") + fint(D.plan.charge_min) + " min charging. "
                                : std::string("**No charging needed.** ");
        facts += "Arrives **" + fclock(D.plan.arrive_min) + "**.";
    } else facts += "**Not possible** with the stations in this plan.";
    return facts;
}

std::vector<std::pair<std::string, std::string>> tripItinerary(const ev_scenario &sc, const TripDetail &D) {
    std::vector<std::pair<std::string, std::string>> v;
    const ev_trip &tr = sc.trips[D.trip];
    double B = sc.cfg.battery_kwh;
    const char *src = sc.nodes[tr.src].name, *dst = sc.nodes[tr.dst].name;
    v.push_back({ fclock(tr.depart_min), "Leave **" + std::string(src) + "** with " + f1(tr.soc0 * B) + " kWh" });
    for (int q = 0; q < D.plan.n_stops; q++) {
        const ev_station &st = sc.stations[D.plan.stop[q]];
        v.push_back({ fclock(D.plan.stop_arrive_min[q]), "Stop " + std::to_string(q + 1) + " at **station " + std::to_string(st.id) + "** (" + sc.nodes[st.node].name +
                                                               "), arriving with " + f1(D.plan.stop_arrive_kwh[q]) + " kWh. Charge " + fint(D.plan.stop_charge_min[q]) +
                                                               " min at " + fint(st.power_kw) + " kW to " + f1(sc.cfg.target_frac * B) + " kWh." });
    }
    if (D.plan.feasible) v.push_back({ fclock(D.plan.arrive_min), "Arrive at **" + std::string(dst) + "**, " + f1(D.plan.km) + " km after departure" });
    else v.push_back({ "", "No chain of built stations is within battery range. The direct route is " + f1(D.plan.direct_km) +
                               " km; usable energy at departure is " + f1(tr.soc0 * B - sc.cfg.reserve_frac * B) + " kWh." });
    return v;
}

/* ======================================================================== live day */
std::string liveSummary(Engine &E, const SimView &cur, const SimView *other, int policy) {
    const ev_sim &sm = cur.sim;
    std::string sum = sm.delayed
        ? "Over the day **" + fint(sm.delayed) + " of " + fint(sm.nv) + " drivers** queue for a port at least once, **" + fdur(sm.total_wait) +
              "** of waiting in all. The longest single wait is **" + fdur(sm.max_wait) + "**. Simulated in " + fms(sm.ms) + " from " + fint((double)sm.events) + " events."
        : "With the ports in this plan nobody has to queue: all " + fint(sm.nv) + " drivers charge on arrival. Simulated in " + fms(sm.ms) + " from " +
              fint((double)sm.events) + " events.";
    if (other) {
        const ev_sim &w = policy == 0 ? sm : other->sim, &d = policy == 0 ? other->sim : sm;
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
    const CompareRow *dpD = findCompare(C, EV_SEL_DP, EV_ROUTE_DIJKSTRA), *grD = findCompare(C, EV_SEL_GREEDY, EV_ROUTE_DIJKSTRA);
    const CompareRow *flD = findCompare(C, EV_SEL_DP, EV_ROUTE_FLOYD), *beD = findCompare(C, EV_SEL_DP, EV_ROUTE_BELLMAN);
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
std::string surgeNote(const ev_sessions &se) {
    return se.surge_lo >= 0
        ? "From **" + fclock(se.surge_lo * 60.0) + " to " + fclock((se.surge_hi + 1) * 60.0) + "** the plan draws **" + fint(se.surge_excess_kwh) +
              " kWh** more than the grid supplies. Staggering sessions or adding storage at the busiest stations closes the gap."
        : std::string("Grid supply covers every hour of the plan.");
}

std::string flowNote(const ev_scenario &sc, const ev_capacity &c) {
    std::string t = fint((double)c.flow_dfs) + " of the " + fint(c.trips_needing) + " trips that need a charge fit into the " + fint((double)c.slots_total) +
                    " port slots available in a two-hour window.";
    if (c.bottleneck_station >= 0) t += " The tightest station is **" + std::to_string(sc.stations[c.bottleneck_station].id) + "**.";
    return t;
}

/* ======================================================================== benchmarks */
const std::vector<Family> FAMILIES = {
    { "Sorting", { "sort" } }, { "Searching", { "search" } }, { "Maximum subarray", { "maxsubarray" } }, { "Matrix multiply", { "matmul" } },
    { "All-pairs paths", { "apsp" } }, { "Spanning trees", { "mst" } }, { "Interval scheduling", { "intervals" } }, { "Knapsack", { "knapsack" } },
    { "Travelling salesman", { "tsp" } }, { "Max-flow", { "flow" } }, { "String matching", { "strings" } }, { "LCS and optimal BST", { "lcs", "obst" } },
};

std::string algName(const std::string &a) {
    static const std::map<std::string, std::string> M = {
        { "merge", "Merge sort" }, { "quick-median3", "Quick sort, median-of-3" }, { "quick-first", "Quick sort, first pivot" },
        { "quick-random", "Quick sort, random pivot" }, { "libc-qsort", "C library qsort" }, { "binary-iterative", "Binary search, iterative" },
        { "binary-recursive", "Binary search, recursive" }, { "linear-scan", "Linear scan" }, { "divide-conquer", "Divide and conquer" },
        { "kadane", "Kadane" }, { "brute-n2", "Brute force, n\xC2\xB2" }, { "classical", "Classical" }, { "strassen-cut64", "Strassen, cutoff 64" },
        { "strassen-cut16", "Strassen, cutoff 16" }, { "dijkstra-x-n", "Dijkstra from every source" }, { "floyd-warshall", "Floyd\xE2\x80\x93Warshall" },
        { "prim-dense", "Prim, dense" }, { "kruskal", "Kruskal" }, { "activity-selection", "Activity selection" }, { "interval-partition", "Interval partitioning" },
        { "dp-table", "Dynamic programming" }, { "bnb-FIFO", "Branch and bound, FIFO" }, { "bnb-LIFO", "Branch and bound, LIFO" },
        { "bnb-LC", "Branch and bound, least cost" }, { "greedy-ratio", "Greedy ratio" }, { "held-karp", "Held\xE2\x80\x93Karp" }, { "brute-force", "Brute force" },
        { "bipartite-kuhn", "Bipartite matching" }, { "ford-fulkerson-dfs", "Ford\xE2\x80\x93" "Fulkerson, DFS" }, { "edmonds-karp-bfs", "Edmonds\xE2\x80\x93Karp, BFS" },
        { "naive", "Naive" }, { "rabin-karp", "Rabin\xE2\x80\x93Karp" }, { "finite-automaton", "Finite automaton" }, { "kmp", "KMP" },
    };
    auto it = M.find(a);
    return it == M.end() ? a : it->second;
}

bool benchSkip(const std::string &ds) {
    return ds == "adversarial" || (ds.size() > 4 && ds.compare(ds.size() - 4, 4, "-neg") == 0) || ds == "weak-corr";
}

std::vector<std::pair<std::string, std::string>> adversarialNotes(const std::vector<BenchRow> &rows) {
    std::vector<std::pair<std::string, std::string>> v;
    auto find = [&](const char *fam, const char *alg, const std::string &ds) -> const BenchRow * {
        for (auto &r : rows) if (r.family == fam && r.alg == alg && r.dataset == ds) return &r;
        return nullptr;
    };
    const BenchRow *qf = find("sort", "quick-first", "adversarial"), *qm = find("sort", "quick-median3", "adversarial");
    if (qf && qm) v.push_back({ "Quick sort on a list that is already sorted", "With the first element as pivot it makes " + fsci(qf->quality) + " comparisons in " + fms(qf->ms) +
                                                                                   ", the n\xC2\xB2/2 worst case. A median-of-three pivot needs " + fms(qm->ms) + "." });
    const BenchRow *sn = find("strings", "naive", "adversarial"), *sk = find("strings", "kmp", "adversarial");
    if (sn && sk) v.push_back({ "A fault burst in the event log", "Searching a million F events for F\xE2\x80\xA6" "FR costs the naive matcher " + fsci(sn->quality) + " comparisons (" +
                                                                       fms(sn->ms) + "). KMP needs " + fsci(sk->quality) + " (" + fms(sk->ms) + ")." });
    const BenchRow *bf = find("sssp", "bellman-ford", "adversarial"), *dj = find("sssp", "dijkstra", "adversarial");
    if (bf && dj) v.push_back({ "Roads listed in the worst possible order", "Bellman\xE2\x80\x93" "Ford is forced through all " + fint(bf->n) + " passes (" + fms(bf->ms) +
                                                                                 "); Dijkstra answers in " + fms(dj->ms) + "." });
    const BenchRow *neg = nullptr;
    for (auto &r : rows) if (r.alg == "dijkstra(on-negative)" && r.note.find("WRONG") != std::string::npos) { neg = &r; break; }
    if (neg) {
        const BenchRow *nb = find("sssp", "bellman-ford", neg->dataset);
        if (nb) v.push_back({ "Regenerative braking downhill", "Negative energy arcs make Dijkstra report " + f2(neg->quality) + " kWh where the true minimum is " + f2(nb->quality) +
                                                                    " kWh. Bellman\xE2\x80\x93" "Ford gets it right." });
    }
    auto gen = [](const std::string &note, const char *key) {
        size_t p = note.find(std::string(key) + "=");
        return p == std::string::npos ? std::string("?") : fint(std::atof(note.c_str() + p + std::strlen(key) + 1));
    };
    const BenchRow *kf = find("knapsack", "bnb-FIFO", "adversarial"), *kl = find("knapsack", "bnb-LC", "adversarial");
    if (kf && kl) v.push_back({ "Site costs that track their value", "On strongly correlated knapsacks FIFO branch and bound generates " + gen(kf->note, "generated") +
                                                                          " search nodes and holds " + gen(kf->note, "frontier") + " at once. Least cost generates " + gen(kl->note, "generated") + "." });
    const BenchRow *ff = find("flow", "ford-fulkerson-dfs", "adversarial"), *ek = find("flow", "edmonds-karp-bfs", "adversarial");
    if (ff && ek) v.push_back({ "Large port capacities", "Depth-first Ford\xE2\x80\x93" "Fulkerson takes " + fms(ff->ms) + "; Edmonds\xE2\x80\x93Karp, whose bound ignores capacities, takes " +
                                                              fms(ek->ms) + "." });
    return v;
}

} // namespace ev33
