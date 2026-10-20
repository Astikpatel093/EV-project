// router.hpp - road legs between two nodes using a chosen shortest-path algorithm.
//
//   Dijkstra       km-shortest path; one shortest-path tree per source node, built on demand and cached
//   FloydWarshall  km-shortest path; all pairs computed once (Θ(V^3), Θ(V^2) memory)
//   BellmanFord    energy-minimal path on the reference-energy graph (negative arcs allowed);
//                  one tree per source, cached
// The router runs the library algorithms from ev/graph/shortest_paths.hpp on a directed graph
// whose edge ids are EnergyModel arc ids, so every leg can be replayed arc by arc.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ev/graph/shortest_paths.hpp"
#include "ev/planner/energy_model.hpp"

namespace ev {

enum class RoutingMethod { Dijkstra, FloydWarshall, BellmanFord };
const char *to_string(RoutingMethod m);             // "dijkstra", "floyd", "bellman"
bool parse_routing(const std::string &s, RoutingMethod &out);

class Router {
public:
    Router(const EnergyModel &model, RoutingMethod method);

    RoutingMethod method() const { return method_; }
    const EnergyModel &model() const { return model_; }

    // Arc ids of the a -> b path (empty when a == b). Returns false when b is unreachable.
    bool leg(int a, int b, std::vector<int> &arcs);
    // Node positions along the path (a ... b); empty when unreachable.
    std::vector<int> path(int a, int b);
    // km along the path, or -1 when unreachable
    double leg_km(int a, int b);

    std::uint64_t trees_built() const { return trees_; }
    std::size_t bytes() const;

private:
    const ShortestPathTree &tree(int source);
    const EnergyModel &model_;
    RoutingMethod method_;
    Graph graph_;
    std::vector<std::unique_ptr<ShortestPathTree>> trees_cache_;
    std::unique_ptr<AllPairsShortestPaths> apsp_;
    std::uint64_t trees_ = 0;
};

} // namespace ev
