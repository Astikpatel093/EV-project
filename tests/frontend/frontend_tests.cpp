// frontend_tests.cpp - the engine calls the desktop app (EV33 Charge Atlas) makes, tested without a
// window: Engine::planTrip (Trip Planner), Engine::placementWith (Station Placement comparison),
// checkDataset and Engine::addCity (Data Management), and the benchmark file loader.
// Expected values come from the hand-checked sample (data/sample/README.md) and the CLI.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "engine.h"
#include "test_support.hpp"

namespace fs = std::filesystem;
using namespace ev33;

namespace {

std::string slurp(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
void spit(const fs::path &p, const std::string &text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}
// A copy of data/sample in the working directory whose file `file` has `from` replaced by `to`.
std::string brokenSample(const std::string &name, const std::string &file, const std::string &from, const std::string &to) {
    fs::path dir = fs::current_path() / ("fe_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto &e : fs::directory_iterator(evtest::source_path("data/sample")))
        if (e.is_regular_file()) fs::copy_file(e.path(), dir / e.path().filename());
    if (!file.empty()) {
        std::string text = slurp(dir / file);
        size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << "fixture text not found in " << file;
        if (at != std::string::npos) text.replace(at, from.size(), to);
        spit(dir / file, text);
    }
    return dir.string();
}

// An engine with the generated cities plus data/sample as the current city.
struct SampleEngine {
    Engine E;
    int sample = -1;
    SampleEngine() {
        bool ok = E.init(evtest::source_path("data/scenarios"), evtest::source_path("benchmarks"));
        EXPECT_TRUE(ok);
        std::string error;
        sample = E.addCity(evtest::source_path("data/sample"), "sample", error);
        EXPECT_GE(sample, 0) << error;
        E.selectCity(sample);
    }
    int pos(int node_id) {
        const Scene &sc = E.city().sc;
        for (int i = 0; i < sc.nn; i++)
            if (sc.nodes[(size_t)i].id == node_id) return i;
        return -1;
    }
    TripQuery sv1(int from, int to, int method) {
        TripQuery q;
        q.src = pos(from);
        q.dst = pos(to);
        q.vehicle.id = "SV1";
        q.vehicle.battery_kwh = 20;
        q.vehicle.charge_kwh = 12;
        q.vehicle.consumption_kwh_per_km = 0.2;
        q.vehicle.reserve_kwh = 2;
        q.vehicle.connector = "CCS2";
        q.vehicle.max_charge_kw = 50;
        q.start_kwh = 12;
        q.depart_min = 540;
        q.method = method;
        return q;
    }
};

} // namespace

// FE01 - sample trip 2 typed into the Trip Planner gives the hand-checked answer with every method
TEST(Frontend, FE01_TripPlannerMatchesHandCheckedSample) {
    SampleEngine s;
    for (int m = 0; m < PLANNER_COUNT; m++) {
        TripAnswer a = s.E.planTrip(s.sv1(1, 5, m));
        ASSERT_TRUE(a.done);
        ASSERT_TRUE(a.error.empty()) << PLANNER_LABEL[m] << ": " << a.error;
        EXPECT_TRUE(a.plan.feasible()) << PLANNER_LABEL[m];
        EXPECT_NEAR(a.plan.distance_km, 80.0, 1e-6) << PLANNER_LABEL[m];
        EXPECT_EQ(a.plan.stops.size(), 1u) << PLANNER_LABEL[m];
        EXPECT_EQ(a.stop_station.size(), 1u);
        EXPECT_FALSE(a.plan.explanation.empty());
        // the drawn path starts and ends at the chosen junctions and the battery never drops below the reserve
        ASSERT_FALSE(a.path.empty());
        EXPECT_EQ(a.path.front(), s.pos(1));
        EXPECT_EQ(a.path.back(), s.pos(5));
        for (const auto &p : a.profile) EXPECT_GE(p[1], 2.0 - 1e-6);
    }
}

// FE02 - a trip beyond the reachable network is reported as not feasible with a reason, not an error
TEST(Frontend, FE02_InfeasibleTripHasReason) {
    SampleEngine s;
    TripAnswer a = s.E.planTrip(s.sv1(1, 9, PLANNER_EXACT));   // node 9 is in the other component
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_FALSE(a.plan.feasible());
    EXPECT_EQ(a.plan.status, ev::PlanStatus::NoRoadConnection);
    EXPECT_FALSE(a.plan.explanation.empty());
}

// FE03 - invalid form input is rejected with a message and nothing is planned (no silent correction)
TEST(Frontend, FE03_InvalidInputRejected) {
    SampleEngine s;
    TripQuery q = s.sv1(1, 5, PLANNER_EXACT);
    q.src = 999;
    EXPECT_NE(s.E.planTrip(q).error.find("source"), std::string::npos);
    q = s.sv1(1, 5, PLANNER_EXACT);
    q.dst = -1;
    EXPECT_NE(s.E.planTrip(q).error.find("destination"), std::string::npos);
    q = s.sv1(1, 5, PLANNER_EXACT);
    q.start_kwh = 25;   // above the 20 kWh battery
    EXPECT_NE(s.E.planTrip(q).error.find("initial charge"), std::string::npos);
    q = s.sv1(1, 5, PLANNER_EXACT);
    q.vehicle.reserve_kwh = 30;   // reserve above the battery
    TripAnswer a = s.E.planTrip(q);
    EXPECT_FALSE(a.error.empty());
    EXPECT_FALSE(a.plan.feasible());
    q = s.sv1(1, 5, PLANNER_EXACT);
    q.vehicle.consumption_kwh_per_km = 0;
    EXPECT_FALSE(s.E.planTrip(q).error.empty());
}

// FE04 - Floyd-Warshall legs are refused above 1,600 junctions with an explanation
TEST(Frontend, FE04_FloydRefusedOnLargeNetwork) {
    // a 1,700-junction chain (1 km roads), written as a dataset folder
    fs::path dir = fs::current_path() / "fe_chain1700";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string nodes = "id,lat,lon\n", roads = "id,from,to,distance_km\n";
    for (int i = 1; i <= 1700; i++) {
        nodes += std::to_string(i) + ",0," + std::to_string(i * 0.009) + "\n";
        if (i > 1) roads += std::to_string(i - 1) + "," + std::to_string(i - 1) + "," + std::to_string(i) + ",1\n";
    }
    spit(dir / "scenario.cfg", "name = chain\ndata_source = synthetic\nbudget_inr = 1000000\nreference_consumption = 0.2\n");
    spit(dir / "nodes.csv", nodes);
    spit(dir / "roads.csv", roads);
    spit(dir / "vehicles.csv", "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,40,30,0.15,4\n");
    spit(dir / "trips.csv", "id,vehicle,from,to\n1,V,1,20\n");
    Engine E;
    ASSERT_TRUE(E.init(evtest::source_path("data/scenarios"), evtest::source_path("benchmarks")));
    std::string error;
    int chain = E.addCity(dir.string(), "chain", error);
    ASSERT_GE(chain, 0) << error;
    E.selectCity(chain);
    ASSERT_NE(E.run(), nullptr);
    ASSERT_EQ(E.city().sc.nn, 1700);
    TripQuery q;
    q.src = 0;
    q.dst = 19;
    q.vehicle.id = "V";
    q.vehicle.battery_kwh = 40;
    q.vehicle.charge_kwh = 30;
    q.vehicle.consumption_kwh_per_km = 0.15;
    q.vehicle.reserve_kwh = 4;
    q.start_kwh = 30;
    q.method = PLANNER_LEG_FLOYD;
    TripAnswer a = E.planTrip(q);
    EXPECT_NE(a.error.find("1,600"), std::string::npos) << a.error;
    // the other methods still answer on the same network
    q.method = PLANNER_LEG_DIJKSTRA;
    TripAnswer d = E.planTrip(q);
    ASSERT_TRUE(d.error.empty()) << d.error;
    EXPECT_TRUE(d.plan.feasible());
    EXPECT_NEAR(d.plan.distance_km, 19.0, 1e-6);
}

// FE05 - the placement comparison: DP is optimal (155 on the sample), greedy reaches 140, all within budget
TEST(Frontend, FE05_PlacementMethodsOnSample) {
    SampleEngine s;
    ev::PlacementResult dp = s.E.placementWith(SEL_DP);
    ev::PlacementResult greedy = s.E.placementWith(SEL_GREEDY);
    ev::PlacementResult bnb = s.E.placementWith(SEL_BNB_LC);
    EXPECT_EQ(dp.benefit, 155);
    EXPECT_EQ(bnb.benefit, 155);
    EXPECT_EQ(greedy.benefit, 140);
    EXPECT_TRUE(dp.optimal);
    for (const ev::PlacementResult *r : { &dp, &greedy, &bnb }) {
        EXPECT_LE(r->cost, r->budget);
        EXPECT_EQ(r->budget, 10000000LL * 100);   // 1 crore rupees in paise
    }
    // the run the Dashboard shows uses the same answer
    ev33::Run *R = s.E.run();
    ASSERT_NE(R, nullptr);
    EXPECT_DOUBLE_EQ(R->run.sel.value, 155.0);
}

// FE06 - Data Management: the sample passes with the right counts
TEST(Frontend, FE06_CheckDatasetAcceptsSample) {
    DatasetCheck d = checkDataset(evtest::source_path("data/sample"));
    ASSERT_TRUE(d.ok) << d.error;
    EXPECT_EQ(d.nodes, 10);
    EXPECT_EQ(d.roads, 11);
    EXPECT_EQ(d.stations, 3);
    EXPECT_EQ(d.offline, 1);
    EXPECT_EQ(d.candidates, 6);
    EXPECT_EQ(d.vehicles, 3);
    EXPECT_EQ(d.trips, 8);
    EXPECT_EQ(d.components, 2);
    EXPECT_EQ(d.source, "synthetic");
}

// FE07 - Data Management: each kind of bad record is rejected with file:line and nothing is loaded
TEST(Frontend, FE07_CheckDatasetRejectsBadRecords) {
    struct Case { const char *name, *file, *from, *to, *expect; };
    const Case cases[] = {
        { "negdist", "roads.csv", "1,1,2,20,,", "1,1,2,-20,,", "roads.csv:3" },
        { "badnum", "roads.csv", "1,1,2,20,,", "1,1,2,twenty,,", "roads.csv:3" },
        { "dupnode", "nodes.csv", "2,0.000000,0.179856", "1,0.000000,0.179856", "nodes.csv:4" },
        { "missingref", "roads.csv", "2,2,3,20,,", "2,2,77,20,,", "roads.csv:4" },
        { "badcost", "candidates.csv", "501,Junction A site,2,4000000", "501,Junction A site,2,-4000000", "candidates.csv:3" },
        { "badbattery", "vehicles.csv", "SV1,Sample hatchback,20,12", "SV1,Sample hatchback,0,12", "vehicles.csv:3" },
        { "missingcol", "roads.csv", "id,from,to,distance_km", "id,from,to,length", "roads.csv" },
    };
    for (const Case &c : cases) {
        std::string dir = brokenSample(c.name, c.file, c.from, c.to);
        DatasetCheck d = checkDataset(dir);
        EXPECT_FALSE(d.ok) << c.name;
        EXPECT_NE(d.error.find(c.expect), std::string::npos) << c.name << ": " << d.error;
        // the app refuses to add it as a city and keeps its current one
        SampleEngine s;
        size_t before = s.E.cities.size();
        std::string error;
        EXPECT_EQ(s.E.addCity(dir, c.name, error), -1) << c.name;
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(s.E.cities.size(), before);
        EXPECT_EQ(s.E.plan.city, s.sample);
    }
}

// FE08 - Data Management: a missing required file is named
TEST(Frontend, FE08_MissingFileNamed) {
    std::string dir = brokenSample("nofile", "", "", "");
    fs::remove(fs::path(dir) / "roads.csv");
    DatasetCheck d = checkDataset(dir);
    EXPECT_FALSE(d.ok);
    EXPECT_NE(d.error.find("roads.csv"), std::string::npos) << d.error;
    DatasetCheck none = checkDataset((fs::current_path() / "no_such_folder").string());
    EXPECT_FALSE(none.ok);
    EXPECT_FALSE(none.error.empty());
}

// FE09 - Benchmarks view: the committed results load; a missing file gives no rows and an error, not invented data
TEST(Frontend, FE09_BenchmarkResults) {
    Engine E;
    ASSERT_TRUE(E.init(evtest::source_path("data/scenarios"), evtest::source_path("benchmarks")));
    const std::vector<BenchRow> &rows = E.bench();
    ASSERT_FALSE(rows.empty()) << E.bench_error;
    for (const BenchRow &r : rows) {
        EXPECT_GT(r.reps, 0);
        EXPECT_GE(r.ms, 0);
        EXPECT_LE(r.min_ms, r.ms + 1e-9);
        EXPECT_GE(r.max_ms, r.ms - 1e-9);
    }
    E.reloadBench((fs::current_path() / "missing_results.csv").string());
    EXPECT_TRUE(E.bench().empty());
    EXPECT_FALSE(E.bench_error.empty());
}
