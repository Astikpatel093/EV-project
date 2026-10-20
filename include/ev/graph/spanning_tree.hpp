// spanning_tree.hpp - minimum spanning trees / forests: Prim and Kruskal, plus union-find.
//
// A minimum spanning tree (MST) connects ALL vertices with the least total edge weight. It is not
// a shortest-path tree: the MST minimises the sum of the chosen edges, while a shortest-path tree
// minimises each vertex's distance from one source. The two usually differ.
//
// If the graph is disconnected no spanning tree exists. Both algorithms then return a minimum
// spanning FOREST: one minimum tree per connected component, with `components` > 1 and
// `is_spanning_tree()` false. Callers must check this instead of assuming connectivity.
//
//  prim     grows trees from a vertex with a binary heap of crossing edges, O(E log V);
//           restarts from the next unvisited vertex for every further component.
//  kruskal  sorts edges by weight (merge sort) and joins components with union-find,
//           O(E log E). Its total weight always equals Prim's on the same graph.
//  prim_dense  O(V^2) version on a distance matrix (infinity = no edge), for complete graphs.
#pragma once

#include <utility>
#include <vector>

#include "ev/graph/graph.hpp"

namespace ev {

class DisjointSet {
public:
    explicit DisjointSet(int n);
    int find(int x);                 // with path halving
    bool unite(int a, int b);        // union by rank; false if already in the same set
    int sets() const { return sets_; }

private:
    std::vector<int> parent_, rank_;
    int sets_;
};

struct SpanningForest {
    double total_weight = 0;
    std::vector<int> edges;                     // ids of the chosen edges (input graph numbering)
    std::vector<std::pair<int, int>> links;     // the same edges as (u, v) vertex pairs
    int components = 0;                         // number of trees (connected components)
    bool is_spanning_tree() const { return components == 1; }
};

// Both require an undirected graph (std::invalid_argument otherwise). A graph with 0 vertices has
// 0 components; a single vertex is a spanning tree with no edges.
SpanningForest prim(const Graph &g);
SpanningForest kruskal(const Graph &g);

// dist[i][j] = weight of edge i-j (kInfinity = no edge); must be square and symmetric.
SpanningForest prim_dense(const std::vector<std::vector<double>> &dist);

} // namespace ev
