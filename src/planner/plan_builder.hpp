// plan_builder.hpp (private) - turns a chosen sequence of road arcs and charging stops into a
// TripPlan, re-checking the battery in exact (non-discretised) arithmetic.
#pragma once

#include <string>
#include <vector>

#include "ev/planner/energy_model.hpp"
#include "ev/planner/trip_plan.hpp"

namespace ev {
namespace detail {

// A step is either an arc (arc >= 0) or a charging stop at site `site` (arc == -1).
struct PlanStep {
    int arc = -1;
    int site = -1;
};

// Fills route, stops, totals and arrival; returns false if the exact battery check fails.
bool build_plan(TripPlan &p, const std::vector<PlanStep> &steps, int from_pos, const EnergyModel &model,
                const std::vector<ChargerSite> &sites, const ElectricVehicle &v, double start_kwh, double depart_min,
                double target_soc);

std::string fmt(double v, int decimals);
std::string node_label(const Scenario &s, int pos);

} // namespace detail
} // namespace ev
