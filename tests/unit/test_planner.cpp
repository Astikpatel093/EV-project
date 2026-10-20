// Unit tests: energy model and trip planners on the hand-checkable sample (data/sample/README.md).
#include <gtest/gtest.h>

#include <stdexcept>

#include "ev/planner/energy_model.hpp"
#include "ev/planner/exact_planner.hpp"
#include "ev/planner/leg_planner.hpp"
#include "../test_support.hpp"

using namespace ev;

namespace {

struct SampleFixture : ::testing::Test {
    Scenario s = evtest::sample();
    EnergyModel model{s};
    const ElectricVehicle &sv1() { return s.vehicles[0]; }
    int pos(int id) { return s.network.index_of(id); }
    // planner over the existing stations only (no candidates built)
    ExactPlanner exact() { return ExactPlanner(s, model, make_charger_sites(s, {})); }
    const TripRequest &trip(int id) {
        for (const TripRequest &t : s.trips)
            if (t.id == id) return t;
        throw std::logic_error("no such trip");
    }
};

// battery along the plan never drops below the reserve, recomputed from scratch
void expect_safe(const EnergyModel &m, const ElectricVehicle &v, const TripPlan &p, double target_soc) {
    double x = p.start_kwh;
    std::size_t next_stop = 0;
    for (std::size_t i = 0; i < p.route_arcs.size(); i++) {
        while (next_stop < p.stops.size() && p.stops[next_stop].route_pos == i) {
            x = std::max(x, target_soc * v.battery_kwh);
            next_stop++;
        }
        x = std::min(v.battery_kwh, x - m.energy(p.route_arcs[i], v));
        EXPECT_GE(x, v.reserve_kwh - 1e-9) << "below reserve after arc " << i;
    }
    EXPECT_NEAR(x, p.arrive_kwh, 1e-9);
}

} // namespace

TEST_F(SampleFixture, EnergyModelMatchesHandCalculation) {
    // flat road: 20 km x 0.2 kWh/km = 4 kWh; climb 600 m adds 7.2 kWh; descent recovers half
    double up = 0, down = 0, flat = 0;
    for (std::size_t k = 0; k < model.arcs().size(); k++) {
        const RoadArc &a = model.arcs()[k];
        if (a.from == pos(1) && a.to == pos(6)) up = model.energy(static_cast<int>(k), sv1());
        if (a.from == pos(6) && a.to == pos(3)) down = model.energy(static_cast<int>(k), sv1());
        if (a.from == pos(1) && a.to == pos(2)) flat = model.energy(static_cast<int>(k), sv1());
    }
    EXPECT_NEAR(flat, 4.0, 1e-12);
    EXPECT_NEAR(up, 10.2, 1e-12);
    EXPECT_NEAR(down, -0.6, 1e-12);
    // a vehicle with consumption 0.15 uses 0.75 of the reference energy
    EXPECT_NEAR(model.scale(s.vehicles[2]), 0.75, 1e-12);
}

TEST_F(SampleFixture, EnoughBatteryForTheWholeTrip) {
    TripPlan p = exact().plan(trip(6));   // SV1 4 -> 5, 20 km
    EXPECT_EQ(p.status, PlanStatus::Direct);
    EXPECT_NEAR(p.distance_km, 20, 1e-9);
    EXPECT_NEAR(p.arrive_kwh, 8.0, 1e-9);
    expect_safe(model, sv1(), p, s.config.target_soc);
}

TEST_F(SampleFixture, ShortestRouteIsNotAutomaticallyEnergyFeasible) {
    TripPlan p = exact().plan(trip(1));   // SV1 1 -> 3
    EXPECT_NEAR(p.shortest_km, 30, 1e-9);           // over the Hill Pass
    EXPECT_FALSE(p.shortest_is_feasible);           // 10.2 kWh climb, only 10 kWh usable
    EXPECT_EQ(p.status, PlanStatus::Direct);        // ...so the flat 40 km road is used
    EXPECT_NEAR(p.distance_km, 40, 1e-9);
    EXPECT_NEAR(p.arrive_kwh, 4.0, 1e-9);
    EXPECT_EQ(p.route, (std::vector<int>{pos(1), pos(2), pos(3)}));
    expect_safe(model, sv1(), p, s.config.target_soc);
}

TEST_F(SampleFixture, InsufficientBatteryWithoutChargers) {
    ExactPlanner none(s, model, {});                 // no chargers at all
    TripPlan p = none.plan(trip(2));                 // SV1 1 -> 5 needs 16 kWh, has 10 usable
    EXPECT_EQ(p.status, PlanStatus::OutOfRange);
    EXPECT_FALSE(p.feasible());
    EXPECT_NE(p.explanation.find("usable"), std::string::npos);
}

TEST_F(SampleFixture, TripRequiringCharging) {
    TripPlan p = exact().plan(trip(2));
    ASSERT_EQ(p.status, PlanStatus::WithCharging);
    ASSERT_EQ(p.stops.size(), 1u);
    EXPECT_EQ(p.stops[0].station_id, 101);           // Town Centre Charger
    EXPECT_NEAR(p.stops[0].arrive_kwh, 4.0, 1e-9);
    EXPECT_NEAR(p.stops[0].depart_kwh, 16.0, 1e-9);  // target 80 % of 20 kWh
    EXPECT_NEAR(p.stops[0].charge_min, 14.4, 1e-9);  // 12 kWh at 50 kW
    EXPECT_NEAR(p.distance_km, 80, 1e-9);
    EXPECT_NEAR(p.arrive_kwh, 8.0, 1e-9);
    EXPECT_NEAR(p.arrive_min, 540 + 96 + 14.4, 1e-9);
    expect_safe(model, sv1(), p, s.config.target_soc);
}

TEST_F(SampleFixture, ChargingStationsPresentButUnusableOrUnreachable) {
    TripPlan p = exact().plan(trip(7));   // 9.5 kWh: 103 reachable but CHAdeMO, 101 too far
    EXPECT_EQ(p.status, PlanStatus::OutOfRange);
    EXPECT_NE(p.explanation.find("unusable"), std::string::npos);
    EXPECT_TRUE(exact().reachable_sites(pos(1), sv1(), 9.5).empty());
}

TEST_F(SampleFixture, InfeasibleEvenWithAChargingStationElsewhere) {
    TripPlan p = exact().plan(trip(4));   // SV2 3 -> 5: one charge never covers 40 km
    EXPECT_EQ(p.status, PlanStatus::ChargingInsufficient);
    EXPECT_FALSE(p.feasible());
}

TEST_F(SampleFixture, BatteryEqualToTheReserve) {
    ExactPlanner ex = exact();
    TripPlan flat = ex.plan(pos(1), pos(2), sv1(), sv1().reserve_kwh, 0);    // any flat driving drops below
    EXPECT_EQ(flat.status, PlanStatus::OutOfRange);
    TripPlan down = ex.plan(pos(6), pos(3), sv1(), sv1().reserve_kwh, 0);    // downhill regenerates energy
    EXPECT_EQ(down.status, PlanStatus::Direct);
    EXPECT_NEAR(down.arrive_kwh, sv1().reserve_kwh + 0.6, 1e-9);
}

TEST_F(SampleFixture, BatteryChargeAboveCapacityIsRejected) {
    EXPECT_THROW(exact().plan(pos(1), pos(3), sv1(), sv1().battery_kwh + 1, 0), std::invalid_argument);
    EXPECT_THROW(exact().plan(pos(1), pos(3), sv1(), -1, 0), std::invalid_argument);
}

TEST_F(SampleFixture, ZeroLengthTrip) {
    TripPlan p = exact().plan(trip(8));
    EXPECT_EQ(p.status, PlanStatus::ZeroLength);
    EXPECT_TRUE(p.feasible());
    EXPECT_EQ(p.distance_km, 0);
    EXPECT_EQ(p.route, std::vector<int>{pos(3)});
}

TEST_F(SampleFixture, NoRoadConnection) {
    TripPlan p = exact().plan(trip(3));
    EXPECT_EQ(p.status, PlanStatus::NoRoadConnection);
    EXPECT_LT(p.shortest_km, 0);
}

TEST_F(SampleFixture, LegPlannerIsSoundButCanMissRoutes) {
    // trip 1: the km-shortest leg crosses the pass and is not drivable; the leg planner falls back
    // to the energy-minimal path and finds the flat road, like the exact planner
    Router dijkstra_legs(model, RoutingMethod::Dijkstra);
    LegPlanner leg(s, dijkstra_legs, make_charger_sites(s, {}));
    TripPlan p1 = leg.plan(trip(1));
    EXPECT_EQ(p1.status, PlanStatus::Direct);
    EXPECT_NEAR(p1.distance_km, 40, 1e-9);
    EXPECT_FALSE(p1.shortest_is_feasible);   // the shortest route itself is still reported as too costly
    Router energy_legs(model, RoutingMethod::BellmanFord);
    LegPlanner leg2(s, energy_legs, make_charger_sites(s, {}));
    EXPECT_EQ(leg2.plan(trip(1)).status, PlanStatus::Direct);
    TripPlan q = leg2.plan(trip(2));
    EXPECT_EQ(q.status, PlanStatus::WithCharging);
    expect_safe(model, sv1(), q, s.config.target_soc);

    // A network where neither the km-shortest path (over a 1,000 m peak) nor the energy-minimal path
    // (the same peak: the long descent recovers energy) can be driven, but a third, longer road can:
    // the leg planner, restricted to those two paths, misses the trip; the exact planner finds it.
    ScenarioTexts t;
    t.label = "three-roads";
    t.config = "name = three-roads\nbudget_inr = 100000\nreference_consumption = 0.2\nclimb_kwh_per_m = 0.012\nregen_efficiency = 0.5\nbattery_step_kwh = 0.1\n";
    t.nodes = "id,lat,lon,elev_m\n1,0,0,300\n2,0,0.09,1000\n3,0,0.18,0\n4,0.1,0.09,800\n5,-0.1,0.09,150\n";
    t.roads = "id,from,to,distance_km\n1,1,2,10\n2,2,3,10\n3,1,4,15\n4,4,3,15\n5,1,5,23\n6,5,3,23\n";
    t.vehicles = "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,20,10,0.2,2\n";
    t.trips = "id,vehicle,from,to\n1,V,1,3\n";
    Scenario h = parse_scenario(t);
    EnergyModel hm(h);
    Router hr(hm, RoutingMethod::Dijkstra);
    LegPlanner hleg(h, hr, {});
    ExactPlanner hex(h, hm, {});
    TripPlan missed = hleg.plan(h.trips[0]), found = hex.plan(h.trips[0]);
    EXPECT_FALSE(missed.feasible());                     // sound: no false "feasible"
    EXPECT_EQ(missed.status, PlanStatus::OutOfRange);
    EXPECT_EQ(found.status, PlanStatus::Direct);         // complete: the 46 km road via node 5
    EXPECT_NEAR(found.distance_km, 46, 1e-9);
    expect_safe(hm, h.vehicles[0], found, h.config.target_soc);
}

TEST_F(SampleFixture, BuiltCandidateMakesATripFeasible) {
    // build candidate 501 at Junction A: trip 7 becomes possible with one stop there
    ExactPlanner ex(s, model, make_charger_sites(s, {0}));
    TripPlan p = ex.plan(trip(7));
    ASSERT_EQ(p.status, PlanStatus::WithCharging);
    EXPECT_EQ(p.stops[0].station_id, 501);
    EXPECT_NEAR(p.distance_km, 40, 1e-9);
    expect_safe(model, sv1(), p, s.config.target_soc);
}

TEST_F(SampleFixture, OfflineStationIsNeverUsed) {
    for (const TripRequest &t : s.trips) {
        TripPlan p = exact().plan(t);
        for (const ChargingStop &st : p.stops) EXPECT_NE(st.station_id, 102);
    }
}

TEST_F(SampleFixture, DiscretisationIsConservative) {
    // with a coarse 1 kWh step the planner must still never return an unsafe plan
    Scenario coarse = s;
    coarse.config.battery_step_kwh = 1.0;
    EnergyModel m2(coarse);
    ExactPlanner ex(coarse, m2, make_charger_sites(coarse, {0, 1, 5}));
    for (const TripRequest &t : coarse.trips) {
        TripPlan p = ex.plan(t);
        if (p.feasible()) expect_safe(m2, coarse.vehicles[static_cast<std::size_t>(coarse.vehicle_index(t.vehicle_id))], p, coarse.config.target_soc);
    }
}
