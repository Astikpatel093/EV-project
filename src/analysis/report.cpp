// report.cpp - exports, run summary, strategy comparison and the syllabus analysis report.
#include "ev/analysis/report.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "ev/algorithms/backtracking.hpp"
#include "ev/algorithms/branch_and_bound.hpp"
#include "ev/algorithms/divide_conquer.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/greedy.hpp"
#include "ev/algorithms/number_theory.hpp"
#include "ev/algorithms/searching.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/algorithms/string_matching.hpp"
#include "ev/core/rng.hpp"
#include "ev/core/timer.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/csv.hpp"
#include "ev/io/scenario_io.hpp"

namespace ev {

namespace {

std::string f(double v, int d) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", d, v);
    return b;
}

} // namespace

// ======================================================================== exports
void export_placement_csv(const std::string &path, const PlanningRun &run) {
    const Scenario &s = run.scenario();
    std::string t = "candidate_id,name,node_id,install_cost_inr,benefit,benefit_estimated,ports,power_kw,connector\n";
    for (int ci : run.placement.chosen) {
        const CandidateStation &c = s.candidates[static_cast<std::size_t>(ci)];
        const ScenarioContext &ctx = const_cast<PlanningRun &>(run).context();
        t += std::to_string(c.id) + "," + csv_escape(c.name) + "," + std::to_string(c.node) + "," +
             std::to_string(c.install_cost / 100) + "," + std::to_string(ctx.benefits.benefit[static_cast<std::size_t>(ci)]) + "," +
             (ctx.benefits.estimated[static_cast<std::size_t>(ci)] ? "yes" : "no") + "," + std::to_string(c.ports) + "," +
             f(c.power_kw, 1) + "," + csv_escape(c.connector) + "\n";
    }
    write_text_file(path, t);
}

void export_plans_csv(const std::string &path, const PlanningRun &run) {
    const Scenario &s = run.scenario();
    std::string t = "trip_id,vehicle,from,to,status,feasible,needs_charge,distance_km,energy_kwh,shortest_km,depart_min,"
                    "arrive_min,charge_min,stops,explanation\n";
    for (std::size_t i = 0; i < run.plans.size(); i++) {
        const TripPlan &p = run.plans[i];
        const TripRequest &q = s.trips[i];
        std::string stops;
        for (std::size_t k = 0; k < p.stops.size(); k++) stops += (k ? ";" : "") + std::to_string(p.stops[k].station_id);
        t += std::to_string(q.id) + "," + csv_escape(q.vehicle_id) + "," + std::to_string(q.from) + "," + std::to_string(q.to) + "," +
             to_string(p.status) + "," + (p.feasible() ? "1" : "0") + "," + (p.needs_charge ? "1" : "0") + "," +
             f(p.feasible() ? p.distance_km : 0, 3) + "," + f(p.feasible() ? p.energy_kwh : 0, 3) + "," + f(p.shortest_km, 3) + "," +
             f(p.depart_min, 1) + "," + f(p.feasible() ? p.arrive_min : 0, 1) + "," + f(p.charge_min, 1) + "," + stops + "," +
             csv_escape(p.explanation) + "\n";
    }
    write_text_file(path, t);
}

void export_summary_json(const std::string &path, const PlanningRun &run, const CapacityResult *cap, const SessionAnalysis *ses) {
    const Scenario &s = run.scenario();
    JsonWriter j;
    j.begin_object();
    j.string("scenario", s.config.name);
    j.string("data_source", to_string(s.config.source));
    j.begin_object("size");
    j.integer("nodes", s.network.node_count());
    j.integer("roads", s.network.edge_count());
    j.integer("existing_stations", static_cast<std::int64_t>(s.stations.size()));
    j.integer("candidates", static_cast<std::int64_t>(s.candidates.size()));
    j.integer("vehicles", static_cast<std::int64_t>(s.vehicles.size()));
    j.integer("trips", static_cast<std::int64_t>(s.trips.size()));
    j.integer("budget_inr", run.placement.budget / 100);
    j.end_object();
    j.begin_object("strategy");
    j.string("placement", to_string(run.options().placement));
    j.string("routing", to_string(run.options().routing));
    j.end_object();
    j.begin_object("placement");
    j.integer("built", static_cast<std::int64_t>(run.placement.chosen.size()));
    j.integer("cost_inr", run.placement.cost / 100);
    j.integer("benefit", run.placement.benefit);
    j.boolean("optimal", run.placement.optimal);
    j.boolean("within_budget", run.placement.cost <= run.placement.budget);
    j.integer("search_nodes", static_cast<std::int64_t>(run.placement.search_nodes));
    j.number("ms", run.placement.ms);
    j.string("note", run.placement.note);
    j.begin_array("candidate_ids");
    for (int ci : run.placement.chosen) j.integer(nullptr, s.candidates[static_cast<std::size_t>(ci)].id);
    j.end_array();
    j.end_object();
    const Kpi &k = run.kpi;
    j.begin_object("service");
    j.integer("trips", k.trips);
    j.integer("feasible", k.feasible);
    j.number("feasible_pct", k.feasible_pct);
    j.integer("need_charge", k.need_charge);
    j.integer("charged_ok", k.charged_ok);
    j.number("avg_km", k.avg_km);
    j.number("avg_kwh", k.avg_kwh);
    j.number("avg_stops", k.avg_stops);
    j.number("avg_charge_min", k.avg_charge_min);
    j.number("avg_detour_km", k.avg_detour_km);
    j.number("avg_travel_min", k.avg_travel_min);
    j.end_object();
    j.begin_object("performance");
    j.number("place_ms", run.ms_place);
    j.number("plan_ms", run.ms_plan);
    j.integer("shortest_path_trees", static_cast<std::int64_t>(const_cast<PlanningRun &>(run).router().trees_built()));
    j.end_object();
    if (cap) {
        j.begin_object("capacity");
        j.integer("trips_needing", cap->trips_needing);
        j.integer("slots", cap->slots_total);
        j.integer("maxflow_ff_dfs", cap->dfs.value);
        j.integer("maxflow_edmonds_karp", cap->bfs.value);
        j.integer("min_cut", cap->min_cut);
        j.integer("bipartite_matching", cap->matching);
        j.boolean("consistent", cap->consistent());
        j.end_object();
    }
    if (ses) {
        j.begin_object("sessions");
        j.integer("sessions", ses->sessions);
        j.integer("served_with_ports", ses->served);
        j.integer("overloaded_sites", ses->overloaded);
        j.integer("surge_from_h", ses->surge_lo);
        j.integer("surge_to_h", ses->surge_hi);
        j.number("surge_excess_kwh", ses->surge_excess_kwh);
        j.begin_array("hourly_kwh");
        for (double v : ses->hourly_kwh) j.number(nullptr, v);
        j.end_array();
        j.end_object();
    }
    j.end_object();
    write_text_file(path, j.str());
}

// ======================================================================== text summaries
void print_run(std::ostream &o, const PlanningRun &run, const CapacityResult *cap, const SessionAnalysis *ses) {
    const Scenario &s = run.scenario();
    const Kpi &k = run.kpi;
    const PlacementResult &p = run.placement;
    o << "\n== Scenario '" << s.config.name << "' (" << to_string(s.config.source) << " data): " << s.network.node_count()
      << " nodes, " << s.network.edge_count() << " roads, " << s.stations.size() << " existing stations, " << s.candidates.size()
      << " candidate sites, " << s.vehicles.size() << " vehicle types, " << s.trips.size() << " trips\n";
    o << "-- Placement [" << to_string(p.method) << "]: " << p.chosen.size() << " new stations, cost " << format_inr_short(p.cost)
      << " of budget " << format_inr_short(p.budget) << ", benefit " << p.benefit << (p.optimal ? " (optimal)" : "") << ", "
      << f(p.ms, 2) << " ms\n";
    if (!p.note.empty()) o << "   note: " << p.note << "\n";
    o << "   sites:";
    for (std::size_t i = 0; i < p.chosen.size() && i < 20; i++) o << " " << s.candidates[static_cast<std::size_t>(p.chosen[i])].id;
    if (p.chosen.size() > 20) o << " ... (+" << p.chosen.size() - 20 << ")";
    o << "\n-- Trip planning [" << to_string(run.options().routing) << " legs]: " << k.feasible << "/" << k.trips << " feasible ("
      << f(k.feasible_pct, 1) << "%); " << k.need_charge << " needed a charge, " << k.charged_ok << " of those served\n";
    o << "   avg " << f(k.avg_km, 1) << " km, " << f(k.avg_kwh, 2) << " kWh, " << f(k.avg_stops, 2) << " stops, "
      << f(k.avg_charge_min, 1) << " min charging, " << f(k.avg_detour_km, 1) << " km detour, " << f(k.avg_travel_min, 1)
      << " min door-to-door\n";
    o << "   planning " << f(run.ms_plan, 2) << " ms, " << const_cast<PlanningRun &>(run).router().trees_built() << " shortest-path trees\n";
    if (cap) {
        o << "-- Capacity (window " << s.config.window_min << " min): " << cap->trips_needing << " trips need a charge, "
          << cap->slots_total << " port-slots\n";
        o << "   max-flow Ford-Fulkerson=" << cap->dfs.value << " (" << cap->dfs.augmentations << " paths)  Edmonds-Karp="
          << cap->bfs.value << " (" << cap->bfs.augmentations << " paths)  min-cut=" << cap->min_cut << "  matching=" << cap->matching
          << (cap->consistent() ? "  [consistent]" : "  [MISMATCH]") << "\n";
        if (cap->bottleneck_site >= 0) o << "   bottleneck: " << run.sites[static_cast<std::size_t>(cap->bottleneck_site)].name << "\n";
    }
    if (ses) {
        o << "-- Sessions: " << ses->sessions << " planned, " << ses->served << " servable with installed ports, " << ses->overloaded
          << " overloaded sites\n";
        if (ses->surge_lo >= 0)
            o << "   worst grid surge: hours " << ses->surge_lo << "-" << ses->surge_hi << ", " << f(ses->surge_excess_kwh, 1)
              << " kWh above supply\n";
        else o << "   grid supply covers every hour of the horizon\n";
    }
}

void print_plan(std::ostream &o, const Scenario &s, const TripPlan &p) {
    auto clock = [](double m) {
        char b[16];
        int mm = static_cast<int>(std::floor(m + 0.5));
        std::snprintf(b, sizeof b, "%02d:%02d", (mm / 60) % 24, mm % 60);
        return std::string(b);
    };
    o << "Trip " << p.trip_id << " (vehicle " << p.vehicle_id << "): " << to_string(p.status) << "\n";
    o << "  " << p.explanation << "\n";
    if (p.shortest_km >= 0)
        o << "  shortest road route: " << f(p.shortest_km, 1) << " km, " << f(p.shortest_kwh, 2) << " kWh, "
          << (p.shortest_is_feasible ? "drivable on the starting charge" : "NOT drivable on the starting charge") << "\n";
    if (!p.feasible() || p.status == PlanStatus::ZeroLength) return;
    o << "  depart " << clock(p.depart_min) << " with " << f(p.start_kwh, 2) << " kWh";
    for (const ChargingStop &st : p.stops)
        o << "\n  stop at " << st.name << " (node " << s.network.node(st.node).id << "): arrive " << clock(st.arrive_min) << " with "
          << f(st.arrive_kwh, 2) << " kWh, charge " << f(st.charge_min, 0) << " min to " << f(st.depart_kwh, 2) << " kWh";
    o << "\n  arrive " << clock(p.arrive_min) << " with " << f(p.arrive_kwh, 2) << " kWh (lowest " << f(p.min_kwh, 2) << " kWh); "
      << f(p.distance_km, 1) << " km, " << p.route.size() << " nodes\n";
}

// ======================================================================== comparison
std::vector<CompareRow> compare_strategies(ScenarioContext &ctx, std::ostream *o, const std::string &csv_path) {
    std::vector<CompareRow> rows;
    const Scenario &s = ctx.scenario;
    if (o)
        *o << "\n== Strategy comparison on '" << s.config.name << "' (" << s.trips.size() << " trips, budget "
           << format_inr_short(s.config.budget) << ")\n"
           << "placement routing   new     cost  benefit opt  place_ms     nodes | feasible     %  avg_km avg_kWh route_ms\n";
    for (int pm = 0; pm < kPlacementMethods; pm++)
        for (RoutingMethod rm : {RoutingMethod::Dijkstra, RoutingMethod::FloydWarshall, RoutingMethod::BellmanFord}) {
            if (rm == RoutingMethod::FloydWarshall && s.network.node_count() > 1600) continue;   // Θ(V^3)
            RunOptions opt;
            opt.placement = static_cast<PlacementMethod>(pm);
            opt.routing = rm;
            Stopwatch w;
            PlanningRun run(ctx, opt);
            CompareRow r;
            r.placement = opt.placement;
            r.routing = rm;
            r.stations = static_cast<int>(run.placement.chosen.size());
            r.cost = run.placement.cost;
            r.benefit = run.placement.benefit;
            r.optimal = run.placement.optimal;
            r.place_ms = run.ms_place;
            r.search_nodes = run.placement.search_nodes;
            r.feasible = run.kpi.feasible;
            r.feasible_pct = run.kpi.feasible_pct;
            r.avg_km = run.kpi.avg_km;
            r.avg_kwh = run.kpi.avg_kwh;
            r.route_ms = run.ms_router + run.ms_plan;
            r.trees = run.router().trees_built();
            rows.push_back(r);
            if (o) {
                char line[256];
                std::snprintf(line, sizeof line, "%-9s %-9s %3d %8s %8lld %3s %9.2f %9llu | %8d %5.1f %7.2f %7.3f %8.1f\n",
                              to_string(r.placement), to_string(r.routing), r.stations, format_inr_short(r.cost).c_str(),
                              static_cast<long long>(r.benefit), r.optimal ? "yes" : "no", r.place_ms,
                              static_cast<unsigned long long>(r.search_nodes), r.feasible, r.feasible_pct, r.avg_km, r.avg_kwh, r.route_ms);
                *o << line;
            }
        }
    if (!csv_path.empty()) {
        std::string t = "placement,routing,new_stations,cost_inr,benefit,optimal,place_ms,search_nodes,feasible,feasible_pct,avg_km,avg_kwh,route_ms,trees\n";
        for (const CompareRow &r : rows)
            t += std::string(to_string(r.placement)) + "," + to_string(r.routing) + "," + std::to_string(r.stations) + "," +
                 std::to_string(r.cost / 100) + "," + std::to_string(r.benefit) + "," + (r.optimal ? "1" : "0") + "," + f(r.place_ms, 4) + "," +
                 std::to_string(r.search_nodes) + "," + std::to_string(r.feasible) + "," + f(r.feasible_pct, 3) + "," + f(r.avg_km, 4) + "," +
                 f(r.avg_kwh, 4) + "," + f(r.route_ms, 4) + "," + std::to_string(r.trees) + "\n";
        write_text_file(csv_path, t);
    }
    return rows;
}

// ======================================================================== analysis
void analyze(ScenarioContext &ctx, std::ostream &o, JsonWriter *j) {
    const Scenario &sc = ctx.scenario;
    const ScenarioConfig &c = sc.config;
    auto jobj = [&](const char *k) { if (j) j->begin_object(k); };
    auto jend = [&]() { if (j) j->end_object(); };
    auto jnum = [&](const char *k, double v) { if (j) j->number(k, v); };
    auto jint = [&](const char *k, std::int64_t v) { if (j) j->integer(k, v); };
    auto jstr = [&](const char *k, const std::string &v) { if (j) j->string(k, v); };
    auto jbool = [&](const char *k, bool v) { if (j) j->boolean(k, v); };
    jobj(nullptr);
    jstr("scenario", c.name);
    jstr("data_source", to_string(c.source));

    RunOptions opt;
    PlanningRun run(ctx, opt);
    CapacityResult cap = analyze_capacity(run);
    SessionAnalysis ses = analyze_sessions(run);
    print_run(o, run, &cap, &ses);
    const int S = static_cast<int>(run.sites.size());
    const std::vector<ChargerSite> &sites = run.sites;
    auto site_benefit = [&](int k) -> std::int64_t {
        const ChargerSite &x = sites[static_cast<std::size_t>(k)];
        return x.kind == ChargerSite::Kind::New ? ctx.benefits.benefit[static_cast<std::size_t>(x.source)] : 0;
    };
    Stopwatch w;

    // ------------------------------------------------ 1. sorting and searching
    o << "\n[1] Sorting, searching and indices\n";
    jobj("sorting_searching");
    {
        std::vector<TripRequest> a = sc.trips, b = sc.trips;
        w.restart();
        merge_sort(a, [](const TripRequest &x, const TripRequest &y) { return x.depart_min < y.depart_min; });
        const double ms_m = w.elapsed_ms();
        w.restart();
        quick_sort(b, [](const TripRequest &x, const TripRequest &y) {
            return x.depart_min < y.depart_min || (x.depart_min == y.depart_min && x.id < y.id);
        });
        const double ms_q = w.elapsed_ms();
        bool sorted = true, stable = true, agree = true;
        for (std::size_t i = 1; i < a.size(); i++) {
            if (a[i - 1].depart_min > a[i].depart_min) sorted = false;
            if (a[i - 1].depart_min == a[i].depart_min && a[i - 1].id > a[i].id) stable = false;   // ids ascend in the input
        }
        for (std::size_t i = 0; i < a.size(); i++)
            if (a[i].depart_min != b[i].depart_min || a[i].id != b[i].id) agree = false;
        o << "    dispatch queue (trips by departure): merge sort " << f(ms_m, 3) << " ms [sorted=" << sorted << " stable=" << stable
          << "], quick sort " << f(ms_q, 3) << " ms [agrees=" << agree << "]\n";
        jnum("merge_ms", ms_m);
        jnum("quick_ms", ms_q);
        jbool("merge_sorted", sorted);
        jbool("merge_stable", stable);
        jbool("quick_agrees", agree);

        std::vector<Paise> costs;
        for (const CandidateStation &cd : sc.candidates) costs.push_back(cd.install_cost);
        quick_sort(costs, [](Paise x, Paise y) { return x < y; }, PivotRule::Random);
        const Paise cut = c.budget / 10;
        const std::size_t afford = upper_bound_index(costs, cut);
        o << "    cost index (quick sort): " << afford << " candidate sites cost <= " << format_inr_short(cut)
          << " (10% of the budget), found by binary search\n";
        jint("sites_under_10pct_budget", static_cast<std::int64_t>(afford));

        std::vector<int> ids;
        for (const RoadNode &n : sc.network.nodes()) ids.push_back(n.id);
        merge_sort(ids, [](int x, int y) { return x < y; });
        long hit_i = 0, hit_r = 0;
        w.restart();
        for (const TripRequest &t : sc.trips) hit_i += binary_search_iterative(ids, t.from) >= 0;
        const double ms_i = w.elapsed_ms();
        w.restart();
        for (const TripRequest &t : sc.trips) hit_r += binary_search_recursive(ids, t.to) >= 0;
        const double ms_r = w.elapsed_ms();
        o << "    node-id index: iterative binary search " << hit_i << "/" << sc.trips.size() << " (" << f(ms_i, 3)
          << " ms), recursive " << hit_r << "/" << sc.trips.size() << " (" << f(ms_r, 3) << " ms)\n";
        jint("bsearch_iter_hits", hit_i);
        jint("bsearch_rec_hits", hit_r);
    }
    jend();

    // ------------------------------------------------ 2. divide and conquer
    o << "\n[2] Divide and conquer (illustrative uses on scenario data)\n";
    jobj("divide_conquer");
    {
        std::vector<double> net(ses.hourly_kwh.size());
        for (std::size_t h = 0; h < net.size(); h++) net[h] = ses.hourly_kwh[h] - c.grid_kw;
        SubarrayResult d = max_subarray_dc(net), kd = max_subarray_kadane(net);
        o << "    max subarray of (charging load - grid supply) per hour: D&C " << f(d.sum, 1) << " kWh [h" << d.lo << "..h" << d.hi
          << "], Kadane " << f(kd.sum, 1) << " -> " << (std::fabs(d.sum - kd.sum) < 1e-6 ? "agree" : "DISAGREE") << "\n";
        jnum("surge_sum_dc", d.sum);
        jint("surge_lo", static_cast<std::int64_t>(d.lo));
        jint("surge_hi", static_cast<std::int64_t>(d.hi));
        jnum("surge_sum_kadane", kd.sum);

        const double cap0 = sc.vehicles.empty() ? 30.0 : sc.vehicles.front().battery_kwh, fade = 0.0002;
        const std::uint64_t cycles = 1500;
        const double cf = cap0 * pow_fast(1 - fade, cycles), cn = cap0 * pow_naive(1 - fade, cycles);
        o << "    battery after " << cycles << " cycles at " << f(fade * 100, 2) << "%/cycle fade: fast power " << f(cf, 4)
          << " kWh, naive " << f(cn, 4) << " kWh\n";
        jnum("battery_after_cycles_kwh", cf);
        jnum("battery_pow_diff", std::fabs(cf - cn));

        if (S > 0) {
            // a site "knows" (forwards overflow to) another when the other is strictly better equipped
            auto better = [&](int x, int y) {
                const ChargerSite &p = sites[static_cast<std::size_t>(x)], &q = sites[static_cast<std::size_t>(y)];
                if (p.ports != q.ports) return p.ports < q.ports;
                if (p.power_kw != q.power_kw) return p.power_kw < q.power_kw;
                if (site_benefit(x) != site_benefit(y)) return site_benefit(x) < site_benefit(y);
                return x > y;
            };
            long qd = 0, qn = 0;
            int hub = find_celebrity(S, better, &qd), hub2 = find_celebrity_naive(S, better, &qn);
            o << "    overflow hub (celebrity: every site forwards to it, it forwards to none): "
              << (hub >= 0 ? sites[static_cast<std::size_t>(hub)].name : std::string("none")) << "; elimination " << qd
              << " queries vs naive " << qn << "\n";
            jint("hub_station_id", hub >= 0 ? sites[static_cast<std::size_t>(hub)].id : -1);
            jint("celebrity_queries_dc", qd);
            jint("celebrity_queries_naive", qn);
            jbool("celebrity_agree", hub == hub2);
        }
        // zone-to-zone demand transition matrix P; four steps P^4 = (P^2)^2
        const int G = 8, Z = G * G;
        auto xy = sc.network.planar_km();
        double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
        for (auto &p : xy) {
            minx = std::fmin(minx, p.first); maxx = std::fmax(maxx, p.first);
            miny = std::fmin(miny, p.second); maxy = std::fmax(maxy, p.second);
        }
        auto zone = [&](int pos) {
            int zx = static_cast<int>((xy[static_cast<std::size_t>(pos)].first - minx) / (maxx - minx + 1e-9) * G);
            int zy = static_cast<int>((xy[static_cast<std::size_t>(pos)].second - miny) / (maxy - miny + 1e-9) * G);
            if (zx >= G) zx = G - 1;
            if (zy >= G) zy = G - 1;
            return zy * G + zx;
        };
        Matrix P(Z, Z);
        for (const TripRequest &t : sc.trips) P(static_cast<std::size_t>(zone(sc.network.index_of(t.from))), static_cast<std::size_t>(zone(sc.network.index_of(t.to)))) += 1;
        for (int r = 0; r < Z; r++) {
            double sum = 0;
            for (int q = 0; q < Z; q++) sum += P(static_cast<std::size_t>(r), static_cast<std::size_t>(q));
            if (sum == 0) P(static_cast<std::size_t>(r), static_cast<std::size_t>(r)) = 1;
            else for (int q = 0; q < Z; q++) P(static_cast<std::size_t>(r), static_cast<std::size_t>(q)) /= sum;
        }
        w.restart();
        Matrix P2 = multiply_classical(P, P), P4 = multiply_classical(P2, P2);
        const double ms_c = w.elapsed_ms();
        w.restart();
        Matrix Q2 = multiply_strassen(P, P, 8), Q4 = multiply_strassen(Q2, Q2, 8);
        const double ms_s = w.elapsed_ms();
        const double md = P4.max_abs_diff(Q4);
        o << "    demand propagation P^4 over " << Z << "x" << Z << " zones: classical " << f(ms_c, 3) << " ms, Strassen " << f(ms_s, 3)
          << " ms, max |difference| " << md << "\n";
        jnum("matmul_classic_ms", ms_c);
        jnum("matmul_strassen_ms", ms_s);
        jnum("matmul_max_diff", md);
    }
    jend();

    // ------------------------------------------------ 3. greedy
    o << "\n[3] Greedy\n";
    jobj("greedy");
    {
        int worst = -1;
        for (int q = 0; q < static_cast<int>(ses.sites.size()); q++)
            if (worst < 0 || ses.sites[static_cast<std::size_t>(q)].chargers_needed > ses.sites[static_cast<std::size_t>(worst)].chargers_needed) worst = q;
        if (worst >= 0 && ses.sites[static_cast<std::size_t>(worst)].sessions > 0) {
            const SiteLoad &L = ses.sites[static_cast<std::size_t>(worst)];
            const ChargerSite &x = sites[static_cast<std::size_t>(L.site)];
            o << "    interval partitioning: busiest site " << x.name << " needs " << L.chargers_needed << " bays for " << L.sessions
              << " sessions (max overlap " << L.depth << ", has " << x.ports << ")\n";
            o << "    activity selection per bay: " << L.served << " of " << L.sessions << " servable there; network-wide " << ses.served
              << "/" << ses.sessions << "\n";
            jint("busiest_station_id", x.id);
            jint("chargers_needed", L.chargers_needed);
            jint("max_overlap", L.depth);
            jint("served_network", ses.served);
            jint("sessions_network", ses.sessions);
        }
        if (!sc.events.empty()) {
            w.restart();
            HuffmanCode h(sc.events);
            std::vector<bool> bits = h.encode(sc.events);
            const bool rt = h.decode(bits) == sc.events;
            const double ms_h = w.elapsed_ms();
            const double bytes = std::ceil(static_cast<double>(bits.size()) / 8.0);
            o << "    Huffman on the " << sc.events.size() << "-event station log: " << h.symbols() << " symbols, " << f(h.average_bits(), 3)
              << " bits/event, " << sc.events.size() << " -> " << static_cast<long long>(bytes) << " bytes ("
              << f(100.0 * bytes / static_cast<double>(sc.events.size()), 1) << "%), round trip " << (rt ? "ok" : "FAILED") << ", "
              << f(ms_h, 3) << " ms\n";
            jint("huffman_symbols", static_cast<std::int64_t>(h.symbols()));
            jnum("huffman_bits_per_event", h.average_bits());
            jnum("huffman_ratio", bytes / static_cast<double>(sc.events.size()));
            jbool("huffman_roundtrip", rt);
        }
        o << "    Dijkstra: " << run.router().trees_built() << " shortest-path trees built while planning\n";
        if (S >= 2) {
            std::vector<std::vector<double>> W(static_cast<std::size_t>(S), std::vector<double>(static_cast<std::size_t>(S), kInfinity));
            Graph g(S, false);
            for (int a = 0; a < S; a++) {
                W[static_cast<std::size_t>(a)][static_cast<std::size_t>(a)] = 0;
                for (int b = a + 1; b < S; b++) {
                    double k1 = run.router().leg_km(sites[static_cast<std::size_t>(a)].node, sites[static_cast<std::size_t>(b)].node);
                    double k2 = run.router().leg_km(sites[static_cast<std::size_t>(b)].node, sites[static_cast<std::size_t>(a)].node);
                    double d = kInfinity;
                    if (k1 >= 0) d = k1;
                    if (k2 >= 0 && k2 < d) d = k2;
                    W[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = W[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = d;
                    if (d < kInfinity) g.add_edge(a, b, d);
                }
            }
            w.restart();
            SpanningForest fp = prim(g);
            const double ms_p = w.elapsed_ms();
            w.restart();
            SpanningForest fk = kruskal(g);
            const double ms_k = w.elapsed_ms();
            o << "    cable backbone MST over " << S << " charger sites: Prim " << f(fp.total_weight, 2) << " km (" << f(ms_p, 3)
              << " ms), Kruskal " << f(fk.total_weight, 2) << " km (" << f(ms_k, 3) << " ms) -> "
              << (std::fabs(fp.total_weight - fk.total_weight) < 1e-6 ? "agree" : "DISAGREE")
              << (fp.is_spanning_tree() ? "" : " (spanning forest: the sites are not all connected)") << "\n";
            jnum("mst_prim_km", fp.total_weight);
            jnum("mst_kruskal_km", fk.total_weight);
            jnum("prim_ms", ms_p);
            jnum("kruskal_ms", ms_k);
            jint("mst_components", fp.components);
        }
    }
    jend();

    // ------------------------------------------------ 4. dynamic programming
    o << "\n[4] Dynamic programming\n";
    jobj("dynamic_programming");
    {
        PlacementResult g = place_stations(sc, ctx.benefits.benefit, PlacementMethod::Greedy);
        o << "    0/1 knapsack placement: DP benefit " << run.placement.benefit << " (cost " << format_inr_short(run.placement.cost)
          << ") vs greedy ratio " << g.benefit << " (cost " << format_inr_short(g.cost) << ") -> greedy reaches "
          << f(run.placement.benefit ? 100.0 * static_cast<double>(g.benefit) / static_cast<double>(run.placement.benefit) : 100.0, 1) << "%\n";
        jint("knapsack_dp_value", run.placement.benefit);
        jint("knapsack_greedy_value", g.benefit);

        const std::uint64_t n = static_cast<std::uint64_t>(sc.network.node_count());
        std::vector<std::uint64_t> dims = {n, 64, 64, 64, 64, n};
        MatrixChainResult mc = matrix_chain_order(dims);
        const std::uint64_t left = matrix_chain_left_to_right(dims);
        o << "    matrix chain (node->zone, 3 zone transitions, zone->node): optimal " << mc.order << " = " << mc.cost
          << " multiplications; left-to-right " << left << "\n";
        jstr("matrix_chain_order", mc.order);
        jint("matrix_chain_cost", static_cast<std::int64_t>(mc.cost));
        jint("matrix_chain_left", static_cast<std::int64_t>(left));

        // route adherence: LCS of the planned node sequence and a simulated GPS trace
        Rng rng(2026);
        int cnt = 0;
        double adh = 0;
        for (const TripPlan &p : run.plans) {
            if (cnt >= 50) break;
            if (!p.feasible() || p.route.size() < 3) continue;
            std::vector<int> gps;
            for (int node : p.route) {
                const double u = rng.unit();
                if (u < 0.08) continue;                                                  // GPS dropout
                gps.push_back(node);
                if (u > 0.92) gps.push_back(static_cast<int>(rng.range(0, sc.network.node_count() - 1)));   // off-route fix
            }
            adh += static_cast<double>(lcs(p.route, gps).length) / static_cast<double>(p.route.size());
            cnt++;
        }
        if (cnt) adh /= cnt;
        o << "    LCS route adherence over " << cnt << " trips (planned vs simulated GPS): " << f(100 * adh, 1) << "%\n";
        jnum("lcs_adherence", adh);

        if (S > 0) {
            // optimal BST for looking up sites by id, weighted by planned sessions + 1
            std::vector<int> order(static_cast<std::size_t>(S));
            for (int k = 0; k < S; k++) order[static_cast<std::size_t>(k)] = k;
            merge_sort(order, [&](int a, int b) { return sites[static_cast<std::size_t>(a)].id < sites[static_cast<std::size_t>(b)].id; });
            std::vector<double> p(static_cast<std::size_t>(S) + 1, 0.0), q(static_cast<std::size_t>(S) + 1, 0.0);
            double tot = 0;
            for (int k = 0; k < S; k++) {
                p[static_cast<std::size_t>(k) + 1] = ses.sites[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])].sessions + 1.0;
                tot += p[static_cast<std::size_t>(k) + 1];
            }
            for (int k = 1; k <= S; k++) p[static_cast<std::size_t>(k)] /= tot;
            const double eo = optimal_bst(p, q), eb = balanced_bst_cost(p, q);
            o << "    optimal BST site lookup: " << f(eo, 3) << " expected probes vs balanced " << f(eb, 3) << "\n";
            jnum("obst_expected_probes", eo);
            jnum("balanced_expected_probes", eb);

            // maintenance van tour: depot = busiest junction, then the 9 busiest sites
            int depot = 0;
            {
                std::vector<int> pass(static_cast<std::size_t>(sc.network.node_count()), 0);
                for (const TripPlan &tp : run.plans)
                    for (int node : tp.route) pass[static_cast<std::size_t>(node)]++;
                for (int v = 1; v < sc.network.node_count(); v++)
                    if (pass[static_cast<std::size_t>(v)] > pass[static_cast<std::size_t>(depot)]) depot = v;
            }
            std::vector<int> pick(order);
            merge_sort(pick, [&](int a, int b) {
                return ses.sites[static_cast<std::size_t>(a)].sessions > ses.sites[static_cast<std::size_t>(b)].sessions ||
                       (ses.sites[static_cast<std::size_t>(a)].sessions == ses.sites[static_cast<std::size_t>(b)].sessions && a < b);
            });
            const int k = S < 9 ? S : 9;
            std::vector<int> pts = {depot};
            for (int t = 0; t < k; t++) pts.push_back(sites[static_cast<std::size_t>(pick[static_cast<std::size_t>(t)])].node);
            std::vector<std::vector<double>> D(pts.size(), std::vector<double>(pts.size(), 0.0));
            for (std::size_t a = 0; a < pts.size(); a++)
                for (std::size_t b = 0; b < pts.size(); b++) {
                    double km = run.router().leg_km(pts[a], pts[b]);
                    D[a][b] = km >= 0 ? km : kInfinity;
                }
            w.restart();
            TourResult hk = tsp_held_karp(D);
            const double ms_hk = w.elapsed_ms();
            w.restart();
            TourResult bf = tsp_brute(D);
            const double ms_bf = w.elapsed_ms();
            o << "    maintenance van TSP (depot + " << k << " sites): Held-Karp " << f(hk.length, 2) << " km (" << f(ms_hk, 3)
              << " ms), brute force " << f(bf.length, 2) << " km (" << f(ms_bf, 3) << " ms) -> "
              << (std::fabs(hk.length - bf.length) < 1e-6 ? "agree" : "DISAGREE") << "\n";
            jnum("tsp_held_karp_km", hk.length);
            jnum("tsp_brute_km", bf.length);
            jnum("tsp_hk_ms", ms_hk);
            jnum("tsp_brute_ms", ms_bf);
        }
    }
    jend();

    // ------------------------------------------------ 5. backtracking and branch-and-bound
    o << "\n[5] Backtracking and branch-and-bound\n";
    jobj("backtracking_bnb");
    {
        std::uint64_t nodes = 0;
        std::uint64_t q8 = nqueens_count(8, nullptr, &nodes);
        o << "    N-Queens(8): " << q8 << " solutions (expected 92) in " << nodes << " nodes -> framework "
          << (q8 == 92 ? "validated" : "BROKEN") << "\n";
        jint("nqueens8", static_cast<std::int64_t>(q8));

        // spend the leftover budget exactly on unselected sites? (costs in lakhs)
        std::vector<char> in(sc.candidates.size(), 0);
        for (int ci : run.placement.chosen) in[static_cast<std::size_t>(ci)] = 1;
        std::vector<std::int64_t> costs;
        for (std::size_t i = 0; i < sc.candidates.size(); i++)
            if (!in[i]) costs.push_back(sc.candidates[i].install_cost / kPaisePerLakh);
        const std::int64_t left = (run.placement.budget - run.placement.cost) / kPaisePerLakh;
        SubsetSumResult ss = subset_sum_backtracking(costs, left, 2000000);
        bool dp_ok = false, dp_known = true;
        try { dp_ok = subset_sum_dp(costs, left); } catch (const std::length_error &) { dp_known = false; }
        const char *bt = ss.outcome == SearchOutcome::Found ? "yes" : ss.outcome == SearchOutcome::Impossible ? "no" : "aborted";
        o << "    subset sum: spend the leftover " << left << " lakh exactly on unselected sites? backtracking=" << bt << " ("
          << ss.nodes << " nodes), DP oracle=" << (dp_known ? (dp_ok ? "yes" : "no") : "n/a") << "\n";
        jint("leftover_budget", left);
        jint("subset_sum_backtracking", ss.outcome == SearchOutcome::Found ? 1 : ss.outcome == SearchOutcome::Impossible ? 0 : -1);
        jint("subset_sum_dp", dp_known ? (dp_ok ? 1 : 0) : -1);

        std::vector<std::int64_t> wv, vv;
        std::int64_t g = c.budget;
        for (const CandidateStation &cd : sc.candidates) g = gcd(g, cd.install_cost);
        if (g <= 0) g = 1;
        for (std::size_t i = 0; i < sc.candidates.size(); i++) {
            wv.push_back(sc.candidates[i].install_cost / g);
            vv.push_back(ctx.benefits.benefit[i]);
        }
        const std::int64_t opt_v = run.placement.benefit;
        jobj("bnb");
        for (Frontier fr : {Frontier::FIFO, Frontier::LIFO, Frontier::LeastCost}) {
            w.restart();
            BranchBoundResult b = knapsack_branch_and_bound(wv, vv, c.budget / g, fr);
            const double ms = w.elapsed_ms();
            o << "    knapsack branch-and-bound " << frontier_name(fr) << ": benefit " << b.value << " (DP " << opt_v << ", "
              << (b.value == opt_v ? "optimal" : "DIFFERENT") << "), generated " << b.stats.generated << ", expanded " << b.stats.expanded
              << ", peak frontier " << b.stats.max_frontier << ", " << f(ms, 3) << " ms" << (b.stats.complete ? "" : " [node limit]") << "\n";
            jobj(frontier_name(fr));
            jint("value", b.value);
            jint("generated", static_cast<std::int64_t>(b.stats.generated));
            jint("expanded", static_cast<std::int64_t>(b.stats.expanded));
            jint("max_frontier", static_cast<std::int64_t>(b.stats.max_frontier));
            jnum("ms", ms);
            jbool("complete", b.stats.complete);
            jend();
        }
        jend();
    }
    jend();

    // ------------------------------------------------ 6. flow
    o << "\n[6] Network flow and matching\n    see the capacity line above: Ford-Fulkerson, Edmonds-Karp, min-cut and matching all give "
      << cap.dfs.value << "/" << cap.bfs.value << "/" << cap.min_cut << "/" << cap.matching << "\n";
    jobj("flow");
    jint("ff_dfs", cap.dfs.value);
    jint("edmonds_karp", cap.bfs.value);
    jint("min_cut", cap.min_cut);
    jint("matching", cap.matching);
    jint("trips_needing", cap.trips_needing);
    jint("slots", cap.slots_total);
    jend();

    // ------------------------------------------------ 7. number theory and strings
    o << "\n[7] Chinese Remainder Theorem and string matching (illustrative uses)\n";
    jobj("crt_strings");
    {
        std::vector<std::int64_t> a, m;
        std::string names;
        for (int k = 0; k < S && a.size() < 4; k++) {
            const ChargerSite &x = sites[static_cast<std::size_t>(k)];
            int period = 0, offset = 0;
            if (x.kind == ChargerSite::Kind::Existing) {
                period = sc.stations[static_cast<std::size_t>(x.source)].maint_period_h;
                offset = sc.stations[static_cast<std::size_t>(x.source)].maint_offset_h;
            } else {
                period = sc.candidates[static_cast<std::size_t>(x.source)].maint_period_h;
                offset = sc.candidates[static_cast<std::size_t>(x.source)].maint_offset_h;
            }
            if (period <= 0) continue;
            a.push_back(offset);
            m.push_back(period);
            names += " " + x.name + "(h=" + std::to_string(offset) + " mod " + std::to_string(period) + ")";
        }
        if (a.size() >= 2) {
            CrtResult r = chinese_remainder(a, m);
            o << "    maintenance windows:" << names << "\n";
            if (r.status == CrtStatus::Solved)
                o << "    CRT: all " << a.size() << " go down together first at hour " << r.x << ", then every " << r.modulus << " h ("
                  << f(static_cast<double>(r.modulus) / 24.0, 1) << " days)\n";
            else if (r.status == CrtStatus::Inconsistent)
                o << "    CRT: the windows never coincide (inconsistent congruences)\n";
            else o << "    CRT: invalid input or overflow\n";
            jint("crt_result", r.status == CrtStatus::Solved ? 1 : r.status == CrtStatus::Inconsistent ? 0 : -1);
            jint("crt_first_hour", r.status == CrtStatus::Solved ? r.x : -1);
            jint("crt_period", r.status == CrtStatus::Solved ? r.modulus : -1);
        }
        if (!sc.events.empty()) {
            if (j) j->begin_array("matching");
            for (const char *pat : {"FOR", "FFFF"}) {
                MatchStats s1, s2, s3, s4;
                double tm[4];
                w.restart();
                std::size_t h1 = match_naive(sc.events, pat, &s1).size();
                tm[0] = w.elapsed_ms();
                w.restart();
                std::size_t h2 = match_rabin_karp(sc.events, pat, &s2).size();
                tm[1] = w.elapsed_ms();
                w.restart();
                std::size_t h3 = match_automaton(sc.events, pat, &s3).size();
                tm[2] = w.elapsed_ms();
                w.restart();
                std::size_t h4 = match_kmp(sc.events, pat, &s4).size();
                tm[3] = w.elapsed_ms();
                const bool agree = h1 == h2 && h2 == h3 && h3 == h4;
                o << "    pattern \"" << pat << "\": " << h1 << " hits | naive " << s1.comparisons << " cmp " << f(tm[0], 3)
                  << " ms | Rabin-Karp " << s2.comparisons << " cmp " << f(tm[1], 3) << " ms | automaton " << s3.comparisons << " steps "
                  << f(tm[2], 3) << " ms | KMP " << s4.comparisons << " cmp " << f(tm[3], 3) << " ms -> " << (agree ? "agree" : "DISAGREE") << "\n";
                if (j) {
                    j->begin_object();
                    j->string("pattern", pat);
                    j->integer("hits", static_cast<std::int64_t>(h1));
                    j->boolean("agree", agree);
                    j->integer("naive_cmp", static_cast<std::int64_t>(s1.comparisons));
                    j->integer("rk_cmp", static_cast<std::int64_t>(s2.comparisons));
                    j->integer("fa_steps", static_cast<std::int64_t>(s3.comparisons));
                    j->integer("kmp_cmp", static_cast<std::int64_t>(s4.comparisons));
                    j->end_object();
                }
            }
            if (j) j->end_array();
        }
    }
    jend();
    jend();
}

} // namespace ev
