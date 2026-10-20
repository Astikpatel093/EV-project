// engine.h - C++ wrapper the desktop app uses to drive the EV33 C library.
//
// Holds the five cities, the current plan (strategy, budget, what-if edits), and the results the
// views draw: the run (selection, plans, capacity, sessions), the day simulation for both driver
// policies, a replayed trip, the strategy comparison, the analysis report and the benchmarks.
// Everything runs on the UI thread; the slow jobs (comparison, report) are split into steps.
#pragma once

extern "C" {
#include "ev_mem.h"
#include "ev_model.h"
#include "ev_plan.h"
#include "ev_report.h"
#include "ev_sim.h"
}

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "json.h"

namespace ev33 {

/* ---------------------------------------------------------------- city geometry */
struct Terrain {
    bool ok = false;
    int gw = 0, gh = 0;
    std::vector<float> V;            // elevation grid, row 0 = north
    float zmin = 0, zmax = 0;
    struct Level { float L; bool major; std::vector<float> segs; };  // x0 y0 x1 y1 ... in km
    std::vector<Level> levels;
};

struct City {
    std::string name, label, blurb;
    bool loaded = false, failed = false;
    std::string error;
    ev_scenario sc{};
    int default_budget = 0;
    double minx = 0, miny = 0, maxx = 1, maxy = 1, minz = 0, maxz = 0;
    Terrain terrain;
};

/* ---------------------------------------------------------------- plan */
struct Edits {
    std::set<int> lock, ban;        // station indices
    std::map<int, int> ports;       // station index -> port count
    bool empty() const { return lock.empty() && ban.empty() && ports.empty(); }
    size_t count() const { return lock.size() + ban.size() + ports.size(); }
    bool operator==(const Edits &o) const { return lock == o.lock && ban == o.ban && ports == o.ports; }
};

struct Plan {
    int city = 0;
    int sel = EV_SEL_DP;
    int route = EV_ROUTE_DIJKSTRA;
    int budget = 0;
    Edits edits;
    bool operator==(const Plan &o) const {
        return city == o.city && sel == o.sel && route == o.route && budget == o.budget && edits == o.edits;
    }
    bool operator!=(const Plan &o) const { return !(*this == o); }
};

struct Sample { int trip; int kind; double km; int stops; };  // kind 2 multi-stop, 1 one stop, 0 direct, -1 not possible

struct Run {
    Plan plan;
    bool valid = false;
    ev_run run{};
    ev_capacity cap{};
    ev_sessions ses{};
    std::vector<int> ports;          // effective port count per station (edits applied)
    std::vector<char> built;         // per station
    std::vector<int> edge_load;      // planned trips per road
    int edge_load_max = 1;
    std::vector<Sample> samples;
    std::vector<int> short_of;       // per station: chargers needed - ports (> 0 when short)
    double total_ms = 0;
    Run() = default;
    Run(const Run &) = delete;
    Run &operator=(const Run &) = delete;
    ~Run();
};

/* ---------------------------------------------------------------- day simulation */
struct LiveState {
    int road = 0, charging_total = 0, waiting_total = 0, arrived = 0, ncars = 0;
    double kw = 0, kwh = 0;
    std::vector<int> charging, waiting;   // per simulated station (index into SimView::st)
    std::vector<float> cars;              // x, y, battery fraction
};

struct SimView {
    bool valid = false;
    int policy = 0;
    ev_sim sim{};
    std::vector<float> cum;               // distance along each vehicle path
    std::vector<double> ends;             // sorted arrival times
    std::map<int, int> local;             // station index -> row in sim.st
    double end_min = 1440;
    std::vector<double> series_t, series_kw;
    const ev_scenario *sc = nullptr;
    SimView() = default;
    SimView(const SimView &) = delete;
    SimView &operator=(const SimView &) = delete;
    ~SimView();
    void build(const ev_scenario *s);
    void at(double T, bool light, LiveState &out) const;
};

/* ---------------------------------------------------------------- trip replay */
struct TripDetail {
    bool valid = false;
    int trip = -1;                        // scenario trip index
    ev_plan plan{};
    std::vector<int> path;
    std::vector<std::array<double, 3>> profile;   // km, kWh after the move, elevation m
};

/* ---------------------------------------------------------------- comparison, report, bench */
struct CompareRow {
    int sel, route, stations;
    long cost, value;
    bool optimal;
    double select_ms;
    long search_nodes;
    int feasible;
    double feasible_pct, avg_km, avg_kwh, route_ms;
    long sssp;
};
struct Compare { int city = -1; int next = 0; bool done = false; std::vector<CompareRow> rows; };

struct Analysis { bool done = false; std::string text; Json data; };

struct BenchRow {
    std::string family, alg, dataset, note;
    double n = 0, ms = 0, quality = 0;
    long peak = 0;
    int check = 0;
};

struct Baseline {
    bool set = false;
    Plan plan;
    ev_kpi kpi{};
    long cost = 0;
    int stations = 0;
    std::string label;
    double sim_wait[2] = { -1, -1 };      // total waiting minutes per policy, computed on demand
};

/* ---------------------------------------------------------------- engine */
class Engine {
public:
    std::vector<City> cities;
    Plan plan;
    std::string data_dir, results_dir;
    std::string status;                   // last engine message (errors start with "!")

    bool init(const std::string &data, const std::string &results);
    City &city() { return cities[(size_t)plan.city]; }
    bool load(int i);
    void selectCity(int i);               // resets budget and edits

    // current run; recomputed when the plan changed
    Run *run();
    void invalidate() { run_.reset(); sims_[0].reset(); sims_[1].reset(); trip_ = TripDetail(); }

    SimView *sim(int policy);             // day simulation for the current run
    const TripDetail &trip(int trip_index);

    // what-if edits on the current plan
    void setBuild(int station, int mode); // 0 engine decides, 1 always build, 2 never build
    void setPorts(int station, int ports);
    int basePorts(int station) const;
    int effectivePorts(int station) const;
    std::string editKey() const;          // l<id>,b<id>,p<id>=n

    // baseline
    Baseline baseline;
    void pinBaseline();
    void clearBaseline() { baseline = Baseline(); }
    double baselineWait(int policy);

    // comparison (call step() each frame until done)
    Compare &compare(int city);
    void stepCompare(Compare &c);

    // analysis report (computed on first request)
    Analysis &analysis(int city);
    void computeAnalysis(int city);

    // benchmarks from results/bench.csv
    const std::vector<BenchRow> &bench();
    std::string bench_error;

    std::string planLabel(const Plan &p) const;
    // headline figures of any plan (used for baselines the caller keeps itself)
    ev_kpi kpiFor(const Plan &p, long *cost = nullptr, int *stations = nullptr);
    std::string sitesCsv();

private:
    std::unique_ptr<Run> run_;
    std::unique_ptr<SimView> sims_[2];
    TripDetail trip_;
    std::map<int, Compare> compares_;
    std::map<int, Analysis> analyses_;
    std::vector<BenchRow> bench_;
    bool bench_loaded_ = false;
    void compute(Run &R, const Plan &P);
    void applyPorts(const Plan &P, std::vector<int> &saved);
    void restorePorts(const Plan &P, const std::vector<int> &saved);
    double simWait(const Plan &P, int policy);
};

extern const char *const SELECT_LABEL[EV_SEL_COUNT];
extern const char *const SELECT_LONG[EV_SEL_COUNT];
extern const char *const ROUTE_LABEL[EV_ROUTE_COUNT];
extern const char *const ROUTE_HINT[EV_ROUTE_COUNT];

} // namespace ev33
