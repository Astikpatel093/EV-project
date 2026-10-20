// Integration tests IT01-IT08: complete workflows on the documented sample dataset
// (data/sample/README.md) and the generated scenarios.
//
// Each test records its actual outcome with RecordProperty("actual", ...), so the JUnit XML report
// (ctest / --gtest_output=xml) contains the observed result next to the pass/fail status.
// docs/testing.md lists input, procedure and expected outcome for every test.
#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>

#include "ev/algorithms/searching.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/analysis/pipeline.hpp"
#include "ev/analysis/report.hpp"
#include "ev/core/errors.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/operations/capacity.hpp"
#include "ev/operations/sessions.hpp"
#include "ev/planner/exact_planner.hpp"
#include "../test_support.hpp"

using namespace ev;

namespace {

const TripRequest &trip(const Scenario &s, int id) {
    for (const TripRequest &t : s.trips)
        if (t.id == id) return t;
    throw std::logic_error("trip not found");
}

std::string route_ids(const Scenario &s, const TripPlan &p) {
    std::string r;
    for (int pos : p.route) r += (r.empty() ? "" : "-") + std::to_string(s.network.node(pos).id);
    return r;
}

void record(const std::string &actual) {
    ::testing::Test::RecordProperty("actual", actual);
    std::printf("    ACTUAL: %s\n", actual.c_str());
}

} // namespace

// IT01 Basic trip planning: load graph and vehicle, request a trip, verify route and distance.
TEST(Integration, IT01_BasicTripPlanning) {
    Scenario s = evtest::sample();
    EnergyModel model(s);
    ExactPlanner planner(s, model, make_charger_sites(s, {}));
    TripPlan p = planner.plan(trip(s, 6));   // SV1 from node 4 to node 5
    record(std::string(to_string(p.status)) + ", route " + route_ids(s, p) + ", " + std::to_string(p.distance_km) + " km");
    EXPECT_EQ(p.status, PlanStatus::Direct);
    EXPECT_NEAR(p.distance_km, 20.0, 1e-9);
    EXPECT_EQ(s.network.node(p.route.front()).id, 4);
    EXPECT_EQ(s.network.node(p.route.back()).id, 5);
    // and the shortest route over the hill is rejected for trip 1 in favour of the flat road
    TripPlan q = planner.plan(trip(s, 1));
    EXPECT_EQ(route_ids(s, q), "1-2-3");
    EXPECT_NEAR(q.distance_km, 40.0, 1e-9);
}

// IT02 Trip requiring charging: the plan contains a valid charging stop.
TEST(Integration, IT02_TripRequiringCharging) {
    Scenario s = evtest::sample();
    EnergyModel model(s);
    ExactPlanner planner(s, model, make_charger_sites(s, {}));
    TripPlan p = planner.plan(trip(s, 2));   // SV1 1 -> 5, 80 km, 10 kWh usable
    ASSERT_EQ(p.status, PlanStatus::WithCharging);
    ASSERT_EQ(p.stops.size(), 1u);
    const ChargingStop &st = p.stops[0];
    record("stop at station " + std::to_string(st.station_id) + " arriving with " + std::to_string(st.arrive_kwh) + " kWh, leaving with " +
           std::to_string(st.depart_kwh) + " kWh; " + std::to_string(p.distance_km) + " km, arrive " + std::to_string(p.arrive_kwh) + " kWh");
    EXPECT_EQ(st.station_id, 101);
    EXPECT_EQ(s.network.node(p.route[st.route_pos]).id, 3);   // the stop is on the route, at the station's node
    EXPECT_GE(st.arrive_kwh, s.vehicles[0].reserve_kwh);       // arrives above the reserve
    EXPECT_NEAR(st.depart_kwh, 16.0, 1e-9);
    EXPECT_NEAR(p.arrive_kwh, 8.0, 1e-9);
}

// IT03 No feasible route: clear failure results.
TEST(Integration, IT03_NoFeasibleRoute) {
    Scenario s = evtest::sample();
    EnergyModel model(s);
    ExactPlanner planner(s, model, make_charger_sites(s, {}));
    TripPlan island = planner.plan(trip(s, 3));   // island: no road at all
    TripPlan small = planner.plan(trip(s, 4));    // micro car: chargers cannot close the gap
    TripPlan low = planner.plan(trip(s, 7));      // too little charge, only a CHAdeMO charger in reach
    record(std::string(to_string(island.status)) + " | " + to_string(small.status) + " | " + to_string(low.status));
    EXPECT_EQ(island.status, PlanStatus::NoRoadConnection);
    EXPECT_EQ(small.status, PlanStatus::ChargingInsufficient);
    EXPECT_EQ(low.status, PlanStatus::OutOfRange);
    for (const TripPlan *p : {&island, &small, &low}) {
        EXPECT_FALSE(p->feasible());
        EXPECT_FALSE(p->explanation.empty());   // every failure is explained
        EXPECT_TRUE(p->stops.empty());
    }
}

// IT04 Station placement: chosen stations maximise the objective for the test dataset.
TEST(Integration, IT04_StationPlacement) {
    Scenario s = evtest::sample();
    ScenarioContext ctx(s);
    PlacementResult r = place_stations(s, ctx.benefits.benefit, PlacementMethod::DynamicProgramming);
    std::string chosen;
    for (int ci : r.chosen) chosen += std::to_string(s.candidates[static_cast<std::size_t>(ci)].id) + " ";
    record("chosen " + chosen + "benefit " + std::to_string(r.benefit) + ", cost " + format_inr(r.cost));
    EXPECT_EQ(chosen, "501 502 506 ");
    EXPECT_EQ(r.benefit, 155);   // hand-computed optimum (data/sample/README.md)
    EXPECT_LE(r.cost, s.config.budget);
    // exhaustive check of every subset of the 6 candidates
    std::int64_t best = 0;
    for (int mask = 0; mask < 64; mask++) {
        Paise cost = 0;
        std::int64_t ben = 0;
        for (int i = 0; i < 6; i++)
            if (mask >> i & 1) { cost += s.candidates[static_cast<std::size_t>(i)].install_cost; ben += ctx.benefits.benefit[static_cast<std::size_t>(i)]; }
        if (cost <= s.config.budget && ben > best) best = ben;
    }
    EXPECT_EQ(r.benefit, best);
}

// IT05 Dataset validation: malformed files are reported and valid state is not corrupted.
TEST(Integration, IT05_DatasetValidation) {
    Scenario current = evtest::sample();   // the application's valid state
    const int nodes_before = current.network.node_count();
    std::vector<std::pair<std::string, ScenarioTexts>> bad;
    auto variant = [](const std::string &file, const std::string &find, const std::string &repl) {
        ScenarioTexts t = evtest::sample_texts();
        std::optional<std::string> *slot = file == "nodes" ? &t.nodes : file == "roads" ? &t.roads : file == "vehicles" ? &t.vehicles
                                         : file == "candidates" ? &t.candidates : &t.trips;
        std::string &text = **slot;
        const std::size_t at = text.find(find);
        if (at == std::string::npos) throw std::logic_error("fixture text not found: " + find);
        text.replace(at, find.size(), repl);
        return t;
    };
    bad.push_back({"missing column", variant("roads", "id,from,to,distance_km,travel_time_min,energy_kwh", "id,from,to,travel_time_min,energy_kwh")});
    bad.push_back({"malformed number", variant("roads", "1,1,2,20,,", "1,1,2,twenty,,")});
    bad.push_back({"duplicate node id", variant("nodes", "2,0.000000", "1,0.000000")});
    bad.push_back({"invalid coordinates", variant("nodes", "6,0.090000", "6,95.000000")});
    bad.push_back({"negative distance", variant("roads", "11,9,10,5,,", "11,9,10,-5,,")});
    bad.push_back({"unknown node reference", variant("roads", "11,9,10,5,,", "11,9,77,5,,")});
    bad.push_back({"charge above capacity", variant("vehicles", "SV1,Sample hatchback,20,12", "SV1,Sample hatchback,20,25")});
    std::string actual;
    int dir = 0;
    for (auto &b : bad) {
        // write the malformed dataset to disk and load it through the normal file loader
        const std::string path = "it05_case_" + std::to_string(dir++);
        make_directories(path);
        auto put = [&](const char *name, const std::optional<std::string> &text) { if (text) write_text_file(path + "/" + name, *text); };
        put("scenario.cfg", b.second.config); put("nodes.csv", b.second.nodes); put("roads.csv", b.second.roads);
        put("stations.csv", b.second.stations); put("candidates.csv", b.second.candidates); put("vehicles.csv", b.second.vehicles);
        put("trips.csv", b.second.trips); put("sessions.csv", b.second.sessions);
        try {
            current = load_scenario(path);   // assignment happens only if loading succeeds
            ADD_FAILURE() << b.first << ": malformed file was accepted";
        } catch (const DataError &e) {
            EXPECT_GT(e.line(), 0) << b.first << ": " << e.what();   // the message names file:line
            actual += b.first + " -> " + e.what() + "; ";
        }
    }
    record(actual);
    EXPECT_EQ(current.network.node_count(), nodes_before);   // valid state untouched
    EXPECT_EQ(current.trips.size(), 8u);
}

// IT06 Prim versus Kruskal on the same connected graph: equal total weight.
TEST(Integration, IT06_PrimVersusKruskal) {
    Scenario s = load_scenario(evtest::source_path("data/scenarios/medium"));
    Graph g = s.network.distance_graph();   // one graph object, used by both algorithms
    ASSERT_EQ(s.network.components(), 1);
    SpanningForest p = prim(g), k = kruskal(g);
    record("Prim " + std::to_string(p.total_weight) + " km, Kruskal " + std::to_string(k.total_weight) + " km, " +
           std::to_string(p.edges.size()) + " edges");
    EXPECT_NEAR(p.total_weight, k.total_weight, 1e-6);
    EXPECT_TRUE(p.is_spanning_tree());
    EXPECT_EQ(static_cast<int>(p.edges.size()), g.vertex_count() - 1);
    // the sample network has an island: both report a spanning forest of 2 trees
    Scenario sample = evtest::sample();
    Graph h = sample.network.distance_graph();
    EXPECT_EQ(prim(h).components, 2);
    EXPECT_EQ(kruskal(h).components, 2);
    EXPECT_NEAR(prim(h).total_weight, kruskal(h).total_weight, 1e-9);
}

// IT07 Sorting and searching integration: sort station records, search ids and attributes.
TEST(Integration, IT07_SortingAndSearching) {
    Scenario s = load_scenario(evtest::source_path("data/scenarios/large"));
    std::vector<CandidateStation> by_id = s.candidates, by_cost = s.candidates;
    quick_sort(by_id, [](const CandidateStation &a, const CandidateStation &b) { return a.id < b.id; });
    merge_sort(by_cost, [](const CandidateStation &a, const CandidateStation &b) { return a.install_cost < b.install_cost; });
    auto id = [](const CandidateStation &c) { return c.id; };
    auto cost = [](const CandidateStation &c) { return c.install_cost; };
    const long found = binary_search_iterative(by_id, 650, id);
    const long missing = binary_search_recursive(by_id, 4999, id);
    const Paise limit = 60 * kPaisePerLakh;
    const std::size_t cheap = upper_bound_index(by_cost, limit, cost);
    record("candidate 650 at index " + std::to_string(found) + ", 4999 -> " + std::to_string(missing) + ", " + std::to_string(cheap) +
           " candidates cost <= 60 lakh");
    ASSERT_GE(found, 0);
    EXPECT_EQ(by_id[static_cast<std::size_t>(found)].id, 650);
    EXPECT_EQ(missing, -1);
    std::size_t count = 0;
    for (const CandidateStation &c : s.candidates) count += c.install_cost <= limit;
    EXPECT_EQ(cheap, count);
    for (std::size_t i = 1; i < by_cost.size(); i++) {
        EXPECT_LE(by_cost[i - 1].install_cost, by_cost[i].install_cost);
        if (by_cost[i - 1].install_cost == by_cost[i].install_cost) EXPECT_LT(by_cost[i - 1].id, by_cost[i].id);   // stable
    }
    // every trip's origin is found in the sorted node-id index, iteratively and recursively
    std::vector<int> ids;
    for (const RoadNode &n : s.network.nodes()) ids.push_back(n.id);
    merge_sort(ids);
    for (const TripRequest &t : s.trips) {
        EXPECT_GE(binary_search_iterative(ids, t.from), 0);
        EXPECT_EQ(binary_search_iterative(ids, t.to), binary_search_recursive(ids, t.to));
    }
}

// IT08 End-to-end: network, vehicles, existing stations, candidates and budget in; a route
// recommendation and a placement recommendation out.
TEST(Integration, IT08_EndToEndWorkflow) {
    Scenario s = evtest::sample();
    ScenarioContext ctx(s);
    RunOptions o;   // DP placement + Dijkstra legs
    PlanningRun run(ctx, o);
    CapacityResult cap = analyze_capacity(run);
    SessionAnalysis ses = analyze_sessions(run);
    std::ostringstream text;
    print_run(text, run, &cap, &ses);
    ExactPlanner exact(s, ctx.model, run.sites);
    TripPlan rec = exact.plan(trip(s, 2));
    record("placement benefit " + std::to_string(run.placement.benefit) + ", " + std::to_string(run.kpi.feasible) + "/" +
           std::to_string(run.kpi.trips) + " trips feasible; trip 2: " + to_string(rec.status) + ", " + std::to_string(rec.distance_km) + " km");
    EXPECT_EQ(run.placement.benefit, 155);
    EXPECT_LE(run.placement.cost, s.config.budget);
    EXPECT_EQ(run.sites.size(), 3u + 3u);   // 3 existing + 3 new
    EXPECT_EQ(rec.status, PlanStatus::WithCharging);
    EXPECT_NEAR(rec.distance_km, 80.0, 1e-9);
    EXPECT_TRUE(cap.consistent());
    // trips 3 (island) and 4 (micro car) stay infeasible; trip 8 is zero-length
    EXPECT_FALSE(run.plans[2].feasible());
    EXPECT_TRUE(run.plans[7].feasible());
    EXPECT_NE(text.str().find("Placement [dp]"), std::string::npos);
    // results can be exported
    make_directories("it08_out");
    export_plans_csv("it08_out/plans.csv", run);
    export_placement_csv("it08_out/placement.csv", run);
    export_summary_json("it08_out/summary.json", run, &cap, &ses);
    EXPECT_TRUE(read_text_file("it08_out/summary.json").has_value());
    EXPECT_NE(read_text_file("it08_out/placement.csv")->find("501"), std::string::npos);
}

// Regression: the generated scenarios keep producing the same headline results.
TEST(Integration, RegressionHeadlineResults) {
    std::string actual;
    for (const char *name : {"small", "medium", "shock", "large", "adversarial"}) {
        Scenario s = load_scenario(evtest::source_path(std::string("data/scenarios/") + name));
        ScenarioContext ctx(s);
        PlanningRun run(ctx, RunOptions{});
        actual += std::string(name) + ": " + std::to_string(run.kpi.feasible) + "/" + std::to_string(run.kpi.trips) + " feasible, benefit " +
                  std::to_string(run.placement.benefit) + "; ";
        const std::string golden_path = evtest::source_path(std::string("tests/fixtures/regression/") + name + ".txt");
        const std::string line = std::to_string(run.kpi.feasible) + " " + std::to_string(run.placement.benefit) + " " +
                                 std::to_string(run.placement.chosen.size());
        auto golden = read_text_file(golden_path);
        ASSERT_TRUE(golden.has_value()) << "missing " << golden_path;
        EXPECT_EQ(line, golden->substr(0, golden->find('\n'))) << name;
    }
    record(actual);
}
