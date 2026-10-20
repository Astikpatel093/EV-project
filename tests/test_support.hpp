// test_support.hpp - shared helpers for the EV33 test suites.
#pragma once

#include <cstdlib>
#include <string>

#include "ev/io/scenario_io.hpp"
#include "ev/models/scenario.hpp"

#ifndef EV33_SOURCE_DIR
#define EV33_SOURCE_DIR "."
#endif

namespace evtest {

// The project folder: the EV33_SOURCE_DIR environment variable when set (to run the test programs
// from another place or machine), else the folder CMake configured.
inline std::string source_root() {
    const char *env = std::getenv("EV33_SOURCE_DIR");
    return env && *env ? std::string(env) : std::string(EV33_SOURCE_DIR);
}
inline std::string source_path(const std::string &rel) { return source_root() + "/" + rel; }

// The hand-checkable dataset documented in data/sample/README.md.
inline ev::Scenario sample() { return ev::load_scenario(source_path("data/sample")); }

// Same files as data/sample, as text, so a test can change one file and parse again.
inline ev::ScenarioTexts sample_texts() {
    ev::ScenarioTexts t = ev::read_scenario_texts(source_path("data/sample"));
    t.label = "sample";
    return t;
}

// A minimal valid scenario: a straight road 1 -(10 km)- 2 -(10 km)- 3 and one vehicle.
inline ev::ScenarioTexts tiny_texts() {
    ev::ScenarioTexts t;
    t.label = "tiny";
    t.config = "name = tiny\nbudget_inr = 1000000\nreference_consumption = 0.2\n";
    t.nodes = "id,lat,lon\n1,0,0\n2,0,0.09\n3,0,0.18\n";
    t.roads = "id,from,to,distance_km\n1,1,2,10\n2,2,3,10\n";
    t.vehicles = "id,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh\nV,10,10,0.2,1\n";
    t.trips = "id,vehicle,from,to\n1,V,1,3\n";
    return t;
}

} // namespace evtest
