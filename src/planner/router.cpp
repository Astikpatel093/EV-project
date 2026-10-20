// router.cpp - cached shortest-path legs.
#include "ev/planner/router.hpp"

#include <stdexcept>

namespace ev {

const char *to_string(RoutingMethod m) {
    switch (m) {
    case RoutingMethod::Dijkstra: return "dijkstra";
    case RoutingMethod::FloydWarshall: return "floyd";
    default: return "bellman";
    }
}

bool parse_routing(const std::string &s, RoutingMethod &out) {
    if (s == "dijkstra") out = RoutingMethod::Dijkstra;
    else if (s == "floyd" || s == "floyd-warshall") out = RoutingMethod::FloydWarshall;
    else if (s == "bellman" || s == "bellman-ford") out = RoutingMethod::BellmanFord;
    else return false;
    return true;
}

Router::Router(const EnergyModel &model, RoutingMethod method)
    : model_(model), method_(method), graph_(model.node_count(), true) {
    // edge id k of graph_ == arc k of the energy model
    for (const RoadArc &a : model.arcs()) graph_.add_edge(a.from, a.to, method == RoutingMethod::BellmanFord ? a.energy_ref : a.km);
    if (method == RoutingMethod::FloydWarshall) {
        apsp_ = std::make_unique<AllPairsShortestPaths>(floyd_warshall(graph_));
        trees_ = static_cast<std::uint64_t>(model.node_count());
    } else {
        trees_cache_.resize(static_cast<std::size_t>(model.node_count()));
    }
}

const ShortestPathTree &Router::tree(int s) {
    auto &slot = trees_cache_[static_cast<std::size_t>(s)];
    if (!slot) {
        slot = std::make_unique<ShortestPathTree>(method_ == RoutingMethod::BellmanFord ? bellman_ford(graph_, s) : dijkstra(graph_, s));
        trees_++;
        if (slot->negative_cycle)   // impossible when regen_efficiency <= 1 (validated); be explicit anyway
            throw std::logic_error("negative energy cycle in the road network");
    }
    return *slot;
}

bool Router::leg(int a, int b, std::vector<int> &arcs) {
    arcs.clear();
    graph_.check_vertex(a);
    graph_.check_vertex(b);
    if (a == b) return true;
    if (method_ == RoutingMethod::FloydWarshall) {
        std::vector<int> p = apsp_->path(a, b);
        if (p.empty()) return false;
        for (std::size_t i = 0; i + 1 < p.size(); i++) {
            // cheapest arc between consecutive path vertices (parallel roads are possible)
            int best = -1;
            for (int k = model_.first(p[i]); k < model_.first(p[i] + 1); k++) {
                const RoadArc &r = model_.arcs()[static_cast<std::size_t>(k)];
                if (r.to == p[i + 1] && (best < 0 || r.km < model_.arcs()[static_cast<std::size_t>(best)].km)) best = k;
            }
            if (best < 0) return false;
            arcs.push_back(best);
        }
        return true;
    }
    const ShortestPathTree &t = tree(a);
    if (!t.reachable(b)) return false;
    arcs = t.edges_to(b);
    return true;
}

std::vector<int> Router::path(int a, int b) {
    std::vector<int> arcs, nodes;
    if (!leg(a, b, arcs)) return nodes;
    nodes.push_back(a);
    for (int k : arcs) nodes.push_back(model_.arcs()[static_cast<std::size_t>(k)].to);
    return nodes;
}

double Router::leg_km(int a, int b) {
    std::vector<int> arcs;
    if (!leg(a, b, arcs)) return -1;
    double km = 0;
    for (int k : arcs) km += model_.arcs()[static_cast<std::size_t>(k)].km;
    return km;
}

std::size_t Router::bytes() const {
    std::size_t b = 0;
    for (const auto &t : trees_cache_)
        if (t) b += t->dist.size() * (sizeof(double) + 2 * sizeof(int));
    if (apsp_) b += apsp_->dist.size() * (sizeof(double) + sizeof(int));
    return b;
}

} // namespace ev
