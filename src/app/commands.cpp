// commands.cpp - implementations of the ev33 commands.
#include "commands.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>

#include "ev/analysis/demos.hpp"
#include "ev/analysis/report.hpp"
#include "ev/core/errors.hpp"
#include "ev/core/timer.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/operations/capacity.hpp"
#include "ev/operations/sessions.hpp"
#include "ev/planner/exact_planner.hpp"

namespace ev {
namespace cli {

namespace {

Scenario load(const Options &o) {
    if (o.data.empty()) throw std::invalid_argument("--data DIR is required");
    return load_scenario(o.data);
}

std::string f(double v, int d) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", d, v);
    return b;
}

std::string clock_str(double m) {
    char b[16];
    int mm = static_cast<int>(std::floor(m + 0.5));
    std::snprintf(b, sizeof b, "%02d:%02d", (mm / 60) % 24, mm % 60);
    return b;
}

RunOptions run_options(const Options &o) {
    RunOptions r;
    r.placement = o.placement;
    r.routing = o.routing;
    r.budget = o.budget;
    return r;
}

void print_validate(const Scenario &s, std::ostream &out) {
    int offline = 0;
    for (const ChargingStation &st : s.stations) offline += st.status != StationStatus::Available;
    out << "ok: '" << s.config.name << "' (" << to_string(s.config.source) << " data) " << s.network.node_count() << " nodes, "
        << s.network.edge_count() << " roads, " << s.stations.size() << " existing stations (" << offline << " unavailable), "
        << s.candidates.size() << " candidate sites, " << s.vehicles.size() << " vehicles, " << s.trips.size() << " trips, "
        << s.sessions.size() << " booked sessions, budget " << format_inr(s.config.budget) << "\n";
    if (!s.config.source_note.empty()) out << "    source: " << s.config.source_note << "\n";
    const int comps = s.network.components();
    if (comps > 1) out << "    note: the road network has " << comps << " separate components\n";
}

} // namespace

int validate(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    print_validate(s, out);
    return 0;
}

int run(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    PlanningRun r(ctx, run_options(o));
    CapacityResult cap = analyze_capacity(r);
    SessionAnalysis ses = analyze_sessions(r);
    print_run(out, r, &cap, &ses);
    if (!o.out.empty()) {
        make_directories(o.out);
        export_placement_csv(o.out + "/placement.csv", r);
        export_plans_csv(o.out + "/plans.csv", r);
        export_summary_json(o.out + "/summary.json", r, &cap, &ses);
        out << "   exported placement.csv, plans.csv, summary.json to " << o.out << "\n";
    }
    return 0;
}

int plan(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    // which trip?
    TripRequest t;
    if (o.trip >= 0) {
        bool found = false;
        for (const TripRequest &x : s.trips)
            if (x.id == o.trip) { t = x; found = true; }
        if (!found) throw std::invalid_argument("trip " + std::to_string(o.trip) + " not found");
    } else {
        if (!o.from || !o.to || o.vehicle.empty()) throw std::invalid_argument("plan needs --trip ID, or --from NODE --to NODE --vehicle ID");
        t.id = 0;
        t.from = *o.from;
        t.to = *o.to;
        t.vehicle_id = o.vehicle;
        t.depart_min = o.depart_min;
        t.start_charge_kwh = o.charge_kwh;
        if (!s.network.has_node(t.from)) throw std::invalid_argument("unknown node " + std::to_string(t.from));
        if (!s.network.has_node(t.to)) throw std::invalid_argument("unknown node " + std::to_string(t.to));
        const int vi = s.vehicle_index(t.vehicle_id);
        if (vi < 0) throw std::invalid_argument("unknown vehicle '" + t.vehicle_id + "'");
        if (t.start_charge_kwh && (*t.start_charge_kwh < 0 || *t.start_charge_kwh > s.vehicles[static_cast<std::size_t>(vi)].battery_kwh))
            throw std::invalid_argument("--charge must be between 0 and the vehicle's battery capacity");
    }
    PlacementResult pl = place_stations(s, ctx.benefits.benefit, o.placement, {}, o.budget);
    std::vector<ChargerSite> sites = make_charger_sites(s, pl.chosen);
    const ElectricVehicle &v = s.vehicles[static_cast<std::size_t>(s.vehicle_index(t.vehicle_id))];
    out << "Vehicle " << v.id << " (" << v.model << "): " << f(v.battery_kwh, 1) << " kWh battery, " << f(v.consumption_kwh_per_km, 3)
        << " kWh/km, reserve " << f(v.reserve_kwh, 1) << " kWh, connector " << (v.connector.empty() ? "?" : v.connector) << "\n";
    out << "Chargers available to the planner: " << s.stations.size() << " existing + " << pl.chosen.size() << " new ("
        << to_string(o.placement) << " placement)\n\n";
    if (o.method == "exact" || o.method == "both") {
        ExactPlanner ex(s, ctx.model, sites);
        TripPlan p = ex.plan(t);
        out << "[exact planner: (node, battery) state search, battery step " << s.config.battery_step_kwh << " kWh, "
            << ex.last_stats().states_settled << " states settled]\n";
        print_plan(out, s, p);
    }
    if (o.method == "leg" || o.method == "both") {
        Router router(ctx.model, o.routing);
        LegPlanner lp(s, router, sites);
        TripPlan p = lp.plan(t);
        out << "[leg planner: charging-leg graph over " << to_string(o.routing) << " legs]\n";
        print_plan(out, s, p);
    }
    return 0;
}

int place(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    PlacementResult r = place_stations(s, ctx.benefits.benefit, o.placement, {}, o.budget);
    int estimated = 0;
    for (bool e : ctx.benefits.estimated) estimated += e;
    out << "Placement [" << to_string(r.method) << "] for '" << s.config.name << "': budget " << format_inr(r.budget) << ", "
        << s.candidates.size() << " candidates (" << estimated << " benefits estimated from trips)\n";
    out << "  chosen " << r.chosen.size() << " sites, total cost " << format_inr(r.cost) << ", total benefit " << r.benefit
        << (r.optimal ? " (optimal for the benefit scores)" : " (heuristic: not guaranteed optimal)") << "\n";
    out << "  budget check: " << (r.cost <= r.budget ? "within budget" : "OVER BUDGET") << ", " << format_inr(r.budget - r.cost) << " left\n";
    if (!r.note.empty()) out << "  note: " << r.note << "\n";
    for (int ci : r.chosen) {
        const CandidateStation &c = s.candidates[static_cast<std::size_t>(ci)];
        out << "    " << c.id << "  " << c.name << "  node " << c.node << "  " << format_inr_short(c.install_cost) << "  benefit "
            << ctx.benefits.benefit[static_cast<std::size_t>(ci)] << "  " << c.ports << " x " << f(c.power_kw, 0) << " kW\n";
    }
    if (!o.out.empty()) {
        make_directories(o.out);
        RunOptions ro = run_options(o);
        PlanningRun pr(ctx, ro);
        export_placement_csv(o.out + "/placement.csv", pr);
        out << "  exported placement.csv to " << o.out << "\n";
    }
    return 0;
}

int schedule(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    std::vector<int> ids;
    if (o.station) ids.push_back(*o.station);
    else
        for (const ChargingSession &x : s.sessions) {
            bool seen = false;
            for (int i : ids) seen = seen || i == x.station_id;
            if (!seen) ids.push_back(x.station_id);
        }
    if (ids.empty()) { out << "no booked sessions in " << o.data << "/sessions.csv\n"; return 0; }
    for (int id : ids) {
        StationSchedule r = schedule_station(s, id);
        out << "Station " << id << ": " << r.sessions.size() << " booked sessions, " << r.ports << " bays\n";
        out << "  one bay (activity selection, earliest finish first): " << r.one_bay.size() << " sessions can be served:";
        for (std::size_t k : r.one_bay) out << " #" << r.sessions[k].id << "(" << clock_str(r.sessions[k].start_min) << "-" << clock_str(r.sessions[k].end_min) << ")";
        out << "\n  every session (interval partitioning): " << r.bays.resources << " bays needed (maximum overlap " << r.depth << ")\n";
        out << "  with the " << r.ports << " installed bays: " << r.served_with_ports.size() << " of " << r.sessions.size()
            << " sessions served (activity selection repeated per bay)\n";
        if (r.bays.resources > r.ports) out << "  -> " << r.bays.resources - r.ports << " more bay(s) needed to serve every booking\n";
    }
    return 0;
}

int compare(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    std::string csv;
    if (!o.out.empty()) { make_directories(o.out); csv = o.out + "/compare.csv"; }
    compare_strategies(ctx, &out, csv);
    if (!csv.empty()) out << "   exported compare.csv to " << o.out << "\n";
    return 0;
}

int analyze(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    JsonWriter j;
    analyze(ctx, out, o.out.empty() ? nullptr : &j);
    if (!o.out.empty()) {
        make_directories(o.out);
        write_text_file(o.out + "/analysis.json", j.str());
        out << "\n   exported analysis.json to " << o.out << "\n";
    }
    return 0;
}

int verify(const Options &o, std::ostream &out) {
    Scenario s = load(o);
    ScenarioContext ctx(s);
    PlacementResult pl = place_stations(s, ctx.benefits.benefit, o.placement, {}, o.budget);
    std::vector<ChargerSite> sites = make_charger_sites(s, pl.chosen);
    ExactPlanner ex(s, ctx.model, sites);
    Router router(ctx.model, o.routing);
    LegPlanner lp(s, router, sites);
    int n = 0, both = 0, only_exact = 0, only_leg = 0, neither = 0, longer = 0, more_stops = 0;
    double extra_km = 0, ms_exact = 0, ms_leg = 0;
    const std::size_t step = s.trips.size() > static_cast<std::size_t>(o.samples) ? s.trips.size() / static_cast<std::size_t>(o.samples) : 1;
    for (std::size_t i = 0; i < s.trips.size() && n < o.samples; i += step, n++) {
        Stopwatch w;
        TripPlan a = ex.plan(s.trips[i]);
        ms_exact += w.elapsed_ms();
        w.restart();
        TripPlan b = lp.plan(s.trips[i]);
        ms_leg += w.elapsed_ms();
        if (a.feasible() && b.feasible()) {
            both++;
            if (b.distance_km > a.distance_km + 1e-6) { longer++; extra_km += b.distance_km - a.distance_km; }
            if (b.stops.size() > a.stops.size()) more_stops++;
        } else if (a.feasible()) only_exact++;
        else if (b.feasible()) only_leg++;
        else neither++;
    }
    out << "Planner cross-check on '" << s.config.name << "' (" << n << " trips, " << to_string(o.routing) << " legs, battery step "
        << s.config.battery_step_kwh << " kWh)\n";
    out << "  both feasible " << both << ", exact only " << only_exact << ", leg only " << only_leg << ", neither " << neither << "\n";
    out << "  leg plan longer than the exact plan: " << longer << " trips (" << f(extra_km, 1) << " km extra in total)\n";
    out << "  leg plan with more charging stops than the exact plan: " << more_stops << " trips\n";
    out << "  time: exact " << f(ms_exact, 1) << " ms, leg " << f(ms_leg, 1) << " ms\n";
    out << "  'exact only' = trips the leg planner misses because each leg is restricted to one path;\n"
           "  'leg only' = trips whose margin is below one battery step, which the exact planner rejects conservatively.\n";
    return 0;
}

int algorithms(std::ostream &out) { return run_algorithm_demos(out) == 0 ? 0 : 2; }

// ---------------------------------------------------------------- demonstration script
int demo(const Options &o, std::ostream &console) {
    const std::string root = o.data.empty() ? "data/scenarios" : o.data;
    const std::string outdir = o.out.empty() ? "results/demo" : o.out;
    make_directories(outdir);
    std::ostringstream log;
    auto say = [&](const std::string &title) {
        const std::string line = "\n######## " + title + "\n";
        console << line;
        log << line;
    };
    auto step = [&](const std::function<void(std::ostream &)> &fn) {
        std::ostringstream buf;
        fn(buf);
        console << buf.str();
        log << buf.str();
    };
    for (const char *name : {"medium", "shock", "large"}) {
        const std::string dir = root + "/" + name;
        say(std::string("SCENARIO ") + name + ": validate");
        Scenario s = load_scenario(dir);
        step([&](std::ostream &out) { print_validate(s, out); });
        ScenarioContext ctx(s);
        struct Strat { PlacementMethod p; RoutingMethod r; const char *dirname, *label; };
        const Strat strategies[2] = {
            {PlacementMethod::DynamicProgramming, RoutingMethod::Dijkstra, "dp_dijkstra", "strategy A = exact placement (DP) + Dijkstra legs"},
            {PlacementMethod::Greedy, RoutingMethod::BellmanFord, "greedy_bellman", "strategy B = greedy placement + energy-minimal Bellman-Ford legs"},
        };
        for (const Strat &st : strategies) {
            say(std::string("SCENARIO ") + name + ": " + st.label);
            step([&](std::ostream &out) {
                RunOptions ro;
                ro.placement = st.p;
                ro.routing = st.r;
                PlanningRun run(ctx, ro);
                CapacityResult cap = analyze_capacity(run);
                SessionAnalysis ses = analyze_sessions(run);
                print_run(out, run, &cap, &ses);
                const std::string od = outdir + "/" + name + "/" + st.dirname;
                make_directories(od);
                export_placement_csv(od + "/placement.csv", run);
                export_plans_csv(od + "/plans.csv", run);
                export_summary_json(od + "/summary.json", run, &cap, &ses);
                out << "   exported placement.csv, plans.csv, summary.json to " << od << "\n";
            });
        }
        say(std::string("SCENARIO ") + name + ": full strategy matrix (5 placement x 3 routing)");
        step([&](std::ostream &out) {
            make_directories(outdir + "/" + name);
            compare_strategies(ctx, &out, outdir + "/" + name + "/compare.csv");
            out << "   exported compare.csv to " << outdir << "/" << name << "\n";
        });
    }
    {
        say("Analysis report on the normal scenario (every algorithm on scenario data)");
        Scenario s = load_scenario(root + "/medium");
        ScenarioContext ctx(s);
        step([&](std::ostream &out) {
            JsonWriter j;
            analyze(ctx, out, &j);
            write_text_file(outdir + "/medium/analysis.json", j.str());
            out << "\n   exported analysis.json to " << outdir << "/medium\n";
        });
        say("Booked sessions: one bay (activity selection) vs every booking (interval partitioning)");
        step([&](std::ostream &out) {
            Options so;
            so.data = root + "/medium";
            schedule(so, out);
        });
    }
    {
        say("Single-trip explanation on the large map (a trip that needs two charging stops)");
        Scenario s = load_scenario(root + "/large");
        ScenarioContext ctx(s);
        RunOptions ro;
        PlanningRun run(ctx, ro);
        int pick = -1;
        for (std::size_t i = 0; i < run.plans.size() && pick < 0; i++)
            if (run.plans[i].stops.size() >= 2) pick = static_cast<int>(i);
        step([&](std::ostream &out) {
            if (pick < 0) { out << "no two-stop trip in this scenario\n"; return; }
            ExactPlanner ex(s, ctx.model, run.sites);
            out << "[leg planner]\n";
            print_plan(out, s, run.plans[static_cast<std::size_t>(pick)]);
            out << "[exact planner]\n";
            print_plan(out, s, ex.plan(s.trips[static_cast<std::size_t>(pick)]));
        });
    }
    write_text_file(outdir + "/transcript.txt", log.str());
    console << "\nDemo complete. Transcript: " << outdir << "/transcript.txt\n";
    return 0;
}

// ---------------------------------------------------------------- interactive menu
int menu(const Options &initial) {
    std::unique_ptr<Scenario> sc;
    std::string dir;
    Options o = initial;
    auto try_load = [&](const std::string &d) {
        try {
            auto s = std::make_unique<Scenario>(load_scenario(d));   // the old scenario stays if loading fails
            sc = std::move(s);
            dir = d;
            std::cout << "loaded " << d << "\n";
        } catch (const std::exception &e) {
            std::cout << "error: " << e.what() << "\n";
        }
    };
    if (!o.data.empty()) try_load(o.data);
    std::string line;
    for (;;) {
        std::cout << "\n==== EV33 menu ====  data: " << (sc ? dir : "(none)") << " | placement: " << to_string(o.placement)
                  << " | routing: " << to_string(o.routing) << "\n"
                  << " 1) load scenario   2) placement method   3) routing method\n"
                  << " 4) run scenario    5) compare strategies 6) plan a trip\n"
                  << " 7) analysis report 8) booked sessions    9) algorithm demos   q) quit\n> " << std::flush;
        if (!std::getline(std::cin, line) || line == "q" || line == "Q") break;
        try {
            if (line == "1") {
                std::cout << "scenario directory: " << std::flush;
                std::getline(std::cin, line);
                try_load(line);
                continue;
            }
            if (line == "2") {
                std::cout << "placement (greedy|dp|bnb-lc|bnb-fifo|bnb-lifo): " << std::flush;
                std::getline(std::cin, line);
                if (!parse_placement(line, o.placement)) std::cout << "unknown method\n";
                continue;
            }
            if (line == "3") {
                std::cout << "routing (dijkstra|floyd|bellman): " << std::flush;
                std::getline(std::cin, line);
                if (!parse_routing(line, o.routing)) std::cout << "unknown method\n";
                continue;
            }
            if (line == "9") { algorithms(std::cout); continue; }
            if (!sc) { std::cout << "load a scenario first (option 1)\n"; continue; }
            o.data = dir;
            if (line == "4") run(o, std::cout);
            else if (line == "5") compare(o, std::cout);
            else if (line == "6") {
                std::cout << "trip id: " << std::flush;
                std::getline(std::cin, line);
                Options p = o;
                p.trip = std::atoi(line.c_str());
                p.method = "both";
                plan(p, std::cout);
            } else if (line == "7") analyze(o, std::cout);
            else if (line == "8") schedule(o, std::cout);
            else std::cout << "unknown option\n";
        } catch (const std::exception &e) {
            std::cout << "error: " << e.what() << "\n";
        }
    }
    return 0;
}

} // namespace cli
} // namespace ev
