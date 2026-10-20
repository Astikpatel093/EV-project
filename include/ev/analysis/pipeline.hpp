// pipeline.hpp - one planning run: benefits -> placement -> charger sites -> route every trip.
//
// ScenarioContext holds what does not change between runs on the same scenario (the energy
// model and the candidate benefits). PlanningRun owns the results of one run so the user
// interfaces, the CLI and the experiment runner all use exactly the same code path.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "ev/placement/placement.hpp"
#include "ev/planner/energy_model.hpp"
#include "ev/planner/leg_planner.hpp"
#include "ev/planner/router.hpp"

namespace ev {

struct ScenarioContext {
    explicit ScenarioContext(const Scenario &s);
    const Scenario &scenario;
    EnergyModel model;
    BenefitTable benefits;
    double benefit_ms = 0;
};

struct RunOptions {
    PlacementMethod placement = PlacementMethod::DynamicProgramming;
    RoutingMethod routing = RoutingMethod::Dijkstra;
    PlacementConstraints constraints;
    std::map<int, int> ports;           // station/candidate id -> port count (what-if)
    std::optional<Paise> budget;        // overrides the scenario budget
};

// Service indicators over all trips of a run.
struct Kpi {
    int trips = 0;
    int feasible = 0;          // trips with a feasible plan (direct, charging or zero-length)
    int need_charge = 0;       // trips whose shortest route is not drivable on the starting charge
    int charged_ok = 0;        // of those, trips made feasible by charging
    int infeasible = 0;
    double feasible_pct = 0;
    double avg_km = 0, avg_kwh = 0, avg_stops = 0, avg_charge_min = 0, avg_detour_km = 0, avg_travel_min = 0;
};
Kpi compute_kpi(const std::vector<TripPlan> &plans);

class PlanningRun {
public:
    PlanningRun(ScenarioContext &ctx, const RunOptions &options);

    const Scenario &scenario() const { return ctx_.scenario; }
    ScenarioContext &context() { return ctx_; }
    const RunOptions &options() const { return options_; }

    PlacementResult placement;
    std::vector<ChargerSite> sites;      // existing stations + built candidates
    std::vector<TripPlan> plans;         // one per scenario trip, same order
    Kpi kpi;
    double ms_place = 0, ms_router = 0, ms_plan = 0;

    Router &router() { return *router_; }
    LegPlanner &planner() { return *planner_; }
    // site index of an existing station / built candidate, -1 if not part of this run
    int site_of_station(int station_index) const;
    int site_of_candidate(int candidate_index) const;

private:
    ScenarioContext &ctx_;
    RunOptions options_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<LegPlanner> planner_;
};

} // namespace ev
