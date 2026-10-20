// Unit tests: Prim, Kruskal, union-find, max flow, min cut, bipartite matching.
#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "ev/core/rng.hpp"
#include "ev/graph/max_flow.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/graph/spanning_tree.hpp"

using namespace ev;

namespace {

Graph clrs_mst() {   // CLRS figure 23.4, total weight 37
    Graph g(9, false);
    const int E[14][3] = {{0, 1, 4}, {0, 7, 8}, {1, 2, 8}, {1, 7, 11}, {2, 3, 7}, {2, 8, 2}, {2, 5, 4},
                          {3, 4, 9}, {3, 5, 14}, {4, 5, 10}, {5, 6, 2}, {6, 7, 1}, {6, 8, 6}, {7, 8, 7}};
    for (const auto &e : E) g.add_edge(e[0], e[1], e[2]);
    return g;
}

} // namespace

TEST(SpanningTree, ConnectedGraphWithCycles) {
    Graph g = clrs_mst();
    SpanningForest p = prim(g), k = kruskal(g);
    EXPECT_DOUBLE_EQ(p.total_weight, 37);
    EXPECT_DOUBLE_EQ(k.total_weight, 37);
    EXPECT_EQ(p.edges.size(), 8u);   // n - 1 edges
    EXPECT_EQ(k.edges.size(), 8u);
    EXPECT_TRUE(p.is_spanning_tree());
    EXPECT_TRUE(k.is_spanning_tree());
}

TEST(SpanningTree, EqualWeightEdges) {
    Graph g(5, false);
    for (int a = 0; a < 5; a++)
        for (int b = a + 1; b < 5; b++) g.add_edge(a, b, 3);   // complete graph, all weights equal
    EXPECT_DOUBLE_EQ(prim(g).total_weight, 12);
    EXPECT_DOUBLE_EQ(kruskal(g).total_weight, 12);
}

TEST(SpanningTree, DisconnectedGraphGivesAForest) {
    Graph g(6, false);
    g.add_edge(0, 1, 1); g.add_edge(1, 2, 2); g.add_edge(0, 2, 5);   // triangle
    g.add_edge(3, 4, 4);                                              // pair
    // vertex 5 is isolated
    SpanningForest p = prim(g), k = kruskal(g);
    EXPECT_EQ(p.components, 3);
    EXPECT_EQ(k.components, 3);
    EXPECT_FALSE(p.is_spanning_tree());
    EXPECT_DOUBLE_EQ(p.total_weight, 7);
    EXPECT_DOUBLE_EQ(k.total_weight, 7);
    EXPECT_EQ(p.edges.size(), 3u);   // n - components
}

TEST(SpanningTree, SingleNodeAndEmptyGraph) {
    Graph one(1, false);
    EXPECT_EQ(prim(one).components, 1);
    EXPECT_TRUE(prim(one).is_spanning_tree());
    EXPECT_TRUE(kruskal(one).edges.empty());
    Graph none(0, false);
    EXPECT_EQ(prim(none).components, 0);
    EXPECT_EQ(kruskal(none).components, 0);
}

TEST(SpanningTree, SelfLoopsAndDuplicatesAreIgnored) {
    Graph g(3, false);
    g.add_edge(0, 0, 0.5);
    g.add_edge(0, 1, 5);
    g.add_edge(0, 1, 2);
    g.add_edge(1, 2, 3);
    EXPECT_DOUBLE_EQ(prim(g).total_weight, 5);
    EXPECT_DOUBLE_EQ(kruskal(g).total_weight, 5);
}

TEST(SpanningTree, DirectedGraphIsRejected) {
    Graph g(2, true);
    g.add_edge(0, 1, 1);
    EXPECT_THROW(prim(g), std::invalid_argument);
    EXPECT_THROW(kruskal(g), std::invalid_argument);
}

TEST(SpanningTree, PrimAndKruskalAgreeOnRandomGraphs) {
    Rng rng(23);
    for (int trial = 0; trial < 100; trial++) {
        const int n = static_cast<int>(rng.range(1, 60));
        Graph g(n, false);
        const int m = static_cast<int>(rng.range(0, n * 3));
        for (int k = 0; k < m; k++) g.add_edge(static_cast<int>(rng.range(0, n - 1)), static_cast<int>(rng.range(0, n - 1)), static_cast<double>(rng.range(1, 20)));
        SpanningForest p = prim(g), k = kruskal(g);
        EXPECT_DOUBLE_EQ(p.total_weight, k.total_weight);
        EXPECT_EQ(p.components, k.components);
        EXPECT_EQ(static_cast<int>(p.edges.size()), n - p.components);
    }
}

TEST(SpanningTree, MinimumSpanningTreeIsNotAShortestPathTree) {
    // square 0-1-2-3 with a heavy diagonal: the MST avoids the diagonal, the shortest path 0->2 uses it
    Graph g(4, false);
    g.add_edge(0, 1, 2); g.add_edge(1, 2, 2); g.add_edge(2, 3, 2); g.add_edge(3, 0, 10); g.add_edge(0, 2, 3);
    EXPECT_DOUBLE_EQ(kruskal(g).total_weight, 6);       // edges 0-1, 1-2, 2-3
    EXPECT_DOUBLE_EQ(dijkstra(g, 0).dist[2], 3);        // via the diagonal, not in the MST
}

TEST(SpanningTree, DenseMatrixVersionMatches) {
    Graph g = clrs_mst();
    std::vector<std::vector<double>> d(9, std::vector<double>(9, kInfinity));
    for (const Edge &e : g.edges()) {
        d[static_cast<std::size_t>(e.u)][static_cast<std::size_t>(e.v)] = e.weight;
        d[static_cast<std::size_t>(e.v)][static_cast<std::size_t>(e.u)] = e.weight;
    }
    EXPECT_DOUBLE_EQ(prim_dense(d).total_weight, 37);
}

TEST(DisjointSet, UnionAndFind) {
    DisjointSet ds(5);
    EXPECT_EQ(ds.sets(), 5);
    EXPECT_TRUE(ds.unite(0, 1));
    EXPECT_TRUE(ds.unite(3, 4));
    EXPECT_FALSE(ds.unite(1, 0));
    EXPECT_EQ(ds.find(0), ds.find(1));
    EXPECT_NE(ds.find(1), ds.find(3));
    EXPECT_EQ(ds.sets(), 3);
}

// ------------------------------------------------------------------ flow
TEST(MaxFlow, TextbookNetwork) {
    for (FlowMethod m : {FlowMethod::FordFulkersonDfs, FlowMethod::EdmondsKarpBfs}) {
        FlowNetwork f(6);   // CLRS figure 26.6, value 23
        const int E[9][3] = {{0, 1, 16}, {0, 2, 13}, {1, 3, 12}, {2, 1, 4}, {2, 4, 14}, {3, 2, 9}, {3, 5, 20}, {4, 3, 7}, {4, 5, 4}};
        for (const auto &e : E) f.add_edge(e[0], e[1], e[2]);
        FlowResult r = f.max_flow(0, 5, m);
        EXPECT_EQ(r.value, 23);
        EXPECT_EQ(f.cut_capacity(f.min_cut_source_side(0)), 23);   // max-flow = min-cut
    }
}

TEST(MaxFlow, InvalidInput) {
    FlowNetwork f(3);
    EXPECT_THROW(f.add_edge(0, 1, -1), std::invalid_argument);
    EXPECT_THROW(f.add_edge(0, 3, 1), std::out_of_range);
    EXPECT_THROW(f.max_flow(1, 1, FlowMethod::EdmondsKarpBfs), std::invalid_argument);
    EXPECT_EQ(f.max_flow(0, 2, FlowMethod::EdmondsKarpBfs).value, 0);   // no path
}

TEST(MaxFlow, DfsBfsAndMatchingAgreeOnRandomBipartiteNetworks) {
    Rng rng(31);
    for (int trial = 0; trial < 60; trial++) {
        const int L = static_cast<int>(rng.range(1, 25)), R = static_cast<int>(rng.range(1, 25));
        std::vector<std::vector<int>> adj(static_cast<std::size_t>(L));
        FlowNetwork f(L + R + 2);
        for (int i = 0; i < L; i++) f.add_edge(0, 1 + i, 1);
        for (int j = 0; j < R; j++) f.add_edge(1 + L + j, L + R + 1, 1);
        for (int i = 0; i < L; i++)
            for (int j = 0; j < R; j++)
                if (rng.unit() < 0.15) {
                    adj[static_cast<std::size_t>(i)].push_back(j);
                    f.add_edge(1 + i, 1 + L + j, 1);
                }
        const std::int64_t dfs = f.max_flow(0, L + R + 1, FlowMethod::FordFulkersonDfs).value;
        f.reset();
        const std::int64_t bfs = f.max_flow(0, L + R + 1, FlowMethod::EdmondsKarpBfs).value;
        std::vector<int> ml;
        const int match = bipartite_matching(L, R, adj, &ml);
        EXPECT_EQ(dfs, bfs);
        EXPECT_EQ(bfs, match);
        std::vector<int> used(static_cast<std::size_t>(R), 0);
        for (int r : ml)
            if (r >= 0) EXPECT_EQ(used[static_cast<std::size_t>(r)]++, 0);   // each right vertex used once
    }
}

TEST(BipartiteMatching, PerfectMatchingNeedsAugmentingPaths) {
    // greedy first-fit would match 0-0 and leave 1 unmatched; augmenting paths fix it
    std::vector<std::vector<int>> adj = {{0, 1}, {0}};
    std::vector<int> ml;
    EXPECT_EQ(bipartite_matching(2, 2, adj, &ml), 2);
    EXPECT_EQ(ml[1], 0);
    EXPECT_EQ(ml[0], 1);
    EXPECT_THROW(bipartite_matching(2, 1, adj), std::out_of_range);
}
