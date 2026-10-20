// main.cpp - ev33 command-line application: argument parsing and dispatch.
//
//   ev33 validate  --data DIR
//   ev33 run       --data DIR [--place M] [--route R] [--budget INR] [--out DIR]
//   ev33 plan      --data DIR (--trip ID | --from NODE --to NODE --vehicle V [--charge KWH] [--depart HH:MM])
//                  [--method exact|leg|both] [--place M] [--route R]
//   ev33 place     --data DIR [--place M] [--budget INR] [--out DIR]
//   ev33 schedule  --data DIR [--station ID]
//   ev33 compare   --data DIR [--out DIR]
//   ev33 analyze   --data DIR [--out DIR]
//   ev33 verify    --data DIR [--samples N] [--route R]
//   ev33 algorithms
//   ev33 demo      [--data ROOT] [--out DIR]
//   ev33 menu      [--data DIR]
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "commands.hpp"
#include "ev/core/errors.hpp"
#include "ev/io/csv.hpp"

namespace {

const char *kVersion = "2.0.0";

void usage(std::ostream &o) {
    o << "ev33 " << kVersion << " - EV charging station placement and trip planning\n\n"
      << "usage:\n"
      << "  ev33 validate  --data DIR                               load and validate a scenario\n"
      << "  ev33 run       --data DIR [--place M] [--route R] [--budget INR] [--out DIR]\n"
      << "                                                          place stations, plan every trip, export CSV/JSON\n"
      << "  ev33 plan      --data DIR --trip ID [--method exact|leg|both]\n"
      << "  ev33 plan      --data DIR --from NODE --to NODE --vehicle V [--charge KWH] [--depart HH:MM]\n"
      << "                                                          plan and explain one trip\n"
      << "  ev33 place     --data DIR [--place M] [--budget INR] [--out DIR]   station placement only\n"
      << "  ev33 schedule  --data DIR [--station ID]                booked sessions: bays needed / sessions served\n"
      << "  ev33 compare   --data DIR [--out DIR]                   every placement x routing method\n"
      << "  ev33 analyze   --data DIR [--out DIR]                   every algorithm on the scenario\n"
      << "  ev33 verify    --data DIR [--samples N] [--route R]     leg planner vs exact planner\n"
      << "  ev33 algorithms                                         textbook demonstrations of every algorithm\n"
      << "  ev33 demo      [--data ROOT] [--out DIR]                the demonstration script\n"
      << "  ev33 menu      [--data DIR]                             interactive menu\n\n"
      << "  M = greedy | dp | bnb-lc | bnb-fifo | bnb-lifo   (default dp)\n"
      << "  R = dijkstra | floyd | bellman                   (default dijkstra)\n";
}

bool parse_clock(const std::string &s, double &minutes) {
    int h = 0, m = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 47 || m < 0 || m > 59) return false;
    minutes = h * 60 + m;
    return true;
}

// returns 0 on success, otherwise prints a message and returns 1
int parse(int argc, char **argv, ev::cli::Options &o) {
    if (argc < 2) { usage(std::cerr); return 1; }
    o.command = argv[1];
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        auto value = [&](const char *what) -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(a + " needs " + what);
            return argv[++i];
        };
        auto integer = [&](const char *what) {
            std::int64_t v;
            std::string s = value(what);
            if (!ev::parse_int64(s, v) || v < -2147483647LL || v > 2147483647LL) throw std::invalid_argument(a + " expects " + what + ", got '" + s + "'");
            return static_cast<int>(v);
        };
        if (a == "--data") o.data = value("a directory");
        else if (a == "--out") o.out = value("a directory");
        else if (a == "--place" || a == "--select") {
            std::string s = value("a placement method");
            if (!ev::parse_placement(s, o.placement)) throw std::invalid_argument("unknown placement method '" + s + "'");
        } else if (a == "--route") {
            std::string s = value("a routing method");
            if (!ev::parse_routing(s, o.routing)) throw std::invalid_argument("unknown routing method '" + s + "'");
        } else if (a == "--budget") {
            o.budget = ev::parse_rupees(value("an amount in rupees"));
        } else if (a == "--trip") o.trip = integer("a trip id");
        else if (a == "--from") o.from = integer("a node id");
        else if (a == "--to") o.to = integer("a node id");
        else if (a == "--station") o.station = integer("a station id");
        else if (a == "--samples") {
            o.samples = integer("a count");
            if (o.samples < 1) throw std::invalid_argument("--samples must be at least 1");
        } else if (a == "--vehicle") o.vehicle = value("a vehicle id");
        else if (a == "--charge") {
            double c;
            std::string s = value("kWh");
            if (!ev::parse_double(s, c)) throw std::invalid_argument("--charge expects a number of kWh, got '" + s + "'");
            o.charge_kwh = c;
        } else if (a == "--depart") {
            std::string s = value("a time HH:MM");
            if (!parse_clock(s, o.depart_min)) throw std::invalid_argument("--depart expects HH:MM, got '" + s + "'");
        } else if (a == "--method") {
            o.method = value("exact, leg or both");
            if (o.method != "exact" && o.method != "leg" && o.method != "both") throw std::invalid_argument("--method must be exact, leg or both");
        } else throw std::invalid_argument("unexpected argument '" + a + "'");
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    namespace cli = ev::cli;
    cli::Options o;
    try {
        if (parse(argc, argv, o)) return 1;
        const std::string &c = o.command;
        if (c == "help" || c == "--help" || c == "-h") { usage(std::cout); return 0; }
        if (c == "version" || c == "--version") { std::cout << "ev33 " << kVersion << "\n"; return 0; }
        if (c == "validate") return cli::validate(o, std::cout);
        if (c == "run") return cli::run(o, std::cout);
        if (c == "plan") return cli::plan(o, std::cout);
        if (c == "place") return cli::place(o, std::cout);
        if (c == "schedule") return cli::schedule(o, std::cout);
        if (c == "compare") return cli::compare(o, std::cout);
        if (c == "analyze") return cli::analyze(o, std::cout);
        if (c == "verify") return cli::verify(o, std::cout);
        if (c == "algorithms") return cli::algorithms(std::cout);
        if (c == "demo") return cli::demo(o, std::cout);
        if (c == "menu") return cli::menu(o);
        std::cerr << "error: unknown command '" << c << "'\n\n";
        usage(std::cerr);
        return 1;
    } catch (const ev::DataError &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::invalid_argument &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return 2;
    }
}
