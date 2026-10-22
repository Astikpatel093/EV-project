// engine.cpp - see engine.h.
#include "engine.h"
#include "fmt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "ev/analysis/report.hpp"
#include "ev/core/timer.hpp"
#include "ev/io/csv.hpp"
#include "ev/io/json_writer.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/placement/placement.hpp"
#include "ev/planner/exact_planner.hpp"
#include "ev/planner/leg_planner.hpp"
#include "ev/planner/router.hpp"

namespace ev33 {

const char *const SELECT_LABEL[SEL_COUNT] = { "Greedy ratio", "Exact DP", "B&B least cost", "B&B FIFO", "B&B LIFO" };
const char *const SELECT_LONG[SEL_COUNT] = {
    "Greedy: benefit per rupee", "Exact: 0/1 knapsack DP", "Exact: branch and bound, least cost",
    "Exact: branch and bound, FIFO", "Exact: branch and bound, LIFO" };
const char *const ROUTE_LABEL[ROUTE_COUNT] = { "Dijkstra", "Floyd\xE2\x80\x93Warshall", "Bellman\u2013Ford" };
const char *const ROUTE_HINT[ROUTE_COUNT] = {
    "Shortest distance. One cached tree per origin.",
    "Shortest distance, all pairs computed up front in \xCE\x98(V\xC2\xB3).",
    "Least energy. Handles the negative energy of braking downhill." };

static ev::PlacementMethod toPlacement(int s) { return static_cast<ev::PlacementMethod>(std::max(0, std::min(SEL_COUNT - 1, s))); }
static ev::RoutingMethod toRouting(int r) { return static_cast<ev::RoutingMethod>(std::max(0, std::min(ROUTE_COUNT - 1, r))); }

const char *selName(int s) { return ev::to_string(toPlacement(s)); }
const char *routeName(int r) { return ev::to_string(toRouting(r)); }
int selParse(const std::string &s) {
    ev::PlacementMethod m;
    return ev::parse_placement(s, m) ? static_cast<int>(m) : -1;
}
int routeParse(const std::string &s) {
    ev::RoutingMethod m;
    return ev::parse_routing(s, m) ? static_cast<int>(m) : -1;
}

double nowMs() {
    static const ev::Stopwatch origin;
    return origin.elapsed_ms();
}
double toLakh(ev::Paise p) { return static_cast<double>(p) / static_cast<double>(ev::kPaisePerLakh); }

struct Reg { const char *name, *label, *blurb; };
static const Reg REG[] = {
    { "medium", "Normal operation", "A synthetic 256-junction city: 10 existing stations, 60 candidate sites, 4 vehicle models and 1,200 daily trips." },
    { "shock", "Constraint shock", "The same city with the budget cut 45%, bridges closed except two, some sites down to one port, an offline station and evening demand up 60%." },
    { "large", "Large-data stress", "1,024 junctions, 30 existing stations, 220 candidate sites and 6,000 trips." },
    { "adversarial", "Long corridor", "A 600 km two-lane corridor with identical site costs and no existing stations: a stress test for the placement model." },
    { "small", "Small town", "36 junctions, 3 existing stations, 12 candidate sites and 60 trips: small enough to check by hand." },
};

/* ======================================================================== scenario view */
static void buildScene(City &c) {
    const ev::Scenario &s = *c.scenario;
    const ev::RoadNetwork &net = s.network;
    Scene &v = c.sc;
    v = Scene();
    const ev::ScenarioConfig &k = s.config;
    v.cfg.name = k.name;
    v.cfg.source = ev::to_string(k.source);
    v.cfg.source_note = k.source_note;
    v.cfg.budget = toLakh(k.budget);
    v.cfg.target_frac = k.target_soc;
    v.cfg.regen_eff = k.regen_efficiency;
    v.cfg.elev_kwh_per_m = k.climb_kwh_per_m;
    v.cfg.speed_kmh = k.speed_kmh;
    v.cfg.max_detour_km = k.max_detour_km;
    v.cfg.grid_kw = k.grid_kw;
    v.cfg.reference_consumption = k.reference_consumption;
    v.cfg.battery_step_kwh = k.battery_step_kwh;
    v.cfg.window_min = k.window_min;
    v.cfg.session_min = k.session_min;
    v.cfg.horizon_h = k.horizon_h;

    v.nn = net.node_count();
    v.ne = net.edge_count();
    std::vector<std::pair<double, double>> xy = net.planar_km();
    v.nodes.resize((size_t)v.nn);
    for (int i = 0; i < v.nn; i++) {
        const ev::RoadNode &n = net.node(i);
        NodeV &o = v.nodes[(size_t)i];
        o.id = n.id;
        o.name = n.name.empty() ? "J" + std::to_string(n.id) : n.name;
        o.x = xy[(size_t)i].first;
        o.y = xy[(size_t)i].second;
        o.elev = n.elevation_m;
        if (n.location) { o.lat = n.location->lat; o.lon = n.location->lon; }
    }
    v.edges.resize((size_t)v.ne);
    for (int e = 0; e < v.ne; e++) {
        EdgeV &o = v.edges[(size_t)e];
        o.id = net.edges()[(size_t)e].id;
        o.u = net.from_index(e);
        o.v = net.to_index(e);
        o.km = net.edges()[(size_t)e].distance_km;
    }
    for (size_t i = 0; i < s.stations.size(); i++) {
        const ev::ChargingStation &st = s.stations[i];
        SiteV o;
        o.id = st.id; o.name = st.name; o.node = net.index_of(st.node); o.existing = true; o.source = (int)i;
        o.cost_paise = st.install_cost; o.cost = toLakh(st.install_cost);
        o.ports = st.ports; o.power_kw = st.power_kw;
        o.available = st.status == ev::StationStatus::Available;
        o.status = ev::to_string(st.status);
        o.connector = st.connector;
        o.maint_period_h = st.maint_period_h; o.maint_offset_h = st.maint_offset_h;
        v.stations.push_back(o);
    }
    v.nexist = (int)v.stations.size();
    const ev::BenefitTable &B = c.ctx->benefits;
    for (size_t i = 0; i < s.candidates.size(); i++) {
        const ev::CandidateStation &cd = s.candidates[i];
        SiteV o;
        o.id = cd.id; o.name = cd.name; o.node = net.index_of(cd.node); o.existing = false; o.source = (int)i;
        o.cost_paise = cd.install_cost; o.cost = toLakh(cd.install_cost);
        o.ports = cd.ports; o.power_kw = cd.power_kw;
        o.value = (long)B.benefit[i];
        o.estimated = B.estimated[i];
        o.status = "candidate";
        o.connector = cd.connector;
        o.maint_period_h = cd.maint_period_h; o.maint_offset_h = cd.maint_offset_h;
        v.stations.push_back(o);
    }
    v.ns = (int)v.stations.size();
    for (const ev::ElectricVehicle &ve : s.vehicles) {
        VehicleV o;
        o.id = ve.id; o.model = ve.model; o.connector = ve.connector;
        o.battery_kwh = ve.battery_kwh; o.consumption = ve.consumption_kwh_per_km; o.reserve_kwh = ve.reserve_kwh; o.max_charge_kw = ve.max_charge_kw;
        v.vehicles.push_back(o);
    }
    v.nveh = (int)v.vehicles.size();
    double sb = 0, sr = 0, sc = 0;
    for (const ev::TripRequest &t : s.trips) {
        TripV o;
        o.id = t.id;
        o.src = net.index_of(t.from);
        o.dst = net.index_of(t.to);
        o.depart_min = (int)std::lround(t.depart_min);
        o.vehicle = s.vehicle_index(t.vehicle_id);
        o.vehicle_id = t.vehicle_id;
        const ev::ElectricVehicle &ve = s.vehicles[(size_t)o.vehicle];
        o.battery_kwh = ve.battery_kwh;
        o.reserve_kwh = ve.reserve_kwh;
        o.start_kwh = t.start_charge_kwh ? *t.start_charge_kwh : ve.charge_kwh;
        o.soc0 = o.battery_kwh > 0 ? o.start_kwh / o.battery_kwh : 0;
        v.vehicles[(size_t)o.vehicle].trips++;
        sb += ve.battery_kwh; sr += ve.reserve_kwh / ve.battery_kwh; sc += ve.consumption_kwh_per_km;
        v.trips.push_back(o);
    }
    v.nt = (int)v.trips.size();
    if (v.nt > 0) { v.cfg.battery_kwh = sb / v.nt; v.cfg.reserve_frac = sr / v.nt; v.cfg.cons_kwh_km = sc / v.nt; }
    v.sessions = (long)s.sessions.size();
    v.events = (long)s.events.size();
}

/* ======================================================================== terrain (IDW grid + marching squares) */
static void buildTerrain(City &c) {
    const Scene &sc = c.sc;
    Terrain &T = c.terrain;
    double w = c.maxx - c.minx, h = c.maxy - c.miny;
    if (w <= 0 || h <= 0 || w / h > 8 || h / w > 8) return;
    int gw = 120, gh = std::max(8, std::min(120, (int)std::lround(gw * h / w)));
    double avg = 0;
    for (int i = 0; i < sc.ne; i++) avg += sc.edges[(size_t)i].km;
    avg /= std::max(1, sc.ne);
    double R = std::max(avg * 1.6, w / 40), bs = R;
    int bw = (int)std::ceil(w / bs) + 1, bh = (int)std::ceil(h / bs) + 1;
    std::vector<std::vector<int>> buckets((size_t)(bw * bh));
    for (int i = 0; i < sc.nn; i++) {
        int bx = std::max(0, std::min(bw - 1, (int)std::floor((sc.nodes[(size_t)i].x - c.minx) / bs)));
        int by = std::max(0, std::min(bh - 1, (int)std::floor((sc.nodes[(size_t)i].y - c.miny) / bs)));
        buckets[(size_t)(by * bw + bx)].push_back(i);
    }
    T.gw = gw; T.gh = gh;
    T.V.assign((size_t)(gw * gh), 0.f);
    for (int gy = 0; gy < gh; gy++) {
        double y = c.maxy - (gy + 0.5) / gh * h;
        for (int gx = 0; gx < gw; gx++) {
            double x = c.minx + (gx + 0.5) / gw * w;
            int bx = (int)std::floor((x - c.minx) / bs), by = (int)std::floor((y - c.miny) / bs);
            double sw = 0, sv = 0, best = 1e300, bestv = 0;
            for (int yy = by - 1; yy <= by + 1; yy++) {
                if (yy < 0 || yy >= bh) continue;
                for (int xx = bx - 1; xx <= bx + 1; xx++) {
                    if (xx < 0 || xx >= bw) continue;
                    for (int i : buckets[(size_t)(yy * bw + xx)]) {
                        const NodeV &n = sc.nodes[(size_t)i];
                        double d2 = (n.x - x) * (n.x - x) + (n.y - y) * (n.y - y);
                        if (d2 < best) { best = d2; bestv = n.elev; }
                        if (d2 < R * R) { double wt = 1 / (d2 + 0.05); sw += wt; sv += wt * n.elev; }
                    }
                }
            }
            T.V[(size_t)(gy * gw + gx)] = (float)(sw > 0 ? sv / sw : bestv);
        }
    }
    T.zmin = 1e30f; T.zmax = -1e30f;
    for (float v : T.V) { T.zmin = std::min(T.zmin, v); T.zmax = std::max(T.zmax, v); }
    auto cx = [&](int gx) { return (float)(c.minx + (gx + 0.5) / gw * w); };
    auto cy = [&](int gy) { return (float)(c.maxy - (gy + 0.5) / gh * h); };
    for (float L = std::ceil(T.zmin / 50) * 50; L <= T.zmax; L += 50) {
        Terrain::Level lev{ L, std::fmod(L, 250.f) == 0.f, {} };
        for (int gy = 0; gy + 1 < gh; gy++)
            for (int gx = 0; gx + 1 < gw; gx++) {
                float a = T.V[(size_t)(gy * gw + gx)], b = T.V[(size_t)(gy * gw + gx + 1)];
                float cc = T.V[(size_t)((gy + 1) * gw + gx + 1)], d = T.V[(size_t)((gy + 1) * gw + gx)];
                int idx = (a >= L ? 8 : 0) | (b >= L ? 4 : 0) | (cc >= L ? 2 : 0) | (d >= L ? 1 : 0);
                if (idx == 0 || idx == 15) continue;
                auto lerp = [&](float p, float q) { float den = q - p; return (L - p) / (den == 0 ? 1e-9f : den); };
                float top[2] = { cx(gx) + lerp(a, b) * (cx(gx + 1) - cx(gx)), cy(gy) };
                float right[2] = { cx(gx + 1), cy(gy) + lerp(b, cc) * (cy(gy + 1) - cy(gy)) };
                float bottom[2] = { cx(gx) + lerp(d, cc) * (cx(gx + 1) - cx(gx)), cy(gy + 1) };
                float left[2] = { cx(gx), cy(gy) + lerp(a, d) * (cy(gy + 1) - cy(gy)) };
                auto add = [&](const float *p, const float *q) { lev.segs.insert(lev.segs.end(), { p[0], p[1], q[0], q[1] }); };
                switch (idx) {
                case 1: case 14: add(left, bottom); break;
                case 2: case 13: add(bottom, right); break;
                case 3: case 12: add(left, right); break;
                case 4: case 11: add(top, right); break;
                case 5: add(left, top); add(bottom, right); break;
                case 6: case 9: add(top, bottom); break;
                case 7: case 8: add(left, top); break;
                case 10: add(left, bottom); add(top, right); break;
                default: break;
                }
            }
        T.levels.push_back(std::move(lev));
    }
    T.ok = true;
}

/* ======================================================================== cities */
bool Engine::init(const std::string &data, const std::string &results) {
    data_dir = data;
    results_dir = results;
    cities.clear();
    for (const Reg &r : REG) {
        City c;
        c.name = r.name; c.label = r.label; c.blurb = r.blurb;
        cities.push_back(std::move(c));
    }
    plan = Plan();
    if (!load(0)) return false;
    plan.budget = cities[0].default_budget;
    return true;
}

bool Engine::load(int i) {
    City &c = cities[(size_t)i];
    if (c.loaded) return true;
    if (c.failed) return false;
    std::string dir = c.dir.empty() ? data_dir + "/" + c.name : c.dir;
    try {
        c.scenario = std::make_unique<ev::Scenario>(ev::load_scenario(dir));
        c.ctx = std::make_unique<ev::ScenarioContext>(*c.scenario);
    } catch (const std::exception &e) {
        c.failed = true;
        c.error = e.what();
        c.scenario.reset();
        c.ctx.reset();
        return false;
    }
    buildScene(c);
    c.default_budget = (int)std::lround(c.sc.cfg.budget);
    c.minx = c.miny = c.minz = 1e300;
    c.maxx = c.maxy = c.maxz = -1e300;
    for (const NodeV &n : c.sc.nodes) {
        c.minx = std::min(c.minx, n.x); c.maxx = std::max(c.maxx, n.x);
        c.miny = std::min(c.miny, n.y); c.maxy = std::max(c.maxy, n.y);
        c.minz = std::min(c.minz, n.elev); c.maxz = std::max(c.maxz, n.elev);
    }
    buildTerrain(c);
    c.loaded = true;
    return true;
}

void Engine::selectCity(int i) {
    if (!load(i)) { status = "!" + cities[(size_t)i].error; return; }
    plan.city = i;
    plan.budget = cities[(size_t)i].default_budget;
    plan.edits = Edits();
    if (plan.route == ROUTE_FLOYD && cities[(size_t)i].sc.nn > FLOYD_MAX_NODES) plan.route = ROUTE_DIJKSTRA;
    if (baseline.set && baseline.plan.city != i) clearBaseline();
    invalidate();
}

/* ======================================================================== runs */
ev::RunOptions Engine::options(const Plan &P) const {
    const City &c = cities[(size_t)P.city];
    const Scene &sc = c.sc;
    ev::RunOptions o;
    o.placement = toPlacement(P.sel);
    o.routing = toRouting(P.route);
    if (o.routing == ev::RoutingMethod::FloydWarshall && sc.nn > FLOYD_MAX_NODES) o.routing = ev::RoutingMethod::Dijkstra;
    o.budget = (ev::Paise)std::max(0, P.budget) * ev::kPaisePerLakh;
    for (int s : P.edits.lock)
        if (s >= sc.nexist && s < sc.ns) o.constraints.locked.push_back(sc.stations[(size_t)s].source);
    for (int s : P.edits.ban)
        if (s >= sc.nexist && s < sc.ns) o.constraints.banned.push_back(sc.stations[(size_t)s].source);
    for (auto &kv : P.edits.ports)
        if (kv.first >= 0 && kv.first < sc.ns) o.ports[sc.stations[(size_t)kv.first].id] = kv.second;
    return o;
}

static int stationOfSite(const Scene &sc, const ev::ChargerSite &s) {
    return s.kind == ev::ChargerSite::Kind::Existing ? s.source : sc.nexist + s.source;
}

static PlanV planView(const Scene &sc, const ev::PlanningRun &run, int t) {
    const ev::TripPlan &p = run.plans[(size_t)t];
    PlanV v;
    v.trip = t;
    v.feasible = p.feasible();
    v.needs_charge = p.needs_charge;
    v.status = p.status;
    v.status_name = ev::to_string(p.status);
    v.explanation = p.explanation;
    v.km = p.distance_km;
    v.kwh = p.energy_kwh;
    v.direct_km = p.shortest_km;
    v.depart_min = p.depart_min;
    v.arrive_min = p.arrive_min;
    v.charge_min = p.charge_min;
    v.start_kwh = p.start_kwh;
    v.arrive_kwh = p.arrive_kwh;
    v.min_kwh = p.min_kwh;
    if (!v.feasible) { v.depart_min = sc.trips[(size_t)t].depart_min; v.start_kwh = sc.trips[(size_t)t].start_kwh; }
    for (const ev::ChargingStop &s : p.stops) {
        v.stop.push_back(stationOfSite(sc, run.sites[(size_t)s.site]));
        v.stop_arrive_min.push_back(s.arrive_min);
        v.stop_charge_min.push_back(s.charge_min);
        v.stop_arrive_kwh.push_back(s.arrive_kwh);
        v.stop_depart_kwh.push_back(s.depart_kwh);
    }
    v.n_stops = (int)v.stop.size();
    return v;
}

void Engine::compute(Run &R, const Plan &P) {
    City &c = cities[(size_t)P.city];
    const Scene &sc = c.sc;
    R.plan = P;
    double t0 = nowMs();
    R.run.pr = std::make_unique<ev::PlanningRun>(*c.ctx, options(P));
    ev::PlanningRun &pr = *R.run.pr;
    RunV &rv = R.run;
    rv.sel_mode = P.sel;
    rv.route_mode = static_cast<int>(pr.options().routing);
    rv.sel.mode = P.sel;
    for (int k : pr.placement.chosen) rv.sel.idx.push_back(sc.nexist + k);
    rv.sel.n = (int)rv.sel.idx.size();
    rv.sel.value = (double)pr.placement.benefit;
    rv.sel.cost = toLakh(pr.placement.cost);
    rv.sel.ms = pr.placement.ms;
    rv.sel.search_nodes = (long)pr.placement.search_nodes;
    rv.sel.optimal = pr.placement.optimal;
    rv.sel.over_budget = pr.placement.over_budget;
    rv.sel.note = pr.placement.note;
    for (const ev::ChargerSite &s : pr.sites) rv.sites.push_back(stationOfSite(sc, s));
    rv.plans.reserve((size_t)sc.nt);
    for (int t = 0; t < sc.nt; t++) rv.plans.push_back(planView(sc, pr, t));
    rv.kpi = pr.kpi;
    rv.ms_values = c.ctx->benefit_ms;
    rv.ms_select = pr.ms_place;
    rv.ms_router = pr.ms_router;
    rv.ms_plan = pr.ms_plan;

    ev::CapacityResult cap = ev::analyze_capacity(pr);
    R.cap.trips_needing = cap.trips_needing;
    R.cap.slots_total = (long)cap.slots_total;
    R.cap.flow_dfs = (long)cap.dfs.value;
    R.cap.flow_bfs = (long)cap.bfs.value;
    R.cap.aug_dfs = (long)cap.dfs.augmentations;
    R.cap.aug_bfs = (long)cap.bfs.augmentations;
    R.cap.min_cut = (long)cap.min_cut;
    R.cap.matching = cap.matching;
    R.cap.ms_dfs = cap.ms_dfs; R.cap.ms_bfs = cap.ms_bfs; R.cap.ms_match = cap.ms_match;
    R.cap.bottleneck_station = cap.bottleneck_site >= 0 ? rv.sites[(size_t)cap.bottleneck_site] : -1;
    R.cap.assigned = cap.assigned;
    R.cap.consistent = cap.consistent();

    ev::SessionAnalysis ses = ev::analyze_sessions(pr);
    R.ses.n = (int)ses.sites.size();
    for (const ev::SiteLoad &L : ses.sites)
        R.ses.st.push_back({ rv.sites[(size_t)L.site], L.sessions, L.chargers_needed, L.depth, L.served });
    R.ses.overloaded = ses.overloaded;
    R.ses.sessions = ses.sessions;
    R.ses.served = ses.served;
    R.ses.hourly_kwh = ses.hourly_kwh;
    R.ses.surge_lo = ses.surge_lo;
    R.ses.surge_hi = ses.surge_hi;
    R.ses.surge_excess_kwh = ses.surge_excess_kwh;
    rv.sssp = (long)pr.router().trees_built();
    R.total_ms = nowMs() - t0;
    R.valid = true;

    R.ports.resize((size_t)sc.ns);
    for (int i = 0; i < sc.ns; i++) {
        auto it = P.edits.ports.find(i);
        R.ports[(size_t)i] = it == P.edits.ports.end() ? sc.stations[(size_t)i].ports : it->second;
    }
    R.built.assign((size_t)sc.ns, 0);
    for (int s : rv.sites) R.built[(size_t)s] = 1;
    R.short_of.assign((size_t)sc.ns, 0);
    for (const LoadV &L : R.ses.st) R.short_of[(size_t)L.station] = L.chargers_needed - R.ports[(size_t)L.station];

    // traffic: planned trips per road
    R.edge_load.assign((size_t)std::max(1, sc.ne), 0);
    const auto &arcs = c.ctx->model.arcs();
    for (const ev::TripPlan &p : pr.plans) {
        if (!p.feasible()) continue;
        for (int a : p.route_arcs) R.edge_load[(size_t)arcs[(size_t)a].edge]++;
    }
    R.edge_load_max = std::max(1, *std::max_element(R.edge_load.begin(), R.edge_load.end()));

    // sample trips for the inspector: multi-stop, one stop, direct, then not possible
    int want[4] = { 10, 10, 4, 6 }, got[4] = { 0, 0, 0, 0 };
    R.samples.clear();
    for (int pass = 0; pass < 4; pass++)
        for (int t = 0; t < sc.nt && got[pass] < want[pass]; t++) {
            const PlanV &p = rv.plans[(size_t)t];
            int kind = !p.feasible ? -1 : p.n_stops >= 2 ? 2 : p.n_stops == 1 ? 1 : 0;
            int target = pass == 0 ? 2 : pass == 1 ? 1 : pass == 2 ? 0 : -1;
            if (kind != target) continue;
            if (kind == -1 && !p.needs_charge) continue;
            if (kind == 0 && p.km <= 0) continue;   // skip zero-length trips
            got[pass]++;
            R.samples.push_back({ t, kind, p.feasible ? p.km : p.direct_km, p.n_stops });
        }
}

Run *Engine::run() {
    if (run_ && run_->plan == plan) return run_.get();
    if (!load(plan.city)) { status = "!" + city().error; return nullptr; }
    sims_[0].reset();
    sims_[1].reset();
    trip_ = TripDetail();
    run_.reset(new Run());
    try {
        compute(*run_, plan);
    } catch (const std::exception &e) {
        run_.reset();
        status = std::string("!") + e.what();
        return nullptr;
    }
    char buf[200];
    std::snprintf(buf, sizeof buf, "Solved in %.2f ms: site selection %.3f ms, routing and planning %.2f ms.",
                  run_->total_ms, run_->run.ms_select, run_->run.ms_router + run_->run.ms_plan);
    status = buf;
    return run_.get();
}

/* ======================================================================== simulation */
void Engine::simulate(Run &R, int policy, SimV &o) {
    const Scene &sc = city().sc;
    ev::SimOptions opt;
    opt.policy = policy ? ev::DriverPolicy::Divert : ev::DriverPolicy::Wait;
    opt.divert_after_min = 20;
    ev::SimResult s = ev::simulate_day(*R.run.pr, opt);
    o.nv = (int)s.trip.size();
    o.trip = s.trip;
    o.battery = s.battery;
    o.path_off = s.path_off;
    o.path = s.path;
    o.seg_off = s.seg_off;
    o.seg.reserve(s.seg.size());
    for (const ev::SimSegment &g : s.seg) {
        SegV v;
        v.vehicle = g.vehicle; v.kind = static_cast<int>(g.kind); v.t0 = g.t0; v.t1 = g.t1; v.p0 = g.p0; v.p1 = g.p1;
        v.station = g.site >= 0 ? R.run.sites[(size_t)g.site] : -1;
        v.e0 = g.e0; v.e1 = g.e1;
        o.seg.push_back(v);
    }
    o.nseg = (int)o.seg.size();
    for (const ev::SimSite &x : s.sites)
        o.st.push_back({ R.run.sites[(size_t)x.site], x.sessions, x.waited, x.wait_min, x.max_queue, x.diverted_away });
    o.nst = (int)o.st.size();
    o.events = s.events;
    o.diverted = s.diverted;
    o.delayed = s.delayed;
    o.total_wait = s.total_wait;
    o.max_wait = s.max_wait;
    o.mean_extra_min = s.mean_extra_min;
    o.end_min = s.end_min;
    o.ms = s.ms;
    (void)sc;
}

SimView *Engine::sim(int policy) {
    Run *R = run();
    if (!R) return nullptr;
    policy = policy ? 1 : 0;
    if (sims_[policy]) return sims_[policy].get();
    std::unique_ptr<SimView> S(new SimView());
    simulate(*R, policy, S->sim);
    S->valid = true;
    S->policy = policy;
    S->build(&city().sc);
    sims_[policy] = std::move(S);
    return sims_[policy].get();
}

void SimView::build(const Scene *s) {
    sc = s;
    int nv = sim.nv;
    cum.assign(sim.path.size(), 0.f);
    for (int v = 0; v < nv; v++)
        for (int i = sim.path_off[(size_t)v] + 1; i < sim.path_off[(size_t)v + 1]; i++) {
            const NodeV &a = sc->nodes[(size_t)sim.path[(size_t)i - 1]], &b = sc->nodes[(size_t)sim.path[(size_t)i]];
            cum[(size_t)i] = cum[(size_t)i - 1] + (float)std::hypot(b.x - a.x, b.y - a.y);
        }
    ends.assign((size_t)nv, 0.0);
    for (const SegV &g : sim.seg) ends[(size_t)g.vehicle] = std::max(ends[(size_t)g.vehicle], g.t1);
    std::sort(ends.begin(), ends.end());
    local.clear();
    for (int k = 0; k < sim.nst; k++) local[sim.st[(size_t)k].station] = k;
    end_min = std::max(1440.0, std::ceil(sim.end_min / 60) * 60);
    series_t.clear();
    series_kw.clear();
    LiveState st;
    for (double T = 0; T <= end_min; T += 10) {
        at(T, true, st);
        series_t.push_back(T / 60.0);
        series_kw.push_back(st.kw);
    }
}

void SimView::at(double T, bool light, LiveState &o) const {
    int K = sim.nst;
    o.charging.assign((size_t)K, 0);
    o.waiting.assign((size_t)K, 0);
    o.road = o.ncars = 0;
    o.kw = o.kwh = 0;
    if (!light) o.cars.resize((size_t)sim.nv * 3);
    for (const SegV &g : sim.seg) {
        if (g.kind == SEG_CHARGE && g.t0 < T)
            o.kwh += (g.e1 - g.e0) * std::min(1.0, (T - g.t0) / std::max(1e-9, g.t1 - g.t0));
        if (T < g.t0 || T >= g.t1) continue;
        if (g.kind == SEG_DRIVE) {
            o.road++;
            if (light) continue;
            int base = sim.path_off[(size_t)g.vehicle];
            double u = (T - g.t0) / std::max(1e-9, g.t1 - g.t0);
            int a = base + g.p0, b = base + g.p1;
            double target = cum[(size_t)a] + u * (cum[(size_t)b] - cum[(size_t)a]);
            int lo = a, hi = b;
            while (hi - lo > 1) { int m = (lo + hi) >> 1; if (cum[(size_t)m] <= target) lo = m; else hi = m; }
            int qi = std::min(hi, b);
            const NodeV &p = sc->nodes[(size_t)sim.path[(size_t)lo]], &q = sc->nodes[(size_t)sim.path[(size_t)qi]];
            double seglen = cum[(size_t)qi] - cum[(size_t)lo];
            double wv = seglen > 0 ? (target - cum[(size_t)lo]) / seglen : 0;
            double e = g.e0 + u * (g.e1 - g.e0);
            double B = sim.battery[(size_t)g.vehicle];
            o.cars[(size_t)o.ncars * 3] = (float)(p.x + (q.x - p.x) * wv);
            o.cars[(size_t)o.ncars * 3 + 1] = (float)(p.y + (q.y - p.y) * wv);
            o.cars[(size_t)o.ncars * 3 + 2] = (float)(B > 0 ? e / B : 0);
            o.ncars++;
        } else {
            auto it = local.find(g.station);
            if (it == local.end()) continue;
            if (g.kind == SEG_WAIT) o.waiting[(size_t)it->second]++;
            else {
                o.charging[(size_t)it->second]++;
                o.kw += (g.e1 - g.e0) / std::max(1e-9, (g.t1 - g.t0) / 60);
            }
        }
    }
    o.arrived = (int)(std::upper_bound(ends.begin(), ends.end(), T) - ends.begin());
    o.charging_total = o.waiting_total = 0;
    for (int k = 0; k < K; k++) { o.charging_total += o.charging[(size_t)k]; o.waiting_total += o.waiting[(size_t)k]; }
}

/* ======================================================================== trips */
// Replays a trip arc by arc for the battery chart: the planned route with its charging stops, or,
// for a trip with no plan, the plain shortest road route (showing where the battery runs out).
const TripDetail &Engine::trip(int t) {
    Run *R = run();
    if (!R || t < 0 || t >= city().sc.nt) { trip_ = TripDetail(); return trip_; }
    if (trip_.valid && trip_.trip == t) return trip_;
    City &c = city();
    const Scene &sc = c.sc;
    const ev::Scenario &s = *c.scenario;
    const ev::EnergyModel &model = c.ctx->model;
    ev::PlanningRun &pr = *R->run.pr;
    const ev::TripPlan &p = pr.plans[(size_t)t];
    const TripV &tr = sc.trips[(size_t)t];
    const ev::ElectricVehicle &veh = s.vehicles[(size_t)tr.vehicle];
    TripDetail d;
    d.trip = t;
    d.plan = R->run.plans[(size_t)t];
    std::vector<int> arcs;
    if (p.feasible()) arcs = p.route_arcs;
    else pr.router().leg(tr.src, tr.dst, arcs);
    d.path.push_back(tr.src);
    double km = 0, e = tr.start_kwh;
    d.profile.push_back({ 0, e, sc.nodes[(size_t)tr.src].elev });
    size_t next_stop = 0;
    for (size_t i = 0; i < arcs.size(); i++) {
        const ev::RoadArc &a = model.arcs()[(size_t)arcs[i]];
        km += a.km;
        e = std::min(veh.battery_kwh, e - model.energy(arcs[i], veh));
        d.path.push_back(a.to);
        d.profile.push_back({ km, e, sc.nodes[(size_t)a.to].elev });
        if (p.feasible() && next_stop < p.stops.size() && p.stops[next_stop].route_pos == i + 1) {
            e = p.stops[next_stop].depart_kwh;
            d.profile.push_back({ km, e, sc.nodes[(size_t)a.to].elev });
            next_stop++;
        }
    }
    d.valid = true;
    trip_ = d;
    return trip_;
}

/* ======================================================================== typed-in trips */
const char *const PLANNER_LABEL[PLANNER_COUNT] = { "Exact: battery-state search", "Fast: charging legs, Dijkstra", "Fast: charging legs, Floyd\u2013Warshall",
                                                   "Fast: charging legs, Bellman\u2013Ford" };
const char *const PLANNER_HINT[PLANNER_COUNT] = {
    "Dijkstra over (junction, battery level) states. Finds the shortest route that keeps the battery above the reserve, "
    "with the fewest stops among equals. Slower, but it does not miss routes (battery measured in steps of the scenario's battery_step_kwh).",
    "Dijkstra between the trip ends and the charger sites, each leg along the km-shortest road path (or the energy-minimal one if that "
    "cannot be driven). Fast; may miss a trip the exact search finds.",
    "Same charging-leg planner with legs from a Floyd\u2013Warshall all-pairs table (offered up to 1,600 junctions).",
    "Same charging-leg planner with energy-minimal legs from Bellman\u2013Ford (handles the negative energy of braking downhill)." };

// Battery level after every road of a route (charging stops lift it to their departure charge).
static void replayRoute(const ev::EnergyModel &model, const std::vector<int> &arcs, const ev::TripPlan *plan, int src, double start_kwh,
                        const ev::ElectricVehicle &v, const Scene &sc, std::vector<int> &path, std::vector<std::array<double, 3>> &profile) {
    path.assign(1, src);
    profile.clear();
    double km = 0, e = start_kwh;
    profile.push_back({ 0, e, sc.nodes[(size_t)src].elev });
    size_t next_stop = 0;
    for (size_t i = 0; i < arcs.size(); i++) {
        const ev::RoadArc &a = model.arcs()[(size_t)arcs[i]];
        km += a.km;
        e = std::min(v.battery_kwh, e - model.energy(arcs[i], v));
        path.push_back(a.to);
        profile.push_back({ km, e, sc.nodes[(size_t)a.to].elev });
        if (plan && next_stop < plan->stops.size() && plan->stops[next_stop].route_pos == i + 1) {
            e = plan->stops[next_stop].depart_kwh;
            profile.push_back({ km, e, sc.nodes[(size_t)a.to].elev });
            next_stop++;
        }
    }
}

TripAnswer Engine::planTrip(const TripQuery &q) {
    TripAnswer a;
    a.done = true;
    a.query = q;
    a.city = plan.city;
    Run *R = run();
    if (!R) { a.error = "The current plan could not be computed: " + status; return a; }
    City &c = city();
    const Scene &sc = c.sc;
    if (q.src < 0 || q.src >= sc.nn) { a.error = "Choose a source junction that exists in this network."; return a; }
    if (q.dst < 0 || q.dst >= sc.nn) { a.error = "Choose a destination junction that exists in this network."; return a; }
    try {
        ev::validate(q.vehicle);
    } catch (const std::exception &e) {
        a.error = std::string("Vehicle: ") + e.what() + ".";
        return a;
    }
    if (!(q.start_kwh >= 0) || q.start_kwh > q.vehicle.battery_kwh + 1e-9) { a.error = "The initial charge must be between 0 and the battery capacity."; return a; }
    if (q.method == PLANNER_LEG_FLOYD && sc.nn > FLOYD_MAX_NODES) {
        a.error = "Floyd\u2013Warshall is not offered above " + fint(FLOYD_MAX_NODES) + " junctions (its table needs V\u00B2 memory and V\u00B3 time).";
        return a;
    }
    ev::PlanningRun &pr = *R->run.pr;
    const ev::EnergyModel &model = c.ctx->model;
    double t0 = nowMs();
    try {
        if (q.method == PLANNER_EXACT) {
            ev::ExactPlanner ex(*c.scenario, model, pr.sites);
            a.plan = ex.plan(q.src, q.dst, q.vehicle, q.start_kwh, q.depart_min);
            a.states = ex.last_stats().states_settled;
        } else {
            ev::RoutingMethod m = q.method == PLANNER_LEG_FLOYD ? ev::RoutingMethod::FloydWarshall
                                : q.method == PLANNER_LEG_BELLMAN ? ev::RoutingMethod::BellmanFord : ev::RoutingMethod::Dijkstra;
            ev::Router router(model, m);
            ev::LegPlanner lp(*c.scenario, router, pr.sites);
            a.plan = lp.plan(q.src, q.dst, q.vehicle, q.start_kwh, q.depart_min);
        }
    } catch (const std::exception &e) {
        a.error = std::string("The planner rejected the request: ") + e.what();
        return a;
    }
    a.ms = nowMs() - t0;
    for (const ev::ChargingStop &st : a.plan.stops) a.stop_station.push_back(stationOfSite(sc, pr.sites[(size_t)st.site]));
    std::vector<int> arcs;
    if (a.plan.feasible()) arcs = a.plan.route_arcs;
    else pr.router().leg(q.src, q.dst, arcs);   // show the plain road route the battery cannot cover
    replayRoute(model, arcs, a.plan.feasible() ? &a.plan : nullptr, q.src, q.start_kwh, q.vehicle, sc, a.path, a.profile);
    return a;
}

ev::PlacementResult Engine::placementWith(int sel) {
    City &c = city();
    ev::RunOptions o = options(plan);
    return ev::place_stations(*c.scenario, c.ctx->benefits.benefit, toPlacement(sel), o.constraints, o.budget);
}

/* ======================================================================== data management */
DatasetCheck checkDataset(const std::string &dir) {
    DatasetCheck d;
    d.dir = dir;
    static const char *FILES[] = { "scenario.cfg", "nodes.csv", "roads.csv", "stations.csv", "candidates.csv", "vehicles.csv", "trips.csv", "sessions.csv", "events.txt" };
    for (const char *f : FILES) {
        FILE *h = std::fopen((dir + "/" + f).c_str(), "rb");
        d.files.push_back({ f, h != nullptr });
        if (h) std::fclose(h);
    }
    try {
        ev::Scenario s = ev::load_scenario(dir);
        d.ok = true;
        d.name = s.config.name;
        d.source = ev::to_string(s.config.source);
        d.source_note = s.config.source_note;
        d.nodes = s.network.node_count();
        d.roads = s.network.edge_count();
        d.stations = (int)s.stations.size();
        for (const ev::ChargingStation &st : s.stations) d.offline += st.status != ev::StationStatus::Available;
        d.candidates = (int)s.candidates.size();
        d.vehicles = (int)s.vehicles.size();
        d.trips = (int)s.trips.size();
        d.sessions = (int)s.sessions.size();
        d.components = s.network.components();
        d.budget_lakh = toLakh(s.config.budget);
    } catch (const std::exception &e) {
        d.ok = false;
        d.error = e.what();
    }
    return d;
}

int Engine::addCity(const std::string &dir, const std::string &label, std::string &error) {
    for (size_t i = 0; i < cities.size(); i++)
        if (cities[i].dir == dir) return (int)i;   // already loaded
    City c;
    c.name = label;
    c.label = label;
    c.dir = dir;
    c.blurb = "Loaded from " + dir;
    cities.push_back(std::move(c));
    int i = (int)cities.size() - 1;
    if (!load(i)) {
        error = cities[(size_t)i].error;
        cities.pop_back();
        return -1;
    }
    cities[(size_t)i].default_budget = (int)std::lround(cities[(size_t)i].sc.cfg.budget);
    return i;
}

/* ======================================================================== edits */
int Engine::basePorts(int s) const { return cities[(size_t)plan.city].sc.stations[(size_t)s].ports; }
int Engine::effectivePorts(int s) const {
    auto it = plan.edits.ports.find(s);
    return it == plan.edits.ports.end() ? basePorts(s) : it->second;
}
void Engine::setBuild(int s, int mode) {
    if (s < cities[(size_t)plan.city].sc.nexist) return;   // existing stations are not part of the decision
    plan.edits.lock.erase(s);
    plan.edits.ban.erase(s);
    if (mode == 1) plan.edits.lock.insert(s);
    else if (mode == 2) plan.edits.ban.insert(s);
}
void Engine::setPorts(int s, int n) {
    n = std::max(1, std::min(64, n));
    if (n == basePorts(s)) plan.edits.ports.erase(s);
    else plan.edits.ports[s] = n;
}
std::string Engine::editKey() const {
    const Scene &sc = cities[(size_t)plan.city].sc;
    std::vector<std::string> t;
    for (int i : plan.edits.lock) t.push_back("l" + std::to_string(sc.stations[(size_t)i].id));
    for (int i : plan.edits.ban) t.push_back("b" + std::to_string(sc.stations[(size_t)i].id));
    for (auto &kv : plan.edits.ports) t.push_back("p" + std::to_string(sc.stations[(size_t)kv.first].id) + "=" + std::to_string(kv.second));
    std::sort(t.begin(), t.end());
    std::string out;
    for (auto &s : t) { if (!out.empty()) out += ","; out += s; }
    return out;
}

std::string Engine::planLabel(const Plan &p) const {
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s, %s, \xE2\x82\xB9%d lakh", SELECT_LABEL[p.sel], ROUTE_LABEL[p.route], p.budget);
    std::string s = buf;
    if (!p.edits.empty()) s += ", " + std::to_string(p.edits.count()) + (p.edits.count() > 1 ? " edits" : " edit");
    return s;
}

/* ======================================================================== baseline */
void Engine::pinBaseline() {
    Run *R = run();
    if (!R) return;
    baseline = Baseline();
    baseline.set = true;
    baseline.plan = plan;
    baseline.kpi = R->run.kpi;
    baseline.cost = R->run.sel.cost;
    baseline.stations = R->run.sel.n;
    baseline.label = planLabel(plan);
}

double Engine::baselineWait(int policy) {
    if (!baseline.set) return -1;
    policy = policy ? 1 : 0;
    if (baseline.sim_wait[policy] < 0) {
        if (!load(baseline.plan.city)) return -1;
        Run R;
        compute(R, baseline.plan);
        SimV s;
        int keep = plan.city;
        plan.city = baseline.plan.city;   // simulate() reads the current city
        simulate(R, policy, s);
        plan.city = keep;
        baseline.sim_wait[policy] = s.total_wait;
    }
    return baseline.sim_wait[policy];
}

KpiV Engine::kpiFor(const Plan &p, double *cost, int *stations) {
    if (!load(p.city)) return KpiV{};
    Run R;
    compute(R, p);
    if (cost) *cost = R.run.sel.cost;
    if (stations) *stations = R.run.sel.n;
    return R.run.kpi;
}

/* ======================================================================== comparison */
Compare &Engine::compare(int c) {
    Compare &C = compares_[c];
    C.city = c;
    return C;
}

void Engine::stepCompare(Compare &C) {
    if (C.done || !load(C.city)) { C.done = true; return; }
    City &city = cities[(size_t)C.city];
    // display order: DP, B&B LC, FIFO, LIFO, greedy  x  Dijkstra, Floyd, Bellman
    static const int ORDER[SEL_COUNT] = { SEL_DP, SEL_BNB_LC, SEL_BNB_FIFO, SEL_BNB_LIFO, SEL_GREEDY };
    while (C.next < SEL_COUNT * ROUTE_COUNT) {
        int sm = ORDER[C.next / ROUTE_COUNT], rm = C.next % ROUTE_COUNT;
        C.next++;
        if (rm == ROUTE_FLOYD && city.sc.nn > FLOYD_MAX_NODES) continue;   // V^3 table too large
        ev::RunOptions o;
        o.placement = toPlacement(sm);
        o.routing = toRouting(rm);
        ev::PlanningRun r(*city.ctx, o);
        CompareRow row{};
        row.sel = sm; row.route = rm; row.stations = (int)r.placement.chosen.size();
        row.cost = toLakh(r.placement.cost); row.value = (double)r.placement.benefit;
        row.optimal = r.placement.optimal; row.select_ms = r.ms_place; row.search_nodes = (long)r.placement.search_nodes;
        row.feasible = r.kpi.feasible; row.feasible_pct = r.kpi.feasible_pct; row.avg_km = r.kpi.avg_km;
        row.avg_kwh = r.kpi.avg_kwh; row.route_ms = r.ms_router + r.ms_plan; row.sssp = (long)r.router().trees_built();
        C.rows.push_back(row);
        return;   // one strategy per frame keeps the window responsive
    }
    C.done = true;
}

/* ======================================================================== analysis */
Analysis &Engine::analysis(int c) { return analyses_[c]; }

void Engine::computeAnalysis(int c) {
    Analysis &A = analyses_[c];
    if (A.done || !load(c)) { A.done = true; return; }
    std::ostringstream text;
    ev::JsonWriter js;
    try {
        ev::analyze(*cities[(size_t)c].ctx, text, &js);
        A.text = text.str();
        A.data = Json::parse(js.str());
    } catch (const std::exception &e) {
        A.text = std::string("The analysis stopped: ") + e.what();
    }
    A.done = true;
}

/* ======================================================================== benchmarks */
const std::vector<BenchRow> &Engine::bench() {
    if (bench_loaded_) return bench_;
    bench_loaded_ = true;
    bench_.clear();
    bench_error.clear();
    if (bench_path.empty()) bench_path = results_dir + "/results.csv";
    std::optional<std::string> text = ev::read_text_file(bench_path);
    if (!text) { bench_error = "No benchmark results at " + bench_path + ". Run ev33_bench to produce them."; return bench_; }
    std::istringstream in(*text);
    std::string line;
    std::vector<std::string> head, f;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (head.empty()) { ev::split_csv_line(line, head); continue; }
        if (!ev::split_csv_line(line, f)) continue;
        auto get = [&](const char *k) -> std::string {
            for (size_t i = 0; i < head.size() && i < f.size(); i++) if (head[i] == k) return f[i];
            return "";
        };
        BenchRow r;
        r.alg = get("algorithm"); r.family = get("family"); r.dataset = get("dataset"); r.variant = get("variant");
        r.validation = get("validation"); r.note = get("note"); r.compiler = get("compiler"); r.build = get("build_type");
        r.n = std::atof(get("input_size").c_str());
        r.ms = std::atof(get("median_ms").c_str());
        r.min_ms = std::atof(get("min_ms").c_str());
        r.max_ms = std::atof(get("max_ms").c_str());
        r.peak = std::atol(get("peak_bytes").c_str());
        r.vertices = std::atof(get("vertices").c_str());
        r.edges = std::atof(get("edges").c_str());
        r.reps = std::atoi(get("repetitions").c_str());
        r.pass = r.validation == "pass";
        if (!r.alg.empty()) bench_.push_back(r);
    }
    if (bench_.empty()) bench_error = "The benchmark file " + bench_path + " has no rows.";
    return bench_;
}

void Engine::reloadBench(const std::string &path) {
    if (!path.empty()) bench_path = path;
    bench_loaded_ = false;
}

/* ======================================================================== export */
std::string Engine::sitesCsv() {
    Run *R = run();
    if (!R) return "";
    const Scene &sc = city().sc;
    std::string out = "station_id,name,kind,junction,ports,power_kw,cost_lakh,benefit,sessions,chargers_needed\n";
    for (const LoadV &L : R->ses.st) {
        const SiteV &s = sc.stations[(size_t)L.station];
        char buf[512];
        std::snprintf(buf, sizeof buf, "%d,%s,%s,%s,%d,%g,%g,%ld,%d,%d\n", s.id, ev::csv_escape(s.name).c_str(),
                      s.existing ? "existing" : "new", ev::csv_escape(sc.nodes[(size_t)s.node].name).c_str(),
                      R->ports[(size_t)L.station], s.power_kw, s.cost, s.value, L.sessions, L.chargers_needed);
        out += buf;
    }
    return out;
}

} // namespace ev33
