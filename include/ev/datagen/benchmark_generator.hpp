// benchmark_generator.hpp - reproducible benchmark datasets of increasing size.
//
// Only integer arithmetic and the project's own xorshift generator are used, so a given
// (seed, parameters) produces byte-identical files on every platform and compiler.
//
// Graph sizes (vertices / target edges):
//   B1 100/300  B2 500/1,500  B3 1,000/3,000  B4 5,000/15,000  B5 10,000/30,000  B6 50,000/150,000
// Variants:
//   connected-varied   random spanning tree first (connected by construction), then extra random
//                      edges; weights 1..1000; no self-loops, no duplicate edges
//   connected-equal    same structure, every weight 10 (many equal-cost paths / MST ties)
//   disconnected       4 components of (almost) equal size, each built like connected-varied
//   dense-small        n <= 400 with m = n(n-1)/4 (half of all pairs); for O(V^2) and O(V^3) methods
// Other datasets: sorting keys (random / sorted / reversed / few-distinct), candidate sites with
// costs and benefits for placement, and charging sessions for scheduling.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ev/graph/graph.hpp"

namespace ev {

constexpr std::uint64_t kBenchmarkSeed = 12345;

struct GraphSize {
    const char *name;   // "B1" .. "B6"
    int vertices;
    int edges;
};
const std::vector<GraphSize> &benchmark_graph_sizes();

enum class GraphVariant { ConnectedVaried, ConnectedEqual, Disconnected, DenseSmall };
const char *to_string(GraphVariant v);
bool parse_graph_variant(const std::string &s, GraphVariant &out);

struct GraphDataset {
    std::string name;            // e.g. "B3-connected-varied"
    GraphVariant variant = GraphVariant::ConnectedVaried;
    int vertices = 0;
    std::uint64_t seed = 0;
    std::vector<Edge> edges;     // undirected, integer-valued weights
    int components = 0;          // by construction
    Graph to_graph() const;      // undirected ev::Graph
};

// Throws std::invalid_argument if the edge count is impossible (fewer than vertices-components or
// more than all pairs).
GraphDataset make_graph_dataset(int vertices, int edges, GraphVariant variant, std::uint64_t seed,
                                const std::string &name = "");

enum class KeyOrder { Random, Sorted, Reversed, FewDistinct };
const char *to_string(KeyOrder k);
std::vector<std::int64_t> make_sort_keys(std::size_t n, KeyOrder order, std::uint64_t seed);

struct PlacementItem { std::int64_t cost_lakh; std::int64_t benefit; };
std::vector<PlacementItem> make_placement_items(std::size_t n, std::uint64_t seed);

struct SessionItem { int id; std::int64_t start_min, end_min; };
std::vector<SessionItem> make_sessions(std::size_t n, std::uint64_t seed);

// 64-bit FNV-1a hash of a byte string (used in metadata.json to verify reproduced files).
std::uint64_t fnv1a(const std::string &bytes);

} // namespace ev
