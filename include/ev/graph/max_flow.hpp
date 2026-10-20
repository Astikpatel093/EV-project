// max_flow.hpp - maximum flow (Ford-Fulkerson with DFS, Edmonds-Karp with BFS), minimum cut and
// maximum bipartite matching.
//
// EV use: how many charge-needing trips can the installed charging ports absorb in a time window
// (source -> trip (1) -> eligible station (1) -> sink (port-slots)).
//   Ford-Fulkerson (DFS augmenting paths)  O(E·|f*|) - depends on the flow value
//   Edmonds-Karp (shortest augmenting paths, BFS)  O(V·E^2) - independent of capacities
//   bipartite matching (augmenting paths, Kuhn)  O(V·E)
// Max-flow = min-cut (checked in the tests), and on a unit-capacity bipartite network the max flow
// equals the maximum matching.
#pragma once

#include <cstdint>
#include <vector>

namespace ev {

enum class FlowMethod { FordFulkersonDfs, EdmondsKarpBfs };

struct FlowResult {
    std::int64_t value = 0;
    std::uint64_t augmentations = 0;
};

class FlowNetwork {
public:
    explicit FlowNetwork(int vertices);
    int vertex_count() const { return static_cast<int>(adj_.size()); }

    // Adds a directed edge with the given capacity (>= 0) and returns its id.
    int add_edge(int u, int v, std::int64_t capacity);

    // Runs from the current flow (call reset() to start again from zero).
    FlowResult max_flow(int source, int sink, FlowMethod method);
    std::int64_t flow(int edge_id) const;
    void reset();

    // After max_flow: side[v] == true when v is reachable from source in the residual graph
    // (the source side of a minimum cut).
    std::vector<bool> min_cut_source_side(int source) const;
    // Sum of capacities of edges from the source side to the sink side.
    std::int64_t cut_capacity(const std::vector<bool> &source_side) const;

private:
    struct E { int to; std::int64_t cap; std::int64_t flow; int rev; };
    std::vector<std::vector<E>> adj_;
    std::vector<std::pair<int, int>> where_;   // edge id -> (vertex, index in adj_)
    void check(int v) const;
    std::int64_t augment_dfs(int s, int t);
    std::int64_t augment_bfs(int s, int t);
};

// adj[i] lists the right-side vertices (0..right-1) that left vertex i may be matched to.
// match_left (optional) receives the partner of each left vertex or -1.
int bipartite_matching(int left, int right, const std::vector<std::vector<int>> &adj,
                       std::vector<int> *match_left = nullptr);

} // namespace ev
