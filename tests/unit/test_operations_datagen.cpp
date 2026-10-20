// Unit tests: capacity, sessions, day simulation, and the data generators.
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <unordered_set>

#include "ev/analysis/pipeline.hpp"
#include "ev/datagen/benchmark_generator.hpp"
#include "ev/datagen/scenario_generator.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/operations/capacity.hpp"
#include "ev/operations/sessions.hpp"
#include "ev/operations/simulation.hpp"
#include "../test_support.hpp"

using namespace ev;

namespace {

struct SmallCity : ::testing::Test {
    Scenario s = generate_scenario("small", 33);
    ScenarioContext ctx{s};
};

} // namespace

TEST(Sessions, BookedSessionsOnTheSample) {
    Scenario s = evtest::sample();
    StationSchedule r = schedule_station(s, 101);
    EXPECT_EQ(r.sessions.size(), 8u);
    std::vector<int> one;
    for (std::size_t k : r.one_bay) one.push_back(r.sessions[k].id);
    EXPECT_EQ(one, (std::vector<int>{1, 4, 7, 8}));
    EXPECT_EQ(r.bays.resources, 3);
    EXPECT_EQ(r.depth, 3);
    EXPECT_EQ(r.served_with_ports.size(), 6u);
    EXPECT_THROW(schedule_station(s, 9999), std::invalid_argument);
}

TEST_F(SmallCity, CapacityMethodsAgree) {
    PlanningRun run(ctx, RunOptions{});
    CapacityResult c = analyze_capacity(run);
    EXPECT_TRUE(c.consistent());
    EXPECT_LE(c.dfs.value, c.trips_needing);
    EXPECT_LE(c.dfs.value, c.slots_total);
}

TEST_F(SmallCity, SessionAnalysisInvariants) {
    PlanningRun run(ctx, RunOptions{});
    SessionAnalysis a = analyze_sessions(run);
    long total = 0;
    for (const SiteLoad &l : a.sites) {
        EXPECT_EQ(l.chargers_needed, l.depth);   // interval partitioning is optimal
        EXPECT_LE(l.served, l.sessions);
        total += l.sessions;
    }
    EXPECT_EQ(total, a.sessions);
    long stops = 0;
    for (const TripPlan &p : run.plans) stops += static_cast<long>(p.stops.size());
    EXPECT_EQ(a.sessions, stops);
}

TEST_F(SmallCity, PipelineRespectsBudgetAndReserve) {
    for (int m = 0; m < kPlacementMethods; m++) {
        RunOptions o;
        o.placement = static_cast<PlacementMethod>(m);
        PlanningRun run(ctx, o);
        EXPECT_LE(run.placement.cost, s.config.budget);
        for (const TripPlan &p : run.plans)
            if (p.feasible()) EXPECT_GE(p.min_kwh, s.vehicles[static_cast<std::size_t>(s.vehicle_index(p.vehicle_id))].reserve_kwh - 1e-9);
    }
}

namespace {

void check_simulation(PlanningRun &run, const SimResult &sim) {
    // every vehicle's segments are contiguous in time
    for (std::size_t v = 0; v + 1 < sim.seg_off.size(); v++)
        for (int i = sim.seg_off[v] + 1; i < sim.seg_off[v + 1]; i++)
            EXPECT_NEAR(sim.seg[static_cast<std::size_t>(i)].t0, sim.seg[static_cast<std::size_t>(i) - 1].t1, 1e-6);
    // occupancy never exceeds the ports: sweep charge segments per site
    std::map<int, std::vector<std::pair<double, int>>> ev;
    for (const SimSegment &g : sim.seg)
        if (g.kind == SegmentKind::Charge) {
            ev[g.site].push_back({g.t0, +1});
            ev[g.site].push_back({g.t1, -1});
        }
    for (auto &kv : ev) {
        std::sort(kv.second.begin(), kv.second.end());   // test-side oracle sort
        int cur = 0;
        for (auto &e : kv.second) {
            cur += e.second;
            EXPECT_LE(cur, run.sites[static_cast<std::size_t>(kv.first)].ports);
        }
    }
    double waits = 0;
    for (const SimSite &st : sim.sites) waits += st.wait_min;
    EXPECT_NEAR(waits, sim.total_wait, 1e-6);
}

} // namespace

TEST_F(SmallCity, SimulationInvariantsBothPolicies) {
    PlanningRun run(ctx, RunOptions{});
    for (DriverPolicy pol : {DriverPolicy::Wait, DriverPolicy::Divert}) {
        SimOptions o;
        o.policy = pol;
        SimResult sim = simulate_day(run, o);
        check_simulation(run, sim);
        int feasible = 0;
        for (const TripPlan &p : run.plans) feasible += p.feasible();
        EXPECT_EQ(static_cast<int>(sim.trip.size()), feasible);
        if (pol == DriverPolicy::Wait) EXPECT_EQ(sim.diverted, 0);
    }
}

TEST(Simulation, UnlimitedPortsRemoveAllWaiting) {
    Scenario s = generate_scenario("medium", 33);
    ScenarioContext ctx(s);
    RunOptions o;
    for (const ChargingStation &st : s.stations) o.ports[st.id] = 500;
    for (const CandidateStation &c : s.candidates) o.ports[c.id] = 500;
    PlanningRun run(ctx, o);
    SimResult sim = simulate_day(run);
    EXPECT_EQ(sim.total_wait, 0);
    EXPECT_EQ(sim.delayed, 0);
}

// ------------------------------------------------------------------ generators
TEST(ScenarioGenerator, SameSeedSameFilesDifferentSeedDifferentFiles) {
    auto text = [](std::uint64_t seed) {
        Scenario s = generate_scenario("small", seed);
        const std::string dir = "gen_test_" + std::to_string(seed);
        write_scenario(s, dir, synthetic_banner("small", seed));
        ScenarioTexts t = read_scenario_texts(dir);
        return *t.nodes + *t.roads + *t.trips + *t.candidates + *t.stations + *t.vehicles;
    };
    EXPECT_EQ(text(33), text(33));
    EXPECT_NE(text(33), text(34));
}

TEST(ScenarioGenerator, ProfilesAndLabels) {
    Scenario s = generate_scenario("medium", 33);
    EXPECT_EQ(s.network.node_count(), 256);
    EXPECT_EQ(s.candidates.size(), 60u);
    EXPECT_EQ(s.trips.size(), 1200u);
    EXPECT_EQ(s.config.source, DataSource::Synthetic);
    EXPECT_NE(synthetic_banner("medium", 33)[0].find("SYNTHETIC"), std::string::npos);
    EXPECT_THROW(generate_scenario("nope", 1), std::invalid_argument);
    // round trip through the files keeps everything
    write_scenario(s, "gen_medium", synthetic_banner("medium", 33));
    Scenario back = load_scenario("gen_medium");
    EXPECT_EQ(back.network.edge_count(), s.network.edge_count());
    EXPECT_EQ(back.trips.size(), s.trips.size());
    EXPECT_EQ(back.config.budget, s.config.budget);
}

TEST(BenchmarkGenerator, SizesConnectivityAndNoDuplicates) {
    for (const GraphSize &g : benchmark_graph_sizes()) {
        if (g.vertices > 10000) continue;   // B6 is exercised by the benchmark harness
        for (GraphVariant v : {GraphVariant::ConnectedVaried, GraphVariant::ConnectedEqual, GraphVariant::Disconnected}) {
            GraphDataset d = make_graph_dataset(g.vertices, g.edges, v, kBenchmarkSeed);
            EXPECT_EQ(static_cast<int>(d.edges.size()), g.edges) << g.name;
            std::unordered_set<long long> seen;
            DisjointSet ds(g.vertices);
            for (const Edge &e : d.edges) {
                EXPECT_NE(e.u, e.v);
                const long long key = static_cast<long long>(std::min(e.u, e.v)) * g.vertices + std::max(e.u, e.v);
                EXPECT_TRUE(seen.insert(key).second) << "duplicate edge";
                if (v == GraphVariant::ConnectedEqual) EXPECT_EQ(e.weight, 10);
                ds.unite(e.u, e.v);
            }
            EXPECT_EQ(ds.sets(), v == GraphVariant::Disconnected ? 4 : 1) << g.name << " " << to_string(v);
        }
    }
}

TEST(BenchmarkGenerator, Reproducible) {
    GraphDataset a = make_graph_dataset(1000, 3000, GraphVariant::ConnectedVaried, 12345);
    GraphDataset b = make_graph_dataset(1000, 3000, GraphVariant::ConnectedVaried, 12345);
    GraphDataset c = make_graph_dataset(1000, 3000, GraphVariant::ConnectedVaried, 54321);
    ASSERT_EQ(a.edges.size(), b.edges.size());
    bool same = true, differs = false;
    for (std::size_t i = 0; i < a.edges.size(); i++) {
        same = same && a.edges[i].u == b.edges[i].u && a.edges[i].v == b.edges[i].v && a.edges[i].weight == b.edges[i].weight;
        differs = differs || a.edges[i].u != c.edges[i].u || a.edges[i].weight != c.edges[i].weight;
    }
    EXPECT_TRUE(same);
    EXPECT_TRUE(differs);
    EXPECT_EQ(make_sort_keys(100, KeyOrder::Random, 1), make_sort_keys(100, KeyOrder::Random, 1));
    EXPECT_EQ(fnv1a("a"), 0xaf63dc4c8601ec8cULL);   // published FNV-1a test vector
    EXPECT_THROW(make_graph_dataset(10, 5, GraphVariant::ConnectedVaried, 1), std::invalid_argument);    // cannot connect
    EXPECT_THROW(make_graph_dataset(4, 7, GraphVariant::ConnectedVaried, 1), std::invalid_argument);     // more than all pairs
}
