// leg_planner.hpp - fast fleet planner on the "charging-leg graph".
//
// For one trip, build a small complete graph whose vertices are the origin, every charger site the
// vehicle can use, and the destination. An arc a -> b exists when the vehicle can drive the
// router's road path from a to b without falling below its reserve - starting with its initial
// charge at the origin, or with the target charge at a charger. Dijkstra on this graph (dense,
// O(K^2) for K = sites + 2), minimising (metres, stops), gives the plan.
//
// Each leg is tried along at most two paths: the router's path (km-shortest for Dijkstra/Floyd,
// energy-minimal for Bellman-Ford) and, when the vehicle cannot drive that one, the energy-minimal
// path (Bellman-Ford on the reference-energy graph, trees cached per source). Legs between chargers
// are computed once per run and their feasibility once per vehicle, so planning thousands of trips
// is fast. The planner is SOUND - every plan it returns is checked in exact arithmetic - but NOT
// COMPLETE: with only two candidate paths per leg it can miss a feasible trip (or use a stop) that
// the ExactPlanner avoids. The tests and the `verify` command measure how often that happens.
#pragma once

#include <memory>
#include <vector>

#include "ev/planner/router.hpp"
#include "ev/planner/trip_plan.hpp"

namespace ev {

class LegPlanner {
public:
    LegPlanner(const Scenario &scenario, Router &router, std::vector<ChargerSite> sites);

    TripPlan plan(const TripRequest &trip);
    TripPlan plan(int from_pos, int to_pos, const ElectricVehicle &v, double start_kwh, double depart_min, int trip_id = -1);

    const std::vector<ChargerSite> &sites() const { return sites_; }
    Router &router() { return router_; }

private:
    struct Path {
        bool reachable = false;
        std::vector<int> arcs;
        std::int64_t metres = 0;
    };
    struct Leg {
        Path main;            // the router's path
        Path alt;             // energy-minimal path, computed when first needed
        bool alt_done = false;
    };
    const Leg &site_leg(int i, int j);
    const Path &site_leg_alt(int i, int j);
    // which path of site i -> site j vehicle vi can drive from the target charge: 0 none, 1 main, 2 alt
    int site_leg_ok(int vi, int i, int j, const ElectricVehicle &v);
    // a drivable path a -> b from `start_kwh`: the router's path, else the energy-minimal one
    bool drivable(int a, int b, double start_kwh, const ElectricVehicle &v, Path &out);
    Router &energy_router();

    const Scenario &sc_;
    Router &router_;
    std::vector<ChargerSite> sites_;
    std::vector<Leg> legs_;                 // S x S, filled lazily
    std::vector<char> leg_done_;
    std::vector<std::vector<signed char>> leg_ok_;   // per vehicle: -1 unknown, 0 no, 1 main path, 2 alternative
    std::unique_ptr<Router> energy_;                 // Bellman-Ford router for the alternative paths
};

} // namespace ev
