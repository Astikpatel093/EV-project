// exact_planner.hpp - energy-feasible trip planning as a shortest path over (node, battery) states.
//
// A plain shortest-distance route may be impossible for a given vehicle. This planner searches
// a state graph whose vertices are (road node, battery level):
//   * driving arc u->v moves from (u, b) to (v, b - energy), allowed only if the new level stays
//     at or above the vehicle's reserve (a full battery cannot store more regenerated energy);
//   * at a node with a usable charger, "charge" moves from (u, b) to (u, target level) and adds a stop.
// Dijkstra on this graph with cost (total metres, number of stops), compared lexicographically,
// returns the SHORTEST-DISTANCE feasible route under the model (fewest stops among equals).
//
// Battery levels are discretised in steps of `battery_step_kwh` (default 0.25 kWh). Energy use is
// rounded UP to whole steps and regenerated energy rounded DOWN, so the discrete level never exceeds
// the true level: every plan returned is truly feasible (re-checked in exact arithmetic), but a
// route whose margin is smaller than about one step per road may be rejected. A smaller step
// reduces that gap and increases work: O(S log S) with S = nodes × (capacity / step) states.
// Charging policy: a stop fills the battery to target_soc × capacity (charging less is not modelled).
#pragma once

#include <cstdint>
#include <vector>

#include "ev/graph/graph.hpp"
#include "ev/planner/energy_model.hpp"
#include "ev/planner/trip_plan.hpp"

namespace ev {

class ExactPlanner {
public:
    ExactPlanner(const Scenario &scenario, const EnergyModel &model, std::vector<ChargerSite> sites);

    TripPlan plan(const TripRequest &trip);
    TripPlan plan(int from_pos, int to_pos, const ElectricVehicle &v, double start_kwh, double depart_min, int trip_id = -1);

    // Charger sites (indices into sites()) usable by v and reachable from `from_pos` on the
    // starting charge without charging on the way.
    std::vector<int> reachable_sites(int from_pos, const ElectricVehicle &v, double start_kwh);

    const std::vector<ChargerSite> &sites() const { return sites_; }

    struct Stats {
        std::uint64_t states_settled = 0;
        std::uint64_t states_pushed = 0;
    };
    const Stats &last_stats() const { return stats_; }

private:
    struct SearchResult {
        bool found = false;
        std::int64_t goal = -1;                 // goal state
        std::vector<char> node_reached;         // filled when exploring without a goal
    };
    SearchResult search(int from, int to, const ElectricVehicle &v, double start_kwh, bool allow_charging);
    void explain_failure(TripPlan &p, int from, int to, const ElectricVehicle &v, double start_kwh);

    const Scenario &sc_;
    const EnergyModel &model_;
    std::vector<ChargerSite> sites_;
    Graph km_graph_;
    Stats stats_;

    // search buffers reused between queries (a generation stamp avoids clearing them)
    std::vector<std::uint32_t> stamp_;
    std::vector<std::int64_t> metres_;
    std::vector<int> stops_;
    std::vector<std::int64_t> parent_;
    std::vector<int> action_;            // >= 0: arc id; < 0: -(site + 1) for a charge
    std::vector<char> settled_;
    std::uint32_t generation_ = 0;
    int levels_ = 0;                     // levels per node in the current buffers
};

} // namespace ev
