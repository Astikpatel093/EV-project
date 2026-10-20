// graph.hpp - weighted graph stored as adjacency lists.
//
// Vertices are 0..n-1. An undirected edge is stored as two arcs (u->v and v->u) that share the
// same edge id, so algorithms can report which input edges they used. Self-loops and parallel
// (duplicate) edges are allowed: shortest-path and spanning-tree algorithms handle them correctly
// (a self-loop is never useful; of several parallel edges the cheapest one wins).
//
// Validation: vertex ids outside [0, n) raise std::out_of_range; NaN or infinite weights raise
// std::invalid_argument. Negative weights are accepted here because Bellman-Ford and the energy
// graph need them; Dijkstra rejects them itself.
#pragma once

#include <cstddef>
#include <vector>

namespace ev {

struct Arc {
    int to = 0;
    double weight = 0;
    int edge = 0;   // id of the edge this arc belongs to
};

struct Edge {
    int u = 0, v = 0;
    double weight = 0;
};

class Graph {
public:
    explicit Graph(int vertices = 0, bool directed = true);

    int vertex_count() const { return static_cast<int>(adj_.size()); }
    int edge_count() const { return static_cast<int>(edges_.size()); }
    bool directed() const { return directed_; }

    // Adds an edge and returns its id (0, 1, 2, ... in insertion order).
    int add_edge(int u, int v, double weight);

    const std::vector<Arc> &out(int u) const;      // arcs leaving u (throws std::out_of_range)
    const std::vector<Edge> &edges() const { return edges_; }
    const Edge &edge(int id) const;

    void check_vertex(int v) const;                 // throws std::out_of_range when v is not a vertex
    bool has_negative_weight() const { return negative_; }

private:
    std::vector<std::vector<Arc>> adj_;
    std::vector<Edge> edges_;
    bool directed_;
    bool negative_ = false;
};

} // namespace ev
