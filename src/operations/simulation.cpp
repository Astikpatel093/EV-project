// simulation.cpp - event-driven day with per-site FIFO queues and driver policies.
#include "ev/operations/simulation.hpp"

#include <deque>

#include "ev/algorithms/heap.hpp"
#include "ev/core/timer.hpp"

namespace ev {

namespace {

enum class EvType { Arrive, Done };

struct Event {
    double t;
    long seq;
    int v;        // simulated vehicle
    int site;
    EvType type;
    double e;     // battery on arrival / after charging
};
struct EarlierEvent {
    bool operator()(const Event &a, const Event &b) const { return a.t < b.t || (a.t == b.t && a.seq < b.seq); }
};

struct Queued { int v; double t, e; };

struct SiteState {
    int node = 0, ports = 0, busy = 0;
    double power = 0;
    std::vector<double> port_end;    // when each port frees
    std::deque<Queued> queue;
    double mean_session = 30;        // running mean, for wait estimates
    int sessions_seen = 0;
};

struct Vehicle {
    int plan = 0;
    int stop = 0;          // index of the planned stop being served or headed to
    bool switched = false;
    double waited = 0;
};

struct RawSeg { int v; SegmentKind kind; double t0, t1; int a, b, site; double e0, e1; };

class Day {
public:
    Day(PlanningRun &run, const SimOptions &opt, SimResult &out) : run_(run), sc_(run.scenario()), opt_(opt), out_(out) {}

    void run() {
        const std::size_t S = run_.sites.size();
        out_.sites.resize(S);
        sites_.resize(S);
        for (std::size_t j = 0; j < S; j++) {
            const ChargerSite &c = run_.sites[j];
            out_.sites[j].site = static_cast<int>(j);
            sites_[j].node = c.node;
            sites_[j].ports = c.available ? c.ports : 0;
            sites_[j].power = c.power_kw;
            sites_[j].port_end.assign(static_cast<std::size_t>(sites_[j].ports), 0.0);
        }
        for (std::size_t t = 0; t < run_.plans.size(); t++) {
            const TripPlan &p = run_.plans[t];
            if (!p.feasible()) continue;
            const int vi = static_cast<int>(veh_.size());
            out_.trip.push_back(static_cast<int>(t));
            out_.battery.push_back(vehicle(vi).battery_kwh);
            veh_.push_back({static_cast<int>(t), 0, false, 0});
            const int src = p.route.front(), dst = p.route.back();
            if (p.stops.empty()) drive(vi, src, dst, p.depart_min, p.start_kwh, -1);
            else drive(vi, src, sites_[static_cast<std::size_t>(p.stops[0].site)].node, p.depart_min, p.start_kwh, p.stops[0].site);
        }
        while (!heap_.empty()) {
            Event e = heap_.pop();
            out_.events++;
            SiteState &q = sites_[static_cast<std::size_t>(e.site)];
            if (e.type == EvType::Arrive) {
                if (q.busy < q.ports) { start_charge(e.v, e.site, e.t, e.t, e.e); continue; }
                if (try_divert(e.v, e.site, e.t, e.e)) continue;
                q.queue.push_back({e.v, e.t, e.e});
                int ql = static_cast<int>(q.queue.size());
                if (ql > out_.sites[static_cast<std::size_t>(e.site)].max_queue) out_.sites[static_cast<std::size_t>(e.site)].max_queue = ql;
            } else {
                out_.sites[static_cast<std::size_t>(e.site)].sessions++;
                q.busy--;
                Vehicle &ve = veh_[static_cast<std::size_t>(e.v)];
                const TripPlan &p = run_.plans[static_cast<std::size_t>(ve.plan)];
                ve.stop++;
                ve.switched = false;
                if (ve.stop < static_cast<int>(p.stops.size())) {
                    const int next_site = p.stops[static_cast<std::size_t>(ve.stop)].site;
                    drive(e.v, q.node, sites_[static_cast<std::size_t>(next_site)].node, e.t, e.e, next_site);
                } else {
                    drive(e.v, q.node, p.route.back(), e.t, e.e, -1);
                }
                if (!q.queue.empty()) {   // the freed port goes to the head of the queue
                    Queued w = q.queue.front();
                    q.queue.pop_front();
                    start_charge(w.v, e.site, w.t, e.t, w.e);
                }
            }
        }
        assemble();
    }

private:
    const ElectricVehicle &vehicle(int vi) const {
        const TripPlan &p = run_.plans[static_cast<std::size_t>(out_.trip[static_cast<std::size_t>(vi)])];
        return sc_.vehicles[static_cast<std::size_t>(sc_.vehicle_index(p.vehicle_id))];
    }

    // drive from node a to node b; schedule arrival at `site` (or nothing when heading to the destination)
    void drive(int vi, int a, int b, double t, double e, int site) {
        const ElectricVehicle &v = vehicle(vi);
        std::vector<int> arcs;
        double minutes = 0, e1 = e;
        if (run_.router().leg(a, b, arcs)) {
            EnergyModel::Trace tr = run_.router().model().simulate(arcs, e, v);
            minutes = tr.minutes;
            e1 = tr.end_kwh;
        }
        raw_.push_back({vi, SegmentKind::Drive, t, t + minutes, a, b, -1, e, e1});
        if (site >= 0) heap_.push({t + minutes, seq_++, vi, site, EvType::Arrive, e1});
    }

    double rate(int vi, int site) const {
        double r = sites_[static_cast<std::size_t>(site)].power;
        const ElectricVehicle &v = vehicle(vi);
        if (v.max_charge_kw > 0 && v.max_charge_kw < r) r = v.max_charge_kw;
        return r;
    }

    void start_charge(int vi, int site, double t_arr, double t, double e) {
        SiteState &q = sites_[static_cast<std::size_t>(site)];
        Vehicle &ve = veh_[static_cast<std::size_t>(vi)];
        if (t > t_arr + 1e-9) {
            raw_.push_back({vi, SegmentKind::Wait, t_arr, t, q.node, q.node, site, e, e});
            ve.waited += t - t_arr;
            out_.sites[static_cast<std::size_t>(site)].waited++;
            out_.sites[static_cast<std::size_t>(site)].wait_min += t - t_arr;
        }
        const double target = sc_.config.target_soc * vehicle(vi).battery_kwh;
        const double need = target - e;
        double d = need > 0 ? need / rate(vi, site) * 60.0 : 0.0;
        if (d < sc_.config.min_session_min) d = sc_.config.min_session_min;   // plug-in and hand-over
        const double after = e < target ? target : e;
        raw_.push_back({vi, SegmentKind::Charge, t, t + d, q.node, q.node, site, e, after});
        std::size_t port = 0;
        for (std::size_t k = 1; k < q.port_end.size(); k++)
            if (q.port_end[k] < q.port_end[port]) port = k;
        if (!q.port_end.empty()) q.port_end[port] = t + d;
        q.busy++;
        q.sessions_seen++;
        q.mean_session += (d - q.mean_session) / q.sessions_seen;
        heap_.push({t + d, seq_++, vi, site, EvType::Done, after});
    }

    double estimated_wait(int site, double now) const {
        const SiteState &q = sites_[static_cast<std::size_t>(site)];
        if (q.busy < q.ports) return 0;
        if (q.ports == 0) return 1e9;
        double first = 1e300;
        for (double pe : q.port_end) if (pe < first) first = pe;
        return (first > now ? first - now : 0) + static_cast<double>(q.queue.size()) / q.ports * q.mean_session;
    }

    bool try_divert(int vi, int site, double t, double e) {
        Vehicle &ve = veh_[static_cast<std::size_t>(vi)];
        if (opt_.policy != DriverPolicy::Divert || ve.switched) return false;
        const double here = estimated_wait(site, t);
        const double threshold = opt_.divert_after_min > 0 ? opt_.divert_after_min : 20.0;
        if (here <= threshold) return false;
        const ElectricVehicle &v = vehicle(vi);
        const TripPlan &p = run_.plans[static_cast<std::size_t>(ve.plan)];
        const int next = ve.stop + 1 < static_cast<int>(p.stops.size())
                             ? sites_[static_cast<std::size_t>(p.stops[static_cast<std::size_t>(ve.stop) + 1].site)].node
                             : p.route.back();
        const EnergyModel &model = run_.router().model();
        const double target = sc_.config.target_soc * v.battery_kwh;
        int best = -1;
        double best_score = here;
        std::vector<int> a1, a2;
        for (std::size_t k = 0; k < sites_.size(); k++) {
            if (static_cast<int>(k) == site || !usable_by(run_.sites[k], v) || sites_[k].ports == 0) continue;
            if (!run_.router().leg(sites_[static_cast<std::size_t>(site)].node, sites_[k].node, a1)) continue;
            EnergyModel::Trace t1 = model.simulate(a1, e, v);
            if (!t1.ok) continue;                                     // must arrive above the reserve
            if (!run_.router().leg(sites_[k].node, next, a2)) continue;
            if (!model.simulate(a2, target, v).ok) continue;          // and continue after charging
            const double score = t1.minutes + estimated_wait(static_cast<int>(k), t + t1.minutes);
            if (score < best_score - 1e-9) { best_score = score; best = static_cast<int>(k); }
        }
        if (best < 0) return false;
        ve.switched = true;
        out_.sites[static_cast<std::size_t>(site)].diverted_away++;
        drive(vi, sites_[static_cast<std::size_t>(site)].node, sites_[static_cast<std::size_t>(best)].node, t, e, best);
        return true;
    }

    // group segments by vehicle (stable counting sort keeps each vehicle's time order), build paths
    void assemble() {
        const std::size_t nv = veh_.size();
        out_.seg_off.assign(nv + 1, 0);
        for (const RawSeg &g : raw_) out_.seg_off[static_cast<std::size_t>(g.v) + 1]++;
        for (std::size_t i = 0; i < nv; i++) out_.seg_off[i + 1] += out_.seg_off[i];
        std::vector<int> fill(out_.seg_off.begin(), out_.seg_off.end() - 1);
        std::vector<const RawSeg *> ord(raw_.size());
        for (const RawSeg &g : raw_) ord[static_cast<std::size_t>(fill[static_cast<std::size_t>(g.v)]++)] = &g;
        out_.seg.resize(raw_.size());
        out_.path_off.assign(nv + 1, 0);
        for (std::size_t vi = 0; vi < nv; vi++) {
            bool first = true;
            double end = 0;
            const int base = static_cast<int>(out_.path.size());
            for (int i = out_.seg_off[vi]; i < out_.seg_off[vi + 1]; i++) {
                const RawSeg &g = *ord[static_cast<std::size_t>(i)];
                SimSegment &o = out_.seg[static_cast<std::size_t>(i)];
                o.vehicle = static_cast<int>(vi);
                o.kind = g.kind;
                o.t0 = g.t0;
                o.t1 = g.t1;
                o.site = g.site;
                o.e0 = g.e0;
                o.e1 = g.e1;
                if (g.kind == SegmentKind::Drive) {
                    std::vector<int> nodes = run_.router().path(g.a, g.b);
                    if (nodes.empty()) nodes = {g.a};
                    const int start = first ? static_cast<int>(out_.path.size()) : static_cast<int>(out_.path.size()) - 1;
                    for (std::size_t k = first ? 0 : 1; k < nodes.size(); k++) out_.path.push_back(nodes[k]);
                    o.p0 = start - base;
                    o.p1 = static_cast<int>(out_.path.size()) - 1 - base;
                    first = false;
                } else {
                    o.p0 = o.p1 = static_cast<int>(out_.path.size()) - 1 - base;
                }
                if (g.t1 > end) end = g.t1;
            }
            out_.path_off[vi + 1] = static_cast<int>(out_.path.size());
            const double w = veh_[vi].waited;
            if (w > 1e-9) {
                out_.delayed++;
                out_.total_wait += w;
                if (w > out_.max_wait) out_.max_wait = w;
            }
            if (end > out_.end_min) out_.end_min = end;
        }
        for (const SimSite &s : out_.sites) out_.diverted += s.diverted_away;
        out_.mean_extra_min = nv ? out_.total_wait / static_cast<double>(nv) : 0;
    }

    PlanningRun &run_;
    const Scenario &sc_;
    const SimOptions &opt_;
    SimResult &out_;
    std::vector<SiteState> sites_;
    std::vector<Vehicle> veh_;
    std::vector<RawSeg> raw_;
    BinaryHeap<Event, EarlierEvent> heap_;
    long seq_ = 0;
};

} // namespace

SimResult simulate_day(PlanningRun &run, const SimOptions &options) {
    Stopwatch w;
    SimResult out;
    Day day(run, options, out);
    day.run();
    out.ms = w.elapsed_ms();
    return out;
}

} // namespace ev
