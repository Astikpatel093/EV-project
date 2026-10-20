// commands.hpp - the ev33 command-line commands (user-interface layer).
#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "ev/analysis/pipeline.hpp"

namespace ev {
namespace cli {

struct Options {
    std::string command;
    std::string data, out;
    PlacementMethod placement = PlacementMethod::DynamicProgramming;
    RoutingMethod routing = RoutingMethod::Dijkstra;
    std::optional<Paise> budget;
    int trip = -1;
    std::optional<int> from, to, station;
    std::string vehicle;
    std::optional<double> charge_kwh;
    double depart_min = 8 * 60;
    std::string method = "exact";   // plan: exact | leg | both
    int samples = 200;              // verify
};

// Each returns a process exit code (0 = success).
int validate(const Options &o, std::ostream &out);
int run(const Options &o, std::ostream &out);
int plan(const Options &o, std::ostream &out);
int place(const Options &o, std::ostream &out);
int schedule(const Options &o, std::ostream &out);
int compare(const Options &o, std::ostream &out);
int analyze(const Options &o, std::ostream &out);
int verify(const Options &o, std::ostream &out);
int algorithms(std::ostream &out);
int demo(const Options &o, std::ostream &out);
int menu(const Options &o);

} // namespace cli
} // namespace ev
