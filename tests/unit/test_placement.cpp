// Unit tests: budgeted station placement (0/1 knapsack on candidate sites).
#include <gtest/gtest.h>

#include <stdexcept>

#include "ev/core/rng.hpp"
#include "ev/placement/placement.hpp"
#include "ev/planner/energy_model.hpp"
#include "../test_support.hpp"

using namespace ev;

namespace {

std::vector<int> ids(const Scenario &s, const PlacementResult &r) {
    std::vector<int> out;
    for (int i : r.chosen) out.push_back(s.candidates[static_cast<std::size_t>(i)].id);
    return out;
}

std::vector<std::int64_t> given(const Scenario &s) {
    std::vector<std::int64_t> b;
    for (const CandidateStation &c : s.candidates) b.push_back(c.benefit.value_or(0));
    return b;
}

Scenario with_candidates(const std::vector<std::pair<long, std::int64_t>> &cost_lakh_benefit, long budget_lakh) {
    Scenario s = evtest::sample();
    s.candidates.clear();
    int id = 900;
    for (const auto &cb : cost_lakh_benefit) {
        CandidateStation c;
        c.id = id++;
        c.node = 2;
        c.install_cost = static_cast<Paise>(cb.first) * kPaisePerLakh;
        c.benefit = cb.second;
        c.power_kw = 50;
        c.ports = 2;
        s.candidates.push_back(c);
    }
    s.config.budget = static_cast<Paise>(budget_lakh) * kPaisePerLakh;
    return s;
}

} // namespace

TEST(Placement, KnownOptimalSolutionOnTheSample) {
    Scenario s = evtest::sample();
    for (PlacementMethod m : {PlacementMethod::DynamicProgramming, PlacementMethod::BranchBoundLC, PlacementMethod::BranchBoundFIFO,
                              PlacementMethod::BranchBoundLIFO}) {
        PlacementResult r = place_stations(s, given(s), m);
        EXPECT_EQ(ids(s, r), (std::vector<int>{501, 502, 506})) << to_string(m);
        EXPECT_EQ(r.benefit, 155);
        EXPECT_EQ(r.cost, s.config.budget);   // a selection whose cost exactly equals the budget
        EXPECT_TRUE(r.optimal);
    }
}

TEST(Placement, GreedyIsNotOptimalHere) {
    Scenario s = evtest::sample();
    PlacementResult g = place_stations(s, given(s), PlacementMethod::Greedy);
    EXPECT_EQ(ids(s, g), (std::vector<int>{501, 502, 504}));
    EXPECT_EQ(g.benefit, 140);
    EXPECT_FALSE(g.optimal);
    EXPECT_LE(g.cost, s.config.budget);
}

TEST(Placement, ZeroBudget) {
    Scenario s = evtest::sample();
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming, {}, Paise(0));
    EXPECT_TRUE(r.chosen.empty());
    EXPECT_EQ(r.cost, 0);
}

TEST(Placement, NoCandidates) {
    Scenario s = with_candidates({}, 100);
    PlacementResult r = place_stations(s, {}, PlacementMethod::DynamicProgramming);
    EXPECT_TRUE(r.chosen.empty());
    EXPECT_EQ(r.benefit, 0);
}

TEST(Placement, AllCandidatesAffordable) {
    Scenario s = evtest::sample();
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming, {}, Paise(1000) * kPaisePerLakh);
    EXPECT_EQ(r.chosen.size(), 6u);
}

TEST(Placement, NoCandidateAffordable) {
    Scenario s = evtest::sample();
    for (PlacementMethod m : {PlacementMethod::DynamicProgramming, PlacementMethod::Greedy, PlacementMethod::BranchBoundLC}) {
        PlacementResult r = place_stations(s, given(s), m, {}, Paise(10) * kPaisePerLakh);   // cheapest costs 20 lakh
        EXPECT_TRUE(r.chosen.empty());
    }
}

TEST(Placement, EqualBenefitsAndEqualCosts) {
    Scenario a = with_candidates({{30, 50}, {20, 50}, {20, 50}, {40, 50}}, 50);
    PlacementResult ra = place_stations(a, given(a), PlacementMethod::DynamicProgramming);
    EXPECT_EQ(ra.benefit, 100);   // two sites fit (20 + 20 or 30 + 20)
    Scenario b = with_candidates({{25, 10}, {25, 40}, {25, 30}, {25, 40}}, 50);
    PlacementResult rb = place_stations(b, given(b), PlacementMethod::DynamicProgramming);
    EXPECT_EQ(rb.benefit, 80);
    EXPECT_EQ(ids(b, rb), ids(b, place_stations(b, given(b), PlacementMethod::DynamicProgramming)));   // deterministic
}

TEST(Placement, ZeroBenefitSitesAreNeverBuilt) {
    Scenario s = with_candidates({{10, 0}, {10, 5}}, 100);
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming);
    EXPECT_EQ(ids(s, r), std::vector<int>{901});
}

TEST(Placement, InvalidInputIsRejected) {
    Scenario s = evtest::sample();
    std::vector<std::int64_t> b = given(s);
    b[0] = -1;
    EXPECT_THROW(place_stations(s, b, PlacementMethod::DynamicProgramming), std::invalid_argument);   // negative benefit
    EXPECT_THROW(place_stations(s, {1, 2}, PlacementMethod::DynamicProgramming), std::invalid_argument);   // size mismatch
    EXPECT_THROW(place_stations(s, given(s), PlacementMethod::DynamicProgramming, {}, Paise(-1)), std::invalid_argument);
    s.candidates[0].install_cost = -5;
    EXPECT_THROW(place_stations(s, given(s), PlacementMethod::DynamicProgramming), std::invalid_argument);   // negative cost
    PlacementConstraints bad;
    bad.locked = {42};
    EXPECT_THROW(place_stations(evtest::sample(), given(s), PlacementMethod::DynamicProgramming, bad), std::invalid_argument);
}

TEST(Placement, LockedAndBannedSites) {
    Scenario s = evtest::sample();
    PlacementConstraints c;
    c.locked = {2};   // 503 must be built (50 lakh)
    c.banned = {0};   // 501 must not be built
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming, c);
    std::vector<int> got = ids(s, r);
    EXPECT_NE(std::find(got.begin(), got.end(), 503), got.end());
    EXPECT_EQ(std::find(got.begin(), got.end(), 501), got.end());
    EXPECT_LE(r.cost, s.config.budget);
    EXPECT_EQ(r.benefit, 70 + 50 + 30);   // 503 + best of the rest in 50 lakh: 502 + 504
    PlacementConstraints over;
    over.locked = {2, 4};   // 50 + 60 = 110 lakh > 100
    PlacementResult o = place_stations(s, given(s), PlacementMethod::DynamicProgramming, over);
    EXPECT_TRUE(o.over_budget);
    EXPECT_EQ(o.chosen.size(), 2u);
}

TEST(Placement, BudgetIsNeverExceededOnRandomInstances) {
    Rng rng(71);
    for (int trial = 0; trial < 150; trial++) {
        std::vector<std::pair<long, std::int64_t>> cb;
        const int n = static_cast<int>(rng.range(0, 25));
        for (int i = 0; i < n; i++) cb.push_back({static_cast<long>(rng.range(1, 90)), rng.range(0, 200)});
        Scenario s = with_candidates(cb, static_cast<long>(rng.range(0, 400)));
        const PlacementResult dp = place_stations(s, given(s), PlacementMethod::DynamicProgramming);
        for (int m = 0; m < kPlacementMethods; m++) {
            PlacementResult r = place_stations(s, given(s), static_cast<PlacementMethod>(m));
            EXPECT_LE(r.cost, s.config.budget);
            if (r.optimal) EXPECT_EQ(r.benefit, dp.benefit);
            EXPECT_LE(r.benefit, dp.benefit);
        }
    }
}

TEST(Placement, ExactWithCostsInRupeesAndPaise) {
    Scenario s = with_candidates({{1, 10}}, 3);
    s.candidates[0].install_cost = parse_rupees("100000.25");
    CandidateStation c2 = s.candidates[0];
    c2.id = 950;
    c2.install_cost = parse_rupees("200000.50");
    c2.benefit = 25;
    s.candidates.push_back(c2);
    s.config.budget = parse_rupees("300000.75");   // exactly the sum: both fit
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming);
    EXPECT_EQ(r.benefit, 35);
    EXPECT_EQ(r.cost, s.config.budget);
    s.config.budget -= 1;   // one paisa short: only one fits
    EXPECT_EQ(place_stations(s, given(s), PlacementMethod::DynamicProgramming).benefit, 25);
}

TEST(Placement, LargeTableFallsBackToBranchAndBound) {
    // co-prime costs in paise give a DP table far too large; the solver says so and stays exact
    Scenario s = with_candidates({{1, 10}, {1, 20}, {1, 30}}, 1);
    s.candidates[0].install_cost = 100000007;
    s.candidates[1].install_cost = 100000037;
    s.candidates[2].install_cost = 100000039;
    s.config.budget = 300000000;
    PlacementResult r = place_stations(s, given(s), PlacementMethod::DynamicProgramming);
    EXPECT_NE(r.note.find("branch-and-bound"), std::string::npos);
    EXPECT_TRUE(r.optimal);
    EXPECT_EQ(r.benefit, 50);
    EXPECT_LE(r.cost, s.config.budget);
}

TEST(Placement, BenefitsAreEstimatedFromTripsWhenMissing) {
    Scenario s = evtest::sample();
    for (CandidateStation &c : s.candidates) c.benefit.reset();
    EnergyModel m(s);
    BenefitTable t = candidate_benefits(s, m);
    for (bool e : t.estimated) EXPECT_TRUE(e);
    // node 2 lies on the shortest routes of trips 2 and 7? no: shortest routes cross the pass;
    // node 4 lies on trips 2, 4, 5 and 6 -> a positive estimate; the island-free hill node 6 too
    EXPECT_GT(t.benefit[1], 0);   // candidate 502 at node 4
    EXPECT_GT(t.benefit[4], 0);   // candidate 505 at node 6 (Hill Pass)
}
