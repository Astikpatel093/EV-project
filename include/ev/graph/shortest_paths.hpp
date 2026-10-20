// shortest_paths.hpp - Dijkstra, Bellman-Ford and Floyd-Warshall.
//
//  dijkstra        greedy, binary heap with lazy deletion: O((V + E) log V). Requires every edge
//                  weight >= 0 and throws std::invalid_argument otherwise (it would silently give
//                  wrong answers with negative weights).
//  bellman_ford    dynamic programming over path length, O(V·E) (stops early when a round
//                  changes nothing). Negative weights allowed; reports a reachable negative cycle.
//  floyd_warshall  dynamic programming over intermediate vertices, Θ(V^3) time, Θ(V^2) memory.
//
// Unreachable vertices have distance +infinity and an empty path. Distances are doubles; if a
// path length would overflow to infinity (weights near 1e308) Dijkstra and Bellman-Ford throw
// std::overflow_error instead of returning a misleading "unreachable".
// Ties: when several paths are equally short, the one found first is kept (a later path must be
// strictly shorter to replace it); together with the heap's (distance, vertex) order this makes
// the returned path deterministic.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "ev/graph/graph.hpp"

namespace ev {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

struct ShortestPathTree {
    int source = -1;
    std::vector<double> dist;        // dist[v], kInfinity if unreachable
    std::vector<int> parent;         // previous vertex on the shortest path, -1 for source/unreachable
    std::vector<int> parent_edge;    // edge id used to reach v, -1 for source/unreachable
    bool negative_cycle = false;     // Bellman-Ford only: a negative cycle is reachable from source
    std::uint64_t relaxations = 0;   // successful distance improvements (work counter)
    int rounds = 0;                  // Bellman-Ford rounds executed

    bool reachable(int v) const;
    // vertices from source to v (inclusive); empty if v is unreachable; {source} if v == source
    std::vector<int> path_to(int v) const;
    std::vector<int> edges_to(int v) const;   // edge ids along path_to(v)
};

ShortestPathTree dijkstra(const Graph &g, int source);
ShortestPathTree bellman_ford(const Graph &g, int source);

struct AllPairsShortestPaths {
    int n = 0;
    std::vector<double> dist;   // row-major n x n
    std::vector<int> next;      // next[i*n+j] = vertex after i on a shortest i->j path, -1 if none
    bool negative_cycle = false;

    double at(int i, int j) const { return dist[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)]; }
    std::vector<int> path(int i, int j) const;   // empty if unreachable
};

AllPairsShortestPaths floyd_warshall(const Graph &g);

} // namespace ev
