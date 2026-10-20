// Unit tests: graph structure, Dijkstra, Bellman-Ford, Floyd-Warshall.
#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "ev/core/rng.hpp"
#include "ev/graph/shortest_paths.hpp"

using namespace ev;

namespace {

Graph clrs_dijkstra() {
    Graph g(5, true);   // CLRS figure 24.6: s=0 t=1 x=2 y=3 z=4
    g.add_edge(0, 1, 10); g.add_edge(0, 3, 5); g.add_edge(1, 2, 1); g.add_edge(1, 3, 2); g.add_edge(2, 4, 4);
    g.add_edge(3, 1, 3); g.add_edge(3, 2, 9); g.add_edge(3, 4, 2); g.add_edge(4, 0, 7); g.add_edge(4, 2, 6);
    return g;
}

double path_weight(const Graph &g, const std::vector<int> &edges) {
    double w = 0;
    for (int e : edges) w += g.edge(e).weight;
    return w;
}

} // namespace

TEST(Graph, ValidatesVerticesAndWeights) {
    Graph g(3);
    EXPECT_THROW(g.add_edge(0, 3, 1), std::out_of_range);
    EXPECT_THROW(g.add_edge(-1, 0, 1), std::out_of_range);
    EXPECT_THROW(g.add_edge(0, 1, std::nan("")), std::invalid_argument);
    EXPECT_THROW(g.add_edge(0, 1, INFINITY), std::invalid_argument);
    EXPECT_THROW(Graph(-1), std::invalid_argument);
    EXPECT_EQ(g.add_edge(0, 1, 2), 0);
    EXPECT_EQ(g.add_edge(1, 2, 2), 1);
}

TEST(Dijkstra, KnownGraphKnownAnswer) {
    Graph g = clrs_dijkstra();
    ShortestPathTree t = dijkstra(g, 0);
    EXPECT_EQ(t.dist, (std::vector<double>{0, 8, 9, 5, 7}));
    EXPECT_EQ(t.path_to(2), (std::vector<int>{0, 3, 1, 2}));
    EXPECT_DOUBLE_EQ(path_weight(g, t.edges_to(2)), 9);
}

TEST(Dijkstra, DisconnectedGraphGivesUnreachable) {
    Graph g(4, false);
    g.add_edge(0, 1, 3);
    g.add_edge(2, 3, 4);
    ShortestPathTree t = dijkstra(g, 0);
    EXPECT_TRUE(t.reachable(1));
    EXPECT_FALSE(t.reachable(2));
    EXPECT_EQ(t.dist[2], kInfinity);
    EXPECT_TRUE(t.path_to(3).empty());
}

TEST(Dijkstra, SingleNodeAndSourceEqualsDestination) {
    Graph one(1);
    ShortestPathTree t = dijkstra(one, 0);
    EXPECT_EQ(t.dist[0], 0);
    EXPECT_EQ(t.path_to(0), std::vector<int>{0});
    ShortestPathTree u = dijkstra(clrs_dijkstra(), 3);
    EXPECT_EQ(u.path_to(3), std::vector<int>{3});
    EXPECT_TRUE(u.edges_to(3).empty());
}

TEST(Dijkstra, InvalidSourceIsRejected) {
    Graph g = clrs_dijkstra();
    EXPECT_THROW(dijkstra(g, 5), std::out_of_range);
    EXPECT_THROW(dijkstra(g, -1), std::out_of_range);
    EXPECT_THROW(bellman_ford(g, 99), std::out_of_range);
}

TEST(Dijkstra, DuplicateEdgesUseTheCheapest) {
    Graph g(2, false);
    g.add_edge(0, 1, 9);
    g.add_edge(0, 1, 4);
    g.add_edge(0, 1, 7);
    ShortestPathTree t = dijkstra(g, 0);
    EXPECT_EQ(t.dist[1], 4);
    EXPECT_EQ(t.edges_to(1), std::vector<int>{1});
}

TEST(Dijkstra, SelfLoopsAreHarmless) {
    Graph g(3, true);
    g.add_edge(0, 0, 1);
    g.add_edge(0, 1, 2);
    g.add_edge(1, 1, 0);
    g.add_edge(1, 2, 2);
    ShortestPathTree t = dijkstra(g, 0);
    EXPECT_EQ(t.dist, (std::vector<double>{0, 2, 4}));
}

TEST(Dijkstra, ZeroWeightEdgesArePermitted) {
    Graph g(3, true);
    g.add_edge(0, 1, 0);
    g.add_edge(1, 2, 0);
    g.add_edge(0, 2, 1);
    EXPECT_EQ(dijkstra(g, 0).dist[2], 0);
}

TEST(Dijkstra, NegativeWeightIsRejected) {
    Graph g(3, true);
    g.add_edge(0, 1, 2);
    g.add_edge(1, 2, -1);
    EXPECT_THROW(dijkstra(g, 0), std::invalid_argument);
    EXPECT_NO_THROW(bellman_ford(g, 0));   // Bellman-Ford is the right tool here
    EXPECT_EQ(bellman_ford(g, 0).dist[2], 1);
}

TEST(Dijkstra, VeryLargeWeights) {
    Graph g(3, true);
    g.add_edge(0, 1, 1e15);
    g.add_edge(1, 2, 1e15);
    EXPECT_EQ(dijkstra(g, 0).dist[2], 2e15);   // exact in double
    Graph h(3, true);
    h.add_edge(0, 1, 1e308);
    h.add_edge(1, 2, 1e308);
    EXPECT_THROW(dijkstra(h, 0), std::overflow_error);   // not silently "unreachable"
    EXPECT_THROW(bellman_ford(h, 0), std::overflow_error);
}

TEST(Dijkstra, MultipleEquallyShortPathsAreDeterministic) {
    // 0 -> 1 -> 3 and 0 -> 2 -> 3 both cost 2
    Graph g(4, false);
    g.add_edge(0, 1, 1);
    g.add_edge(0, 2, 1);
    g.add_edge(1, 3, 1);
    g.add_edge(2, 3, 1);
    ShortestPathTree a = dijkstra(g, 0), b = dijkstra(g, 0);
    EXPECT_EQ(a.dist[3], 2);
    EXPECT_EQ(a.path_to(3), b.path_to(3));          // same answer every time
    EXPECT_EQ(a.path_to(3).size(), 3u);             // and it is one of the two shortest paths
    EXPECT_DOUBLE_EQ(path_weight(g, a.edges_to(3)), 2);
}

TEST(BellmanFord, TextbookNegativeArcs) {
    Graph g(5, true);   // CLRS figure 24.4
    g.add_edge(0, 1, 6); g.add_edge(0, 3, 7); g.add_edge(1, 2, 5); g.add_edge(1, 3, 8); g.add_edge(1, 4, -4);
    g.add_edge(2, 1, -2); g.add_edge(3, 2, -3); g.add_edge(3, 4, 9); g.add_edge(4, 0, 2); g.add_edge(4, 2, 7);
    ShortestPathTree t = bellman_ford(g, 0);
    EXPECT_FALSE(t.negative_cycle);
    EXPECT_EQ(t.dist, (std::vector<double>{0, 2, 4, 7, -2}));
}

TEST(BellmanFord, DetectsNegativeCycle) {
    Graph g(3, true);
    g.add_edge(0, 1, 1);
    g.add_edge(1, 2, -2);
    g.add_edge(2, 1, 1);
    EXPECT_TRUE(bellman_ford(g, 0).negative_cycle);
    EXPECT_TRUE(floyd_warshall(g).negative_cycle);
    // a negative cycle that the source cannot reach does not matter
    Graph h(4, true);
    h.add_edge(0, 1, 1);
    h.add_edge(2, 3, -2);
    h.add_edge(3, 2, 1);
    EXPECT_FALSE(bellman_ford(h, 0).negative_cycle);
}

TEST(ShortestPaths, AllThreeAlgorithmsAgreeOnRandomGraphs) {
    Rng rng(17);
    for (int trial = 0; trial < 40; trial++) {
        const int n = static_cast<int>(rng.range(2, 40));
        Graph g(n, trial % 2 == 0);
        const int m = static_cast<int>(rng.range(0, n * 4));
        for (int k = 0; k < m; k++) g.add_edge(static_cast<int>(rng.range(0, n - 1)), static_cast<int>(rng.range(0, n - 1)), static_cast<double>(rng.range(0, 30)));
        AllPairsShortestPaths all = floyd_warshall(g);
        for (int s = 0; s < n; s++) {
            ShortestPathTree d = dijkstra(g, s), b = bellman_ford(g, s);
            for (int v = 0; v < n; v++) {
                EXPECT_EQ(d.dist[static_cast<std::size_t>(v)], b.dist[static_cast<std::size_t>(v)]);
                EXPECT_EQ(d.dist[static_cast<std::size_t>(v)], all.at(s, v));
                if (d.reachable(v)) {
                    EXPECT_DOUBLE_EQ(path_weight(g, d.edges_to(v)), d.dist[static_cast<std::size_t>(v)]);
                    std::vector<int> p = all.path(s, v);
                    ASSERT_FALSE(p.empty());
                    EXPECT_EQ(p.front(), s);
                    EXPECT_EQ(p.back(), v);
                } else {
                    EXPECT_TRUE(all.path(s, v).empty());
                }
            }
        }
    }
}
