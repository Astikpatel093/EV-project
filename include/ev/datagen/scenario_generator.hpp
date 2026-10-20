// scenario_generator.hpp - deterministic SYNTHETIC scenarios for the application and the tests.
//
// Profiles:
//   small        6 x 6 grid city, 3 existing stations, 12 candidate sites, 60 trips (hand-checkable)
//   medium       16 x 16 grid city, 10 existing, 60 candidates, 1,200 trips (normal operation)
//   shock        medium + 45 % budget cut, 30 % of ports out, river bridges closed, +60 % evening demand
//   large        32 x 32 grid city, 30 existing, 220 candidates, 6,000 trips (stress)
//   adversarial  2 x 200 corridor, equal costs, identical departure times, fault-burst event log
//
// Everything is invented: node coordinates are placed around latitude 0, longitude 0 (open sea),
// deliberately, so the data cannot be mistaken for a real city; vehicle models are generic
// descriptions, not real products. Every file begins with a "# SYNTHETIC" comment.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ev/models/scenario.hpp"

namespace ev {

std::vector<std::string> scenario_profiles();   // the names above
// Throws std::invalid_argument for an unknown profile.
Scenario generate_scenario(const std::string &profile, std::uint64_t seed);
// The comment lines written at the top of every generated file.
std::vector<std::string> synthetic_banner(const std::string &profile, std::uint64_t seed);

} // namespace ev
