// engine.cpp - see engine.h.
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ev33 {

const char *const SELECT_LABEL[EV_SEL_COUNT] = { "Greedy ratio", "Exact DP", "B&B least cost", "B&B FIFO", "B&B LIFO" };
const char *const SELECT_LONG[EV_SEL_COUNT] = {
    "Greedy: value per rupee", "Exact: 0/1 knapsack DP", "Exact: branch and bound, least cost",
    "Exact: branch and bound, FIFO", "Exact: branch and bound, LIFO" };
const char *const ROUTE_LABEL[EV_ROUTE_COUNT] = { "Dijkstra", "Floyd\xE2\x80\x93Warshall", "Bellman\xE2\x80\x93" "Ford" };
const char *const ROUTE_HINT[EV_ROUTE_COUNT] = {
    "Shortest distance. One cached tree per trip origin.",
    "Shortest distance, all pairs computed up front in \xCE\x98(V\xC2\xB3).",
    "Least energy. Handles the negative energy of braking downhill." };

struct Reg { const char *name, *label, *blurb; };
static const Reg REG[] = {
    { "medium", "Normal operation", "A 256-junction city with 60 candidate sites and 1,200 daily trips." },
    { "shock", "Constraint shock", "Budget cut 45%, a third of sites down to one port, two river bridges left, evening demand up 60%." },
    { "large", "Large-data stress", "1,024 junctions, 220 candidate sites and 6,000 trips." },
    { "adversarial", "Long corridor", "A 600 km two-lane corridor with identical site costs: a stress test for the selection model." },
    { "small", "Small town", "36 junctions and 60 trips, small enough to check by hand." },
};

/* ======================================================================== lifetimes */
Run::~Run() {
    if (!valid) return;
    ev_capacity_free(&cap);
    ev_sessions_free(&ses);
    ev_run_free(&run);
}

SimView::~SimView() {
    if (valid) ev_sim_free(&sim);
}

/* ======================================================================== terrain (IDW grid + marching squares) */
static void buildTerrain(City &c) {
    const ev_scenario &sc = c.sc;
    Terrain &T = c.terrain;
    double w = c.maxx - c.minx, h = c.maxy - c.miny;
    if (w <= 0 || h <= 0 || w / h > 8 || h / w > 8) return;
    int gw = 120, gh = std::max(8, std::min(120, (int)std::lround(gw * h / w)));
    double avg = 0;
    for (int i = 0; i < sc.ne; i++) avg += sc.edges[i].km;
    avg /= std::max(1, sc.ne);
    double R = std::max(avg * 1.6, w / 40), bs = R;
    int bw = (int)std::ceil(w / bs) + 1, bh = (int)std::ceil(h / bs) + 1;
    std::vector<std::vector<int>> buckets((size_t)(bw * bh));
    for (int i = 0; i < sc.nn; i++) {
        int bx = std::max(0, std::min(bw - 1, (int)std::floor((sc.nodes[i].x - c.minx) / bs)));
        int by = std::max(0, std::min(bh - 1, (int)std::floor((sc.nodes[i].y - c.miny) / bs)));
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
                        const ev_node &n = sc.nodes[i];
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
    char err[256] = "";
    std::string dir = data_dir + "/" + c.name;
    if (ev_scenario_load(dir.c_str(), &c.sc, err, sizeof err) != 0) {
        c.failed = true;
        c.error = err;
        return false;
    }
    ev_station_values(&c.sc);
    c.default_budget = c.sc.cfg.budget;
    c.minx = c.miny = c.minz = 1e300;
    c.maxx = c.maxy = c.maxz = -1e300;
    for (int k = 0; k < c.sc.nn; k++) {
        const ev_node &n = c.sc.nodes[k];
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
    if (plan.route == EV_ROUTE_FLOYD && cities[(size_t)i].sc.nn > 1600) plan.route = EV_ROUTE_DIJKSTRA;
    if (baseline.set && baseline.plan.city != i) clearBaseline();
    invalidate();
}

/* ======================================================================== runs */
void Engine::applyPorts(const Plan &P, std::vector<int> &saved) {
    ev_scenario &sc = cities[(size_t)P.city].sc;
    saved.clear();
    for (auto &kv : P.edits.ports) { saved.push_back(sc.stations[kv.first].ports); sc.stations[kv.first].ports = kv.second; }
}
void Engine::restorePorts(const Plan &P, const std::vector<int> &saved) {
    ev_scenario &sc = cities[(size_t)P.city].sc;
    size_t k = 0;
    for (auto &kv : P.edits.ports) sc.stations[kv.first].ports = saved[k++];
}

void Engine::compute(Run &R, const Plan &P) {
    City &c = cities[(size_t)P.city];
    ev_scenario *sc = &c.sc;
    R.plan = P;
    int saved_budget = sc->cfg.budget;
    sc->cfg.budget = P.budget;
    std::vector<int> saved;
    applyPorts(P, saved);
    std::vector<int> lock(P.edits.lock.begin(), P.edits.lock.end()), ban(P.edits.ban.begin(), P.edits.ban.end());
    ev_constraints con = { (int)lock.size(), (int)ban.size(), lock.data(), ban.data() };
    double t0 = ev_now_ms();
    ev_run_pipeline_ex(sc, (ev_sel_mode)P.sel, (ev_route_mode)P.route, &con, &R.run);
    ev_capacity_analyze(R.run.router, sc, &R.run.sel, R.run.plans, &R.cap);
    ev_sessions_analyze(sc, &R.run.sel, R.run.plans, &R.ses);
    R.total_ms = R.run.ms_values + R.run.ms_select + R.run.ms_router + R.run.ms_plan;
    (void)t0;
    R.valid = true;

    R.ports.resize((size_t)sc->ns);
    for (int i = 0; i < sc->ns; i++) R.ports[(size_t)i] = sc->stations[i].ports;
    R.built.assign((size_t)sc->ns, 0);
    for (int i = 0; i < R.run.sel.n; i++) R.built[(size_t)R.run.sel.idx[i]] = 1;
    R.short_of.assign((size_t)sc->ns, 0);
    for (int q = 0; q < R.ses.n; q++) {
        const ev_station_load &L = R.ses.st[q];
        R.short_of[(size_t)L.station] = L.chargers_needed - sc->stations[L.station].ports;
    }

    // traffic: planned trips per road
    R.edge_load.assign((size_t)std::max(1, sc->ne), 0);
    std::vector<int> pp((size_t)sc->nn * 4);
    for (int t = 0; t < sc->nt; t++) {
        if (!R.run.plans[t].feasible) continue;
        int len = ev_plan_path(R.run.router, sc, &R.run.plans[t], pp.data(), sc->nn * 4, nullptr);
        for (int i = 0; i + 1 < len; i++) {
            int a = ev_graph_find_arc(sc->g_km, pp[(size_t)i], pp[(size_t)i + 1]);
            if (a >= 0) R.edge_load[(size_t)(sc->g_km->arcs[a].id / 2)]++;
        }
    }
    R.edge_load_max = std::max(1, *std::max_element(R.edge_load.begin(), R.edge_load.end()));

    // sample trips for the inspector: multi-stop, one stop, direct, then not possible
    int want[4] = { 10, 10, 4, 6 }, got[4] = { 0, 0, 0, 0 };
    R.samples.clear();
    for (int pass = 0; pass < 4; pass++)
        for (int t = 0; t < sc->nt && got[pass] < want[pass]; t++) {
            const ev_plan *p = &R.run.plans[t];
            int kind = !p->feasible ? -1 : p->n_stops >= 2 ? 2 : p->n_stops == 1 ? 1 : 0;
            int target = pass == 0 ? 2 : pass == 1 ? 1 : pass == 2 ? 0 : -1;
            if (kind != target) continue;
            if (kind == -1 && !p->needs_charge) continue;
            got[pass]++;
            R.samples.push_back({ t, kind, p->feasible ? p->km : p->direct_km, p->n_stops });
        }

    restorePorts(P, saved);
    sc->cfg.budget = saved_budget;
}

Run *Engine::run() {
    if (run_ && run_->plan == plan) return run_.get();
    if (!load(plan.city)) { status = "!" + city().error; return nullptr; }
    sims_[0].reset();
    sims_[1].reset();
    trip_ = TripDetail();
    run_.reset(new Run());
    compute(*run_, plan);
    char buf[160];
    std::snprintf(buf, sizeof buf, "Solved in %.2f ms: site selection %.3f ms, routing and planning %.2f ms.",
                  run_->total_ms, run_->run.ms_select, run_->run.ms_router + run_->run.ms_plan);
    status = buf;
    return run_.get();
}

/* ======================================================================== simulation */
SimView *Engine::sim(int policy) {
    Run *R = run();
    if (!R) return nullptr;
    policy = policy ? 1 : 0;
    if (sims_[policy]) return sims_[policy].get();
    ev_scenario *sc = &city().sc;
    std::unique_ptr<SimView> S(new SimView());
    std::vector<int> saved;
    applyPorts(R->plan, saved);
    ev_sim_opts opt = { policy ? EV_POLICY_DIVERT : EV_POLICY_WAIT, 20.0 };
    ev_simulate_ex(R->run.router, sc, &R->run.sel, R->run.plans, &opt, &S->sim);
    restorePorts(R->plan, saved);
    S->valid = true;
    S->policy = policy;
    S->build(sc);
    sims_[policy] = std::move(S);
    return sims_[policy].get();
}

void SimView::build(const ev_scenario *s) {
    sc = s;
    int nv = sim.nv;
    cum.assign((size_t)sim.path_off[nv], 0.f);
    for (int v = 0; v < nv; v++)
        for (int i = sim.path_off[v] + 1; i < sim.path_off[v + 1]; i++) {
            const ev_node &a = sc->nodes[sim.path[i - 1]], &b = sc->nodes[sim.path[i]];
            cum[(size_t)i] = cum[(size_t)i - 1] + (float)std::hypot(b.x - a.x, b.y - a.y);
        }
    ends.assign((size_t)nv, 0.0);
    for (int i = 0; i < sim.nseg; i++) {
        const ev_sim_seg &g = sim.seg[i];
        ends[(size_t)g.vehicle] = std::max(ends[(size_t)g.vehicle], g.t1);
    }
    std::sort(ends.begin(), ends.end());
    local.clear();
    for (int k = 0; k < sim.nst; k++) local[sim.st[k].station] = k;
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
    double B = sc->cfg.battery_kwh;
    for (int i = 0; i < sim.nseg; i++) {
        const ev_sim_seg &g = sim.seg[i];
        if (g.kind == EV_SEG_CHARGE && g.t0 < T)
            o.kwh += (g.e1 - g.e0) * std::min(1.0, (T - g.t0) / std::max(1e-9, g.t1 - g.t0));
        if (T < g.t0 || T >= g.t1) continue;
        if (g.kind == EV_SEG_DRIVE) {
            o.road++;
            if (light) continue;
            int base = sim.path_off[g.vehicle];
            double u = (T - g.t0) / std::max(1e-9, g.t1 - g.t0);
            int a = base + g.p0, b = base + g.p1;
            double target = cum[(size_t)a] + u * (cum[(size_t)b] - cum[(size_t)a]);
            int lo = a, hi = b;
            while (hi - lo > 1) { int m = (lo + hi) >> 1; if (cum[(size_t)m] <= target) lo = m; else hi = m; }
            int qi = std::min(hi, b);
            const ev_node &p = sc->nodes[sim.path[lo]], &q = sc->nodes[sim.path[qi]];
            double seglen = cum[(size_t)qi] - cum[(size_t)lo];
            double wv = seglen > 0 ? (target - cum[(size_t)lo]) / seglen : 0;
            double e = g.e0 + u * (g.e1 - g.e0);
            o.cars[(size_t)o.ncars * 3] = (float)(p.x + (q.x - p.x) * wv);
            o.cars[(size_t)o.ncars * 3 + 1] = (float)(p.y + (q.y - p.y) * wv);
            o.cars[(size_t)o.ncars * 3 + 2] = (float)(e / B);
            o.ncars++;
        } else {
            auto it = local.find(g.station);
            if (it == local.end()) continue;
            if (g.kind == EV_SEG_WAIT) o.waiting[(size_t)it->second]++;
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
const TripDetail &Engine::trip(int t) {
    Run *R = run();
    if (!R || t < 0) { trip_ = TripDetail(); return trip_; }
    if (trip_.valid && trip_.trip == t) return trip_;
    const ev_scenario *sc = &city().sc;
    TripDetail d;
    d.trip = t;
    d.plan = R->run.plans[t];
    const ev_trip *tr = &sc->trips[t];
    const ev_config *c = &sc->cfg;
    d.path.resize((size_t)sc->nn * 4);
    int len = d.plan.feasible ? ev_plan_path(R->run.router, sc, &d.plan, d.path.data(), sc->nn * 4, nullptr)
                              : ev_router_path(R->run.router, tr->src, tr->dst, d.path.data(), sc->nn * 4);
    d.path.resize((size_t)std::max(0, len));
    double km = 0, e = tr->soc0 * c->battery_kwh;
    int next_stop = 0;
    d.profile.push_back({ 0, e, sc->nodes[tr->src].elev });
    for (int i = 0; i + 1 < len; i++) {
        int u = d.path[(size_t)i], v = d.path[(size_t)i + 1];
        int pa = ev_graph_find_arc(sc->g_km, u, v);
        if (pa < 0) break;
        int id = sc->g_km->arcs[pa].id;
        km += sc->arc_km[id];
        e -= sc->arc_kwh[id];
        d.profile.push_back({ km, e, sc->nodes[v].elev });
        if (d.plan.feasible && next_stop < d.plan.n_stops && v == sc->stations[d.plan.stop[next_stop]].node && i + 1 < len - 1) {
            if (e < c->target_frac * c->battery_kwh) e = c->target_frac * c->battery_kwh;
            d.profile.push_back({ km, e, sc->nodes[v].elev });
            next_stop++;
        }
    }
    d.valid = true;
    trip_ = d;
    return trip_;
}

/* ======================================================================== edits */
int Engine::basePorts(int s) const { return cities[(size_t)plan.city].sc.stations[s].ports; }
int Engine::effectivePorts(int s) const {
    auto it = plan.edits.ports.find(s);
    return it == plan.edits.ports.end() ? basePorts(s) : it->second;
}
void Engine::setBuild(int s, int mode) {
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
    const ev_scenario &sc = cities[(size_t)plan.city].sc;
    std::vector<std::string> t;
    for (int i : plan.edits.lock) t.push_back("l" + std::to_string(sc.stations[i].id));
    for (int i : plan.edits.ban) t.push_back("b" + std::to_string(sc.stations[i].id));
    for (auto &kv : plan.edits.ports) t.push_back("p" + std::to_string(sc.stations[kv.first].id) + "=" + std::to_string(kv.second));
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

double Engine::simWait(const Plan &P, int policy) {
    Run R;
    compute(R, P);
    ev_scenario *sc = &cities[(size_t)P.city].sc;
    std::vector<int> saved;
    applyPorts(P, saved);
    ev_sim s{};
    ev_sim_opts opt = { policy ? EV_POLICY_DIVERT : EV_POLICY_WAIT, 20.0 };
    ev_simulate_ex(R.run.router, sc, &R.run.sel, R.run.plans, &opt, &s);
    restorePorts(P, saved);
    double w = s.total_wait;
    ev_sim_free(&s);
    return w;
}

double Engine::baselineWait(int policy) {
    if (!baseline.set) return -1;
    policy = policy ? 1 : 0;
    if (baseline.sim_wait[policy] < 0) baseline.sim_wait[policy] = simWait(baseline.plan, policy);
    return baseline.sim_wait[policy];
}

ev_kpi Engine::kpiFor(const Plan &p, long *cost, int *stations) {
    if (!load(p.city)) return ev_kpi{};
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
    ev_scenario *sc = &cities[(size_t)C.city].sc;
    // display order: DP, B&B LC, FIFO, LIFO, greedy  x  Dijkstra, Floyd, Bellman
    static const int ORDER[EV_SEL_COUNT] = { EV_SEL_DP, EV_SEL_BNB_LC, EV_SEL_BNB_FIFO, EV_SEL_BNB_LIFO, EV_SEL_GREEDY };
    while (C.next < EV_SEL_COUNT * EV_ROUTE_COUNT) {
        int sm = ORDER[C.next / EV_ROUTE_COUNT], rm = C.next % EV_ROUTE_COUNT;
        C.next++;
        if (rm == EV_ROUTE_FLOYD && sc->nn > 1600) continue;   // V^3 table too large
        ev_run r;
        ev_run_pipeline(sc, (ev_sel_mode)sm, (ev_route_mode)rm, &r);
        CompareRow row{};
        row.sel = sm; row.route = rm; row.stations = r.sel.n; row.cost = r.sel.cost; row.value = r.sel.value;
        row.optimal = r.sel.optimal != 0; row.select_ms = r.ms_select; row.search_nodes = r.sel.search_nodes;
        row.feasible = r.kpi.feasible; row.feasible_pct = r.kpi.feasible_pct; row.avg_km = r.kpi.avg_km;
        row.avg_kwh = r.kpi.avg_kwh; row.route_ms = r.ms_router + r.ms_plan; row.sssp = ev_router_sssp_runs(r.router);
        C.rows.push_back(row);
        ev_run_free(&r);
        return;   // one strategy per frame keeps the window responsive
    }
    C.done = true;
}

/* ======================================================================== analysis */
static std::string slurp(FILE *f) {
    std::string s;
    std::rewind(f);
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    return s;
}

Analysis &Engine::analysis(int c) { return analyses_[c]; }

void Engine::computeAnalysis(int c) {
    Analysis &A = analyses_[c];
    if (A.done || !load(c)) { A.done = true; return; }
    FILE *txt = std::tmpfile(), *js = std::tmpfile();
    if (!txt || !js) {
        if (txt) std::fclose(txt);
        if (js) std::fclose(js);
        A.text = "Could not create temporary files for the report.";
        A.done = true;
        return;
    }
    ev_analyze_fp(&cities[(size_t)c].sc, txt, js);
    std::fflush(txt);
    std::fflush(js);
    A.text = slurp(txt);
    A.data = Json::parse(slurp(js));
    std::fclose(txt);
    std::fclose(js);
    A.done = true;
}

/* ======================================================================== benchmarks */
const std::vector<BenchRow> &Engine::bench() {
    if (bench_loaded_) return bench_;
    bench_loaded_ = true;
    std::ifstream in(results_dir + "/bench.csv");
    if (!in) { bench_error = "No benchmark results at " + results_dir + "/bench.csv. Run: make bench"; return bench_; }
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f;
        std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == ',' && f.size() < 8) { f.push_back(cur); cur.clear(); }
            else cur += line[i];
        }
        f.push_back(cur);
        if (f.size() < 9) continue;
        BenchRow r;
        r.family = f[0]; r.alg = f[1]; r.dataset = f[2];
        r.n = std::atof(f[3].c_str()); r.ms = std::atof(f[4].c_str()); r.peak = std::atol(f[5].c_str());
        r.quality = std::atof(f[6].c_str()); r.check = std::atoi(f[7].c_str()); r.note = f[8];
        bench_.push_back(r);
    }
    return bench_;
}

/* ======================================================================== export */
std::string Engine::sitesCsv() {
    Run *R = run();
    if (!R) return "";
    const ev_scenario &sc = city().sc;
    std::string out = "station_id,junction,ports,power_kw,cost_lakh,service_value,sessions,chargers_needed\n";
    for (int i = 0; i < R->run.sel.n; i++) {
        int s = R->run.sel.idx[i];
        int sessions = 0, need = 0;
        for (int q = 0; q < R->ses.n; q++)
            if (R->ses.st[q].station == s) { sessions = R->ses.st[q].sessions; need = R->ses.st[q].chargers_needed; }
        char buf[256];
        std::snprintf(buf, sizeof buf, "%d,%s,%d,%g,%d,%d,%d,%d\n", sc.stations[s].id, sc.nodes[sc.stations[s].node].name,
                      R->ports[(size_t)s], sc.stations[s].power_kw, sc.stations[s].cost, sc.stations[s].value, sessions, need);
        out += buf;
    }
    return out;
}

} // namespace ev33
