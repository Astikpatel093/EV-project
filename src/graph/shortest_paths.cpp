// shortest_paths.cpp - Dijkstra, Bellman-Ford, Floyd-Warshall.
#include "ev/graph/shortest_paths.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "ev/algorithms/heap.hpp"

namespace ev {

bool ShortestPathTree::reachable(int v) const {
    return v >= 0 && static_cast<std::size_t>(v) < dist.size() && dist[static_cast<std::size_t>(v)] < kInfinity;
}

std::vector<int> ShortestPathTree::path_to(int v) const {
    std::vector<int> rev;
    if (!reachable(v)) return rev;
    for (int x = v; x != -1; x = parent[static_cast<std::size_t>(x)]) {
        rev.push_back(x);
        if (rev.size() > dist.size()) return {};   // defensive: a parent cycle (negative cycle) must not loop forever
    }
    return std::vector<int>(rev.rbegin(), rev.rend());
}

std::vector<int> ShortestPathTree::edges_to(int v) const {
    std::vector<int> rev;
    if (!reachable(v)) return rev;
    for (int x = v; parent[static_cast<std::size_t>(x)] != -1; x = parent[static_cast<std::size_t>(x)]) {
        rev.push_back(parent_edge[static_cast<std::size_t>(x)]);
        if (rev.size() > dist.size()) return {};
    }
    return std::vector<int>(rev.rbegin(), rev.rend());
}

static ShortestPathTree empty_tree(const Graph &g, int source) {
    g.check_vertex(source);
    ShortestPathTree t;
    const std::size_t n = static_cast<std::size_t>(g.vertex_count());
    t.source = source;
    t.dist.assign(n, kInfinity);
    t.parent.assign(n, -1);
    t.parent_edge.assign(n, -1);
    t.dist[static_cast<std::size_t>(source)] = 0;
    return t;
}

static double checked_add(double a, double b) {
    double s = a + b;
    if (!std::isfinite(s)) throw std::overflow_error("path length overflows the range of double");
    return s;
}

ShortestPathTree dijkstra(const Graph &g, int source) {
    if (g.has_negative_weight()) {
        for (const Edge &e : g.edges())
            if (e.weight < 0)
                throw std::invalid_argument("Dijkstra requires non-negative weights; edge " + std::to_string(e.u) + "-" +
                                            std::to_string(e.v) + " has weight " + std::to_string(e.weight));
    }
    ShortestPathTree t = empty_tree(g, source);
    struct Item { double d; int v; };
    struct Closer {
        bool operator()(const Item &a, const Item &b) const { return a.d < b.d || (a.d == b.d && a.v < b.v); }
    };
    BinaryHeap<Item, Closer> heap;
    std::vector<bool> done(t.dist.size(), false);
    heap.push({0.0, source});
    while (!heap.empty()) {
        Item it = heap.pop();
        const std::size_t u = static_cast<std::size_t>(it.v);
        if (done[u]) continue;            // stale entry (lazy deletion)
        done[u] = true;
        for (const Arc &a : g.out(it.v)) {
            const std::size_t v = static_cast<std::size_t>(a.to);
            if (done[v]) continue;
            double nd = checked_add(t.dist[u], a.weight);
            if (nd < t.dist[v]) {
                t.dist[v] = nd;
                t.parent[v] = it.v;
                t.parent_edge[v] = a.edge;
                t.relaxations++;
                heap.push({nd, a.to});
            }
        }
    }
    return t;
}

ShortestPathTree bellman_ford(const Graph &g, int source) {
    ShortestPathTree t = empty_tree(g, source);
    const int n = g.vertex_count();
    // round k finds every shortest path that uses at most k edges; n-1 rounds suffice without
    // negative cycles. A change in round n proves a negative cycle reachable from the source.
    for (int round = 1; round <= n; round++) {
        bool changed = false;
        for (int u = 0; u < n; u++) {
            const double du = t.dist[static_cast<std::size_t>(u)];
            if (du == kInfinity) continue;
            for (const Arc &a : g.out(u)) {
                double nd = checked_add(du, a.weight);
                if (nd < t.dist[static_cast<std::size_t>(a.to)]) {
                    t.dist[static_cast<std::size_t>(a.to)] = nd;
                    t.parent[static_cast<std::size_t>(a.to)] = u;
                    t.parent_edge[static_cast<std::size_t>(a.to)] = a.edge;
                    t.relaxations++;
                    changed = true;
                }
            }
        }
        t.rounds = round;
        if (!changed) break;
        if (round == n) t.negative_cycle = true;
    }
    return t;
}

std::vector<int> AllPairsShortestPaths::path(int i, int j) const {
    if (i < 0 || j < 0 || i >= n || j >= n) throw std::out_of_range("vertex out of range");
    std::vector<int> p;
    if (at(i, j) == kInfinity) return p;
    p.push_back(i);
    int u = i;
    while (u != j) {
        u = next[static_cast<std::size_t>(u) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)];
        if (u < 0 || p.size() > static_cast<std::size_t>(n)) return {};
        p.push_back(u);
    }
    return p;
}

AllPairsShortestPaths floyd_warshall(const Graph &g) {
    AllPairsShortestPaths r;
    const std::size_t n = static_cast<std::size_t>(g.vertex_count());
    r.n = static_cast<int>(n);
    r.dist.assign(n * n, kInfinity);
    r.next.assign(n * n, -1);
    for (std::size_t i = 0; i < n; i++) { r.dist[i * n + i] = 0; r.next[i * n + i] = static_cast<int>(i); }
    for (const Edge &e : g.edges()) {
        auto relax = [&](int a, int b) {
            std::size_t k = static_cast<std::size_t>(a) * n + static_cast<std::size_t>(b);
            if (e.weight < r.dist[k]) { r.dist[k] = e.weight; r.next[k] = b; }
        };
        relax(e.u, e.v);
        if (!g.directed()) relax(e.v, e.u);
    }
    for (std::size_t k = 0; k < n; k++)
        for (std::size_t i = 0; i < n; i++) {
            const double dik = r.dist[i * n + k];
            if (dik == kInfinity) continue;
            for (std::size_t j = 0; j < n; j++) {
                const double dkj = r.dist[k * n + j];
                if (dkj == kInfinity) continue;
                double d = dik + dkj;
                if (d < r.dist[i * n + j]) { r.dist[i * n + j] = d; r.next[i * n + j] = r.next[i * n + k]; }
            }
        }
    for (std::size_t i = 0; i < n; i++)
        if (r.dist[i * n + i] < 0) r.negative_cycle = true;
    return r;
}

} // namespace ev
