// spanning_tree.cpp - Prim, Kruskal and union-find.
#include "ev/graph/spanning_tree.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/algorithms/heap.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/graph/shortest_paths.hpp"

namespace ev {

DisjointSet::DisjointSet(int n) : parent_(static_cast<std::size_t>(n)), rank_(static_cast<std::size_t>(n), 0), sets_(n) {
    for (int i = 0; i < n; i++) parent_[static_cast<std::size_t>(i)] = i;
}

int DisjointSet::find(int x) {
    while (parent_[static_cast<std::size_t>(x)] != x) {
        parent_[static_cast<std::size_t>(x)] = parent_[static_cast<std::size_t>(parent_[static_cast<std::size_t>(x)])];   // path halving
        x = parent_[static_cast<std::size_t>(x)];
    }
    return x;
}

bool DisjointSet::unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) return false;
    if (rank_[static_cast<std::size_t>(a)] < rank_[static_cast<std::size_t>(b)]) std::swap(a, b);
    parent_[static_cast<std::size_t>(b)] = a;
    if (rank_[static_cast<std::size_t>(a)] == rank_[static_cast<std::size_t>(b)]) rank_[static_cast<std::size_t>(a)]++;
    sets_--;
    return true;
}

static void require_undirected(const Graph &g) {
    if (g.directed()) throw std::invalid_argument("minimum spanning trees are defined for undirected graphs");
}

SpanningForest prim(const Graph &g) {
    require_undirected(g);
    const int n = g.vertex_count();
    SpanningForest f;
    std::vector<bool> in_tree(static_cast<std::size_t>(n), false);
    struct Cand { double w; int edge; int from, to; };
    struct Lighter {
        bool operator()(const Cand &a, const Cand &b) const { return a.w < b.w || (a.w == b.w && a.edge < b.edge); }
    };
    BinaryHeap<Cand, Lighter> heap;
    for (int root = 0; root < n; root++) {
        if (in_tree[static_cast<std::size_t>(root)]) continue;
        f.components++;                         // a new tree of the forest starts here
        in_tree[static_cast<std::size_t>(root)] = true;
        for (const Arc &a : g.out(root)) heap.push({a.weight, a.edge, root, a.to});
        while (!heap.empty()) {
            Cand c = heap.pop();
            if (in_tree[static_cast<std::size_t>(c.to)]) continue;   // both ends already in the tree (or a self-loop)
            in_tree[static_cast<std::size_t>(c.to)] = true;
            f.total_weight += c.w;
            f.edges.push_back(c.edge);
            f.links.emplace_back(c.from, c.to);
            for (const Arc &a : g.out(c.to))
                if (!in_tree[static_cast<std::size_t>(a.to)]) heap.push({a.weight, a.edge, c.to, a.to});
        }
    }
    return f;
}

SpanningForest kruskal(const Graph &g) {
    require_undirected(g);
    const int n = g.vertex_count();
    std::vector<int> order(static_cast<std::size_t>(g.edge_count()));
    for (std::size_t i = 0; i < order.size(); i++) order[i] = static_cast<int>(i);
    const std::vector<Edge> &E = g.edges();
    merge_sort(order, [&](int a, int b) {
        const double wa = E[static_cast<std::size_t>(a)].weight, wb = E[static_cast<std::size_t>(b)].weight;
        return wa < wb || (wa == wb && a < b);
    });
    DisjointSet ds(n);
    SpanningForest f;
    for (int id : order) {
        const Edge &e = E[static_cast<std::size_t>(id)];
        if (ds.unite(e.u, e.v)) {   // joins two different trees: safe by the cut property
            f.total_weight += e.weight;
            f.edges.push_back(id);
            f.links.emplace_back(e.u, e.v);
            if (static_cast<int>(f.edges.size()) == n - 1) break;
        }
    }
    f.components = ds.sets();
    return f;
}

SpanningForest prim_dense(const std::vector<std::vector<double>> &d) {
    const std::size_t n = d.size();
    for (const auto &row : d)
        if (row.size() != n) throw std::invalid_argument("distance matrix must be square");
    SpanningForest f;
    std::vector<bool> in(n, false);
    std::vector<double> key(n, kInfinity);
    std::vector<int> par(n, -1);
    for (std::size_t step = 0; step < n; step++) {
        std::size_t u = n;
        for (std::size_t v = 0; v < n; v++)
            if (!in[v] && (u == n || key[v] < key[u])) u = v;
        if (key[u] == kInfinity) { key[u] = 0; f.components++; }   // start of a new component
        in[u] = true;
        if (par[u] >= 0) {
            f.total_weight += key[u];
            f.links.emplace_back(par[u], static_cast<int>(u));
        }
        for (std::size_t v = 0; v < n; v++)
            if (!in[v] && v != u && d[u][v] < key[v]) { key[v] = d[u][v]; par[v] = static_cast<int>(u); }
    }
    return f;
}

} // namespace ev
