// engine.h - the layer both front ends (desktop app and web server) use to drive the EV33 engine.
//
// Holds the cities (scenarios), the current plan (placement method, routing method, budget,
// what-if edits) and the results the views draw: the run (placement, trip plans, capacity,
// sessions), the day simulation for both driver policies, a replayed trip, the strategy
// comparison, the analysis report and the benchmarks.
//
// The engine itself lives in namespace ev (include/ev/...). This file flattens its results into
// small "view" records with plain fields so the drawing code stays simple:
//   * stations: one list of every charging site the user can see - existing stations first
//     (always present), then candidate sites (built or not, decided by the placement method)
//   * money in lakh rupees (double) for display; the engine itself works in integer paise
// Everything runs on the caller's thread; the slow jobs (comparison) are split into steps.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ev/analysis/pipeline.hpp"
#include "ev/core/money.hpp"
#include "ev/models/scenario.hpp"
#include "ev/operations/capacity.hpp"
#include "ev/operations/sessions.hpp"
#include "ev/operations/simulation.hpp"
#include "json.h"

namespace ev33 {

/* ---------------------------------------------------------------- method ids (match ev:: enum order) */
enum { SEL_GREEDY = 0, SEL_DP, SEL_BNB_LC, SEL_BNB_FIFO, SEL_BNB_LIFO, SEL_COUNT };
enum { ROUTE_DIJKSTRA = 0, ROUTE_FLOYD, ROUTE_BELLMAN, ROUTE_COUNT };
enum { SEG_DRIVE = 0, SEG_WAIT = 1, SEG_CHARGE = 2 };
const char *selName(int sel);        // "greedy", "dp", "bnb-lc", "bnb-fifo", "bnb-lifo"
const char *routeName(int route);    // "dijkstra", "floyd", "bellman"
int selParse(const std::string &s);  // -1 if unknown
int routeParse(const std::string &s);
const int FLOYD_MAX_NODES = 1600;    // Floyd-Warshall keeps a V x V table: skipped above this

/* ---------------------------------------------------------------- scenario view */
struct NodeV {
    int id = 0;
    std::string name;
    double x = 0, y = 0;   // km on a local flat projection (east, north)
    double elev = 0;       // metres
    double lat = 0, lon = 0;
};
struct EdgeV { int id = 0, u = 0, v = 0; double km = 0; };   // u, v = node positions
struct SiteV {
    int id = 0;
    std::string name;
    int node = 0;              // node position
    bool existing = false;     // an existing station (not part of the placement decision)
    int source = 0;            // index into Scenario::stations or Scenario::candidates
    double cost = 0;           // installation cost, lakh rupees (existing: historical cost)
    ev::Paise cost_paise = 0;
    int ports = 1;
    double power_kw = 0;
    long value = 0;            // candidate benefit score (existing: 0)
    bool estimated = false;    // benefit estimated from the trips
    bool available = true;     // existing station that is online
    std::string status = "available", connector;
    int maint_period_h = 0, maint_offset_h = 0;
};
struct VehicleV {
    std::string id, model, connector;
    double battery_kwh = 0, consumption = 0, reserve_kwh = 0, max_charge_kw = 0;
    int trips = 0;
};
struct TripV {
    int id = 0, src = 0, dst = 0;   // src, dst = node positions
    int depart_min = 0;
    int vehicle = 0;                // index into Scene::vehicles
    std::string vehicle_id;
    double start_kwh = 0, battery_kwh = 0, reserve_kwh = 0;
    double soc0 = 0;                // start_kwh / battery_kwh
};
struct CfgV {
    std::string name, source, source_note;
    double budget = 0;              // lakh
    double target_frac = 0.8, regen_eff = 0.6, elev_kwh_per_m = 0, speed_kmh = 40, max_detour_km = 15, grid_kw = 0;
    double reference_consumption = 0.18, battery_step_kwh = 0.25;
    int window_min = 120, session_min = 30, horizon_h = 24;
    // fleet figures, trip-weighted, for summaries only (every plan uses its own vehicle)
    double battery_kwh = 0, reserve_frac = 0, cons_kwh_km = 0;
};
struct Scene {
    CfgV cfg;
    int nn = 0, ne = 0, ns = 0, nt = 0, nexist = 0, nveh = 0;
    std::vector<NodeV> nodes;
    std::vector<EdgeV> edges;
    std::vector<SiteV> stations;    // existing stations [0, nexist), then candidates
    std::vector<TripV> trips;
    std::vector<VehicleV> vehicles;
    long sessions = 0, events = 0;  // booked sessions, event-log length
    int candidates() const { return ns - nexist; }
    int candidateStation(int candidate_index) const { return nexist + candidate_index; }
};

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
    std::unique_ptr<ev::Scenario> scenario;
    std::unique_ptr<ev::ScenarioContext> ctx;
    Scene sc;
    int default_budget = 0;          // lakh
    double minx = 0, miny = 0, maxx = 1, maxy = 1, minz = 0, maxz = 0;
    Terrain terrain;
};

/* ---------------------------------------------------------------- plan */
struct Edits {
    std::set<int> lock, ban;        // station indices (candidates only)
    std::map<int, int> ports;       // station index -> port count
    bool empty() const { return lock.empty() && ban.empty() && ports.empty(); }
    size_t count() const { return lock.size() + ban.size() + ports.size(); }
    bool operator==(const Edits &o) const { return lock == o.lock && ban == o.ban && ports == o.ports; }
};

struct Plan {
    int city = 0;
    int sel = SEL_DP;
    int route = ROUTE_DIJKSTRA;
    int budget = 0;                 // lakh
    Edits edits;
    bool operator==(const Plan &o) const {
        return city == o.city && sel == o.sel && route == o.route && budget == o.budget && edits == o.edits;
    }
    bool operator!=(const Plan &o) const { return !(*this == o); }
};

/* ---------------------------------------------------------------- run view */
struct PlanV {
    int trip = 0;
    bool feasible = false, needs_charge = false;
    ev::PlanStatus status = ev::PlanStatus::NoRoadConnection;
    std::string status_name, explanation;
    double km = 0, kwh = 0, direct_km = 0;
    double depart_min = 0, arrive_min = 0, charge_min = 0;
    double start_kwh = 0, arrive_kwh = 0, min_kwh = 0;
    int n_stops = 0;
    std::vector<int> stop;                                   // station indices
    std::vector<double> stop_arrive_min, stop_charge_min, stop_arrive_kwh, stop_depart_kwh;
};
using KpiV = ev::Kpi;
struct SelV {
    int mode = SEL_DP;
    int n = 0;                       // candidate sites built
    std::vector<int> idx;            // their station indices, ascending
    double value = 0, cost = 0;      // benefit, cost in lakh
    double ms = 0;
    long search_nodes = 0;
    bool optimal = false, over_budget = false;
    std::string note;
};
struct RunV {
    int sel_mode = SEL_DP, route_mode = ROUTE_DIJKSTRA;
    SelV sel;
    std::vector<int> sites;          // station index of every charger site in the run (existing + built)
    std::vector<PlanV> plans;
    KpiV kpi;
    double ms_values = 0, ms_select = 0, ms_router = 0, ms_plan = 0;
    long sssp = 0;                   // shortest-path trees built by the router
    std::unique_ptr<ev::PlanningRun> pr;
};
struct CapV {
    int trips_needing = 0;
    long slots_total = 0, flow_dfs = 0, flow_bfs = 0, aug_dfs = 0, aug_bfs = 0, min_cut = 0;
    int matching = 0;
    double ms_dfs = 0, ms_bfs = 0, ms_match = 0;
    int bottleneck_station = -1;     // station index
    std::vector<int> assigned;       // per run site
    bool consistent = true;
};
struct LoadV { int station = 0, sessions = 0, chargers_needed = 0, depth = 0, served = 0; };
struct SessV {
    int n = 0;
    std::vector<LoadV> st;           // one per run site
    int overloaded = 0;
    long sessions = 0, served = 0;
    std::vector<double> hourly_kwh;
    int surge_lo = -1, surge_hi = -1;
    double surge_excess_kwh = 0;
};

struct Sample { int trip; int kind; double km; int stops; };  // kind 2 multi-stop, 1 one stop, 0 direct, -1 not possible

struct Run {
    Plan plan;
    bool valid = false;
    RunV run;
    CapV cap;
    SessV ses;
    std::vector<int> ports;          // effective port count per station (edits applied)
    std::vector<char> built;         // per station: present in this run (existing, or built candidate)
    std::vector<int> edge_load;      // planned trips per road
    int edge_load_max = 1;
    std::vector<Sample> samples;
    std::vector<int> short_of;       // per station: chargers needed - ports (> 0 when short)
    double total_ms = 0;
    Run() = default;
    Run(const Run &) = delete;
    Run &operator=(const Run &) = delete;
};

/* ---------------------------------------------------------------- day simulation */
struct SegV { int vehicle = 0, kind = 0; double t0 = 0, t1 = 0; int p0 = 0, p1 = 0, station = -1; double e0 = 0, e1 = 0; };
struct SimSiteV { int station = 0, sessions = 0, waited = 0; double wait_min = 0; int max_queue = 0, diverted_away = 0; };
struct SimV {
    int nv = 0, nseg = 0, nst = 0;
    std::vector<int> trip, path_off, path, seg_off;
    std::vector<double> battery;
    std::vector<SegV> seg;
    std::vector<SimSiteV> st;
    long events = 0;
    int diverted = 0, delayed = 0;
    double total_wait = 0, max_wait = 0, mean_extra_min = 0, end_min = 0, ms = 0;
};

struct LiveState {
    int road = 0, charging_total = 0, waiting_total = 0, arrived = 0, ncars = 0;
    double kw = 0, kwh = 0;
    std::vector<int> charging, waiting;   // per simulated station (index into SimV::st)
    std::vector<float> cars;              // x, y, battery fraction
};

struct SimView {
    bool valid = false;
    int policy = 0;
    SimV sim;
    std::vector<float> cum;               // distance along each vehicle path
    std::vector<double> ends;             // sorted arrival times
    std::map<int, int> local;             // station index -> row in sim.st
    double end_min = 1440;
    std::vector<double> series_t, series_kw;
    const Scene *sc = nullptr;
    void build(const Scene *s);
    void at(double T, bool light, LiveState &out) const;
};

/* ---------------------------------------------------------------- trip replay */
struct TripDetail {
    bool valid = false;
    int trip = -1;                        // scenario trip index
    PlanV plan;
    std::vector<int> path;
    std::vector<std::array<double, 3>> profile;   // km, kWh after the move, elevation m
};

/* ---------------------------------------------------------------- comparison, report, bench */
struct CompareRow {
    int sel, route, stations;
    double cost, value;
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
    std::string family, alg, dataset, variant, validation, note, compiler, build;
    double n = 0, ms = 0, min_ms = 0, max_ms = 0;
    long peak = 0;
    int reps = 0;
    bool pass = false;
};

struct Baseline {
    bool set = false;
    Plan plan;
    KpiV kpi{};
    double cost = 0;
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
    void setBuild(int station, int mode); // 0 engine decides, 1 always build, 2 never build (candidates only)
    void setPorts(int station, int ports);
    int basePorts(int station) const;
    int effectivePorts(int station) const;
    std::string editKey() const;          // l<id>,b<id>,p<id>=n

    // baseline
    Baseline baseline;
    void pinBaseline();
    void clearBaseline() { baseline = Baseline(); }
    double baselineWait(int policy);

    // comparison (call stepCompare() each frame until done)
    Compare &compare(int city);
    void stepCompare(Compare &c);

    // analysis report (computed on first request)
    Analysis &analysis(int city);
    void computeAnalysis(int city);

    // benchmarks from benchmarks/results.csv
    const std::vector<BenchRow> &bench();
    std::string bench_error, bench_path;

    std::string planLabel(const Plan &p) const;
    // headline figures of any plan (used for baselines the caller keeps itself)
    KpiV kpiFor(const Plan &p, double *cost = nullptr, int *stations = nullptr);
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
    ev::RunOptions options(const Plan &P) const;
    void simulate(Run &R, int policy, SimV &out);
};

extern const char *const SELECT_LABEL[SEL_COUNT];
extern const char *const SELECT_LONG[SEL_COUNT];
extern const char *const ROUTE_LABEL[ROUTE_COUNT];
extern const char *const ROUTE_HINT[ROUTE_COUNT];

double nowMs();
double toLakh(ev::Paise p);

} // namespace ev33
