// report.hpp - exports (CSV/JSON), the run summary, the strategy comparison and the analysis
// report that exercises every syllabus algorithm on a loaded scenario.
//
// The analysis report's section on illustrative uses (CRT for maintenance windows, the celebrity
// problem for an overflow hub, Strassen for zone-to-zone demand, ...) is labelled as such: these
// algorithms are not needed by the core planning workflow and are shown on real scenario data for
// teaching. Pure textbook demonstrations live in ev/analysis/demos.hpp.
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "ev/analysis/pipeline.hpp"
#include "ev/io/json_writer.hpp"
#include "ev/operations/capacity.hpp"
#include "ev/operations/sessions.hpp"

namespace ev {

void export_placement_csv(const std::string &path, const PlanningRun &run);
void export_plans_csv(const std::string &path, const PlanningRun &run);
void export_summary_json(const std::string &path, const PlanningRun &run, const CapacityResult *cap,
                         const SessionAnalysis *ses);

void print_run(std::ostream &out, const PlanningRun &run, const CapacityResult *cap, const SessionAnalysis *ses);
// Explains one trip plan in plain language (used by `ev33 plan`).
void print_plan(std::ostream &out, const Scenario &s, const TripPlan &p);

struct CompareRow {
    PlacementMethod placement;
    RoutingMethod routing;
    int stations = 0;            // built candidates
    Paise cost = 0;
    std::int64_t benefit = 0;
    bool optimal = false;
    double place_ms = 0;
    std::uint64_t search_nodes = 0;
    int feasible = 0;
    double feasible_pct = 0, avg_km = 0, avg_kwh = 0, route_ms = 0;
    std::uint64_t trees = 0;
};
// Every placement x routing pairing (Floyd-Warshall skipped above 1,600 nodes). Writes a table to
// `out` (may be null) and a CSV to csv_path (when not empty).
std::vector<CompareRow> compare_strategies(ScenarioContext &ctx, std::ostream *out, const std::string &csv_path = "");

// Runs the full analysis; text to `out`, JSON (same content) to `json` when not null.
void analyze(ScenarioContext &ctx, std::ostream &out, JsonWriter *json);

} // namespace ev
