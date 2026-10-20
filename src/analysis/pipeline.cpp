// pipeline.cpp - the planning run used by every front end.
#include "ev/analysis/pipeline.hpp"

#include "ev/core/timer.hpp"

namespace ev {

ScenarioContext::ScenarioContext(const Scenario &s) : scenario(s), model(s) {
    Stopwatch w;
    benefits = candidate_benefits(s, model);
    benefit_ms = w.elapsed_ms();
}

Kpi compute_kpi(const std::vector<TripPlan> &plans) {
    Kpi k;
    k.trips = static_cast<int>(plans.size());
    int with_short = 0;
    for (const TripPlan &p : plans) {
        if (p.needs_charge && p.status != PlanStatus::NoRoadConnection) k.need_charge++;
        if (!p.feasible()) { k.infeasible++; continue; }
        k.feasible++;
        if (p.needs_charge) k.charged_ok++;
        k.avg_km += p.distance_km;
        k.avg_kwh += p.energy_kwh;
        k.avg_stops += static_cast<double>(p.stops.size());
        k.avg_charge_min += p.charge_min;
        k.avg_travel_min += p.arrive_min - p.depart_min;
        if (p.shortest_km >= 0) { k.avg_detour_km += p.distance_km - p.shortest_km; with_short++; }
    }
    if (k.feasible) {
        k.avg_km /= k.feasible;
        k.avg_kwh /= k.feasible;
        k.avg_stops /= k.feasible;
        k.avg_charge_min /= k.feasible;
        k.avg_travel_min /= k.feasible;
    }
    if (with_short) k.avg_detour_km /= with_short;
    k.feasible_pct = k.trips ? 100.0 * k.feasible / k.trips : 0.0;
    return k;
}

PlanningRun::PlanningRun(ScenarioContext &ctx, const RunOptions &options) : ctx_(ctx), options_(options) {
    const Scenario &s = ctx.scenario;
    placement = place_stations(s, ctx.benefits.benefit, options.placement, options.constraints, options.budget);
    ms_place = placement.ms;
    sites = make_charger_sites(s, placement.chosen, options.ports);
    Stopwatch w;
    router_ = std::make_unique<Router>(ctx.model, options.routing);
    planner_ = std::make_unique<LegPlanner>(s, *router_, sites);
    ms_router = w.elapsed_ms();
    w.restart();
    plans.reserve(s.trips.size());
    for (const TripRequest &t : s.trips) plans.push_back(planner_->plan(t));
    ms_plan = w.elapsed_ms();
    kpi = compute_kpi(plans);
}

int PlanningRun::site_of_station(int station_index) const {
    for (std::size_t k = 0; k < sites.size(); k++)
        if (sites[k].kind == ChargerSite::Kind::Existing && sites[k].source == station_index) return static_cast<int>(k);
    return -1;
}

int PlanningRun::site_of_candidate(int candidate_index) const {
    for (std::size_t k = 0; k < sites.size(); k++)
        if (sites[k].kind == ChargerSite::Kind::New && sites[k].source == candidate_index) return static_cast<int>(k);
    return -1;
}

} // namespace ev
