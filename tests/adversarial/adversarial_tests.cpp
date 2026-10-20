// Adversarial and robustness tests AD01-AD25.
//
// Every test states the expected behaviour class in its name and comment:
//   REJECT       the input is refused with a precise message (exception / DataError)
//   EMPTY        a valid, empty result
//   UNREACHABLE  a valid "no route" result with an explanation
//   VALID        a correct, normal result
//   CONTROLLED   a controlled error instead of an overflow or a misleading value
// The suite also runs under AddressSanitizer + UndefinedBehaviorSanitizer (EV33_SANITIZE=ON),
// which turns any invalid memory access or undefined arithmetic into a test failure.
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

#include "ev/algorithms/divide_conquer.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/greedy.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/algorithms/string_matching.hpp"
#include "ev/analysis/pipeline.hpp"
#include "ev/core/errors.hpp"
#include "ev/core/rng.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/planner/exact_planner.hpp"
#include "../test_support.hpp"

using namespace ev;

namespace {

// Parses a variant of the tiny scenario where one file is replaced; returns the error text or "".
std::string load_error(void (*edit)(ScenarioTexts &)) {
    ScenarioTexts t = evtest::tiny_texts();
    edit(t);
    try {
        parse_scenario(t);
        return "";
    } catch (const DataError &e) {
        return e.what();
    }
}

#define EXPECT_REJECTED(edit, needle)                                                     \
    do {                                                                                  \
        std::string err = load_error(edit);                                               \
        EXPECT_NE(err, "") << "input was accepted";                                       \
        EXPECT_NE(err.find(needle), std::string::npos) << "message was: " << err;         \
    } while (0)

} // namespace

TEST(Adversarial, AD01_EmptyInputFiles_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes = ""; }, "nodes.csv: file is empty");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "\n\n"; }, "roads.csv: file is empty");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes.reset(); }, "nodes.csv: file not found");
    // a header with no rows is a valid, empty table
    EXPECT_EQ(load_error([](ScenarioTexts &t) { t.trips = "id,vehicle,from,to\n"; }), "");
}

TEST(Adversarial, AD02_MissingFields_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,2\n"; }, "roads.csv:2: expected 4 fields, found 3");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,2,\n"; }, "missing value for 'distance_km'");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.vehicles = "id,battery_kwh,charge_kwh,reserve_kwh\nV,10,10,1\n"; },
                    "missing required column 'consumption_kwh_per_km'");
}

TEST(Adversarial, AD03_DuplicateNodeIds_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes = "id\n1\n2\n2\n"; }, "nodes.csv:4: duplicate node id 2");
}

TEST(Adversarial, AD04_DuplicateStationIds_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.stations = "id,node,power_kw,ports\n7,1,50,2\n7,2,50,2\n"; }, "duplicate station id 7");
    // a candidate may not reuse an existing station's id either
    EXPECT_REJECTED([](ScenarioTexts &t) {
        t.stations = "id,node,power_kw,ports\n7,1,50,2\n";
        t.candidates = "id,node,install_cost_inr,power_kw,ports\n7,2,500000,50,2\n";
    }, "already used");
}

TEST(Adversarial, AD05_EdgeToNonexistentNode_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,99,5\n"; }, "unknown node 99");
}

TEST(Adversarial, AD06_NegativeDistance_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,2,-5\n"; }, "positive, finite distance");
    Graph g(2);
    g.add_edge(0, 1, -5);
    EXPECT_THROW(dijkstra(g, 0), std::invalid_argument);   // algorithm level as well
}

TEST(Adversarial, AD07_NegativeInstallationCost_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.candidates = "id,node,install_cost_inr,power_kw,ports\n5,2,-500000,50,2\n"; }, "must not be negative");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.candidates = "id,node,install_cost_inr,power_kw,ports\n5,2,0,50,2\n"; }, "cost must be positive");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.config = "name = x\nbudget_inr = -10\n"; }, "budget_inr");
}

TEST(Adversarial, AD08_InvalidLatitudeLongitude_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes = "id,lat,lon\n1,91,0\n2,0,0\n3,0,1\n"; }, "invalid coordinates");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes = "id,lat,lon\n1,0,181\n2,0,0\n3,0,1\n"; }, "invalid coordinates");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.nodes = "id,lat,lon\n1,10,\n2,0,0\n3,0,1\n"; }, "both lat and lon");
}

TEST(Adversarial, AD09_ChargeAboveCapacity_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.vehicles = "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,10,12,0.2,1\n"; },
                    "exceeds the battery capacity");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.trips = "id,vehicle,from,to,start_charge_kwh\n1,V,1,3,10.5\n"; }, "exceeds the battery capacity");
}

TEST(Adversarial, AD10_ZeroBatteryCapacity_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.vehicles = "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,0,0,0.2,0\n"; },
                    "battery capacity must be positive");
}

TEST(Adversarial, AD11_ZeroEnergyConsumption_REJECT) {
    // zero consumption would make every trip trivially feasible - a misleading result, so it is refused
    EXPECT_REJECTED([](ScenarioTexts &t) { t.vehicles = "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,10,10,0,1\n"; },
                    "energy consumption must be positive");
}

TEST(Adversarial, AD12_ExtremelyLargeWeights_CONTROLLED) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,2,1e300\n2,2,3,1\n"; }, "unrealistically long");
    Graph g(3);
    g.add_edge(0, 1, 1e308);
    g.add_edge(1, 2, 1e308);
    EXPECT_THROW(dijkstra(g, 0), std::overflow_error);   // not reported as "unreachable"
    Graph h(3);
    h.add_edge(0, 1, 4e15);
    h.add_edge(1, 2, 4e15);
    EXPECT_EQ(dijkstra(h, 0).dist[2], 8e15);   // large but representable: exact
}

TEST(Adversarial, AD13_ArithmeticOverflowRisks_CONTROLLED) {
    // money: amounts are bounded when parsed, so sums of costs fit in 64 bits
    EXPECT_THROW(parse_rupees("99999999999999999999"), std::invalid_argument);
    // placement: many huge locked costs saturate instead of wrapping around
    Scenario s = evtest::sample();
    PlacementConstraints c;
    std::vector<std::int64_t> b(s.candidates.size(), 1);
    for (std::size_t i = 0; i < s.candidates.size(); i++) {
        s.candidates[i].install_cost = parse_rupees("999999999999999");   // ~10^15 rupees each
        c.locked.push_back(static_cast<int>(i));
    }
    PlacementResult r = place_stations(s, b, PlacementMethod::DynamicProgramming, c);
    EXPECT_TRUE(r.over_budget);
    EXPECT_GT(r.cost, 0);   // no wrap-around to a negative total
    // modular arithmetic near 2^63 and matrix-chain products near 2^64 stay exact or saturate
    const std::uint64_t p = (1ULL << 61) - 1;
    EXPECT_EQ(mulmod(p - 1, p - 1, p), 1u);
    EXPECT_EQ(matrix_chain_order({4000000000ULL, 4000000000ULL, 4000000000ULL}).cost, std::numeric_limits<std::uint64_t>::max());
    EXPECT_THROW(place_stations(evtest::sample(), std::vector<std::int64_t>(6, 2000000000000LL), PlacementMethod::DynamicProgramming),
                 std::invalid_argument);   // absurd benefit scores are refused
}

TEST(Adversarial, AD14_DisconnectedComponents_UNREACHABLE) {
    Scenario s = evtest::sample();   // node 9 is on an island
    EnergyModel m(s);
    ExactPlanner p(s, m, make_charger_sites(s, {}));
    TripPlan plan = p.plan(s.network.index_of(1), s.network.index_of(10), s.vehicles[2], 50, 0);
    EXPECT_EQ(plan.status, PlanStatus::NoRoadConnection);
    EXPECT_EQ(prim(s.network.distance_graph()).components, 2);   // minimum spanning FOREST
}

TEST(Adversarial, AD15_MultipleEqualCostPaths_VALID) {
    Scenario s = evtest::sample();   // 4-5 and 4-8-5 are both 20 km
    EnergyModel m(s);
    ExactPlanner p(s, m, {});
    TripPlan a = p.plan(s.network.index_of(4), s.network.index_of(5), s.vehicles[0], 12, 0);
    TripPlan b = p.plan(s.network.index_of(4), s.network.index_of(5), s.vehicles[0], 12, 0);
    EXPECT_NEAR(a.distance_km, 20, 1e-9);
    EXPECT_EQ(a.route, b.route);   // deterministic choice
}

TEST(Adversarial, AD16_ExtremelyLongInputs_VALID) {
    Rng r(7);
    std::vector<std::int64_t> v(1000000);
    for (auto &x : v) x = r.range(-1000000000, 1000000000);
    std::vector<std::int64_t> q = v;
    merge_sort(v, [](std::int64_t a, std::int64_t b) { return a < b; });
    quick_sort(q, [](std::int64_t a, std::int64_t b) { return a < b; }, PivotRule::Random);
    EXPECT_EQ(v, q);
    std::string log(2000000, 'F');
    log[1999999] = 'R';
    EXPECT_EQ(match_kmp(log, "FFFR").size(), 1u);
    HuffmanCode h(log);
    EXPECT_EQ(h.decode(h.encode(log)), log);
}

TEST(Adversarial, AD17_RepeatedEdges_VALID) {
    // two different roads between the same nodes are allowed; the shorter one is used
    ScenarioTexts t = evtest::tiny_texts();
    t.roads = "id,from,to,distance_km\n1,1,2,10\n2,2,3,10\n3,1,2,4\n";
    Scenario s = parse_scenario(t);
    EnergyModel m(s);
    ExactPlanner p(s, m, {});
    EXPECT_NEAR(p.plan(s.trips[0]).distance_km, 14, 1e-9);
    // the same road id twice is a data error
    t.roads = "id,from,to,distance_km\n1,1,2,10\n1,2,3,10\n";
    EXPECT_THROW(parse_scenario(t), DataError);
}

TEST(Adversarial, AD18_SelfLoops_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.roads = "id,from,to,distance_km\n1,1,1,5\n2,1,2,5\n"; }, "self-loop");
    Graph g(2);   // the generic graph accepts them; algorithms ignore them
    g.add_edge(0, 0, 1);
    g.add_edge(0, 1, 3);
    EXPECT_EQ(dijkstra(g, 0).dist[1], 3);
}

TEST(Adversarial, AD19_NoStationWithinBudget_EMPTY) {
    Scenario s = evtest::sample();
    std::vector<std::int64_t> b;
    for (const CandidateStation &c : s.candidates) b.push_back(*c.benefit);
    PlacementResult r = place_stations(s, b, PlacementMethod::BranchBoundLC, {}, parse_rupees("100"));
    EXPECT_TRUE(r.chosen.empty());
    EXPECT_EQ(r.cost, 0);
    EXPECT_EQ(r.benefit, 0);
}

TEST(Adversarial, AD20_StationPresentButUnreachable_UNREACHABLE) {
    Scenario s = evtest::sample();
    EnergyModel m(s);
    ExactPlanner p(s, m, make_charger_sites(s, {}));
    // SV1 with 9.5 kWh at node 1: station 101 is 40 km (8 kWh) away, only 7.5 kWh usable
    TripPlan plan = p.plan(s.network.index_of(1), s.network.index_of(5), s.vehicles[0], 9.5, 0);
    EXPECT_EQ(plan.status, PlanStatus::OutOfRange);
    EXPECT_TRUE(p.reachable_sites(s.network.index_of(1), s.vehicles[0], 9.5).empty());
}

TEST(Adversarial, AD21_InvalidSourceOrDestination_REJECT) {
    EXPECT_REJECTED([](ScenarioTexts &t) { t.trips = "id,vehicle,from,to\n1,V,1,42\n"; }, "ends at unknown node 42");
    EXPECT_REJECTED([](ScenarioTexts &t) { t.trips = "id,vehicle,from,to\n1,X,1,3\n"; }, "unknown vehicle 'X'");
    Scenario s = parse_scenario(evtest::tiny_texts());
    EnergyModel m(s);
    ExactPlanner p(s, m, {});
    EXPECT_THROW(p.plan(0, 7, s.vehicles[0], 5, 0), std::out_of_range);
    EXPECT_THROW(dijkstra(Graph(3), 3), std::out_of_range);
}

TEST(Adversarial, AD22_EmptyStationAndCandidateLists_VALID) {
    Scenario s = parse_scenario(evtest::tiny_texts());   // no stations.csv, no candidates.csv
    ScenarioContext ctx(s);
    PlanningRun run(ctx, RunOptions{});
    EXPECT_TRUE(run.placement.chosen.empty());
    EXPECT_TRUE(run.sites.empty());
    EXPECT_EQ(run.plans.size(), 1u);
    EXPECT_EQ(run.plans[0].status, PlanStatus::Direct);   // 20 km x 0.2 = 4 kWh of 9 usable
}

TEST(Adversarial, AD23_InvalidMatrixDimensions_REJECT) {
    EXPECT_THROW(multiply_classical(Matrix(0, 0), Matrix(0, 0)), std::invalid_argument);
    EXPECT_THROW(multiply_strassen(Matrix(0, 4), Matrix(4, 2)), std::invalid_argument);
    EXPECT_THROW(matrix_chain_order({3, 0, 4}), std::invalid_argument);
}

TEST(Adversarial, AD24_MatrixDimensionMismatch_REJECT) {
    EXPECT_THROW(multiply_classical(Matrix(2, 3), Matrix(2, 3)), std::invalid_argument);
    EXPECT_THROW(multiply_strassen(Matrix(5, 1), Matrix(2, 5)), std::invalid_argument);
}

TEST(Adversarial, AD25_InvalidHuffmanInput_REJECT) {
    EXPECT_THROW(HuffmanCode{std::string()}, std::invalid_argument);
    HuffmanCode h(std::string("abcabcaaa"));
    EXPECT_THROW(h.encode("abz"), std::invalid_argument);
    std::vector<bool> bits = h.encode("b");
    ASSERT_EQ(h.code('b').size(), 2u);
    bits.push_back(h.code('b')[0] == '1');   // half of another 2-bit code: the stream ends mid-code
    EXPECT_THROW(h.decode(bits), std::invalid_argument);
}
