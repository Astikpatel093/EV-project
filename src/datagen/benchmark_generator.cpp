// benchmark_generator.cpp - integer-only, seed-reproducible benchmark data.
#include "ev/datagen/benchmark_generator.hpp"

#include <stdexcept>
#include <unordered_set>

#include "ev/core/rng.hpp"

namespace ev {

const std::vector<GraphSize> &benchmark_graph_sizes() {
    static const std::vector<GraphSize> sizes = {
        {"B1", 100, 300}, {"B2", 500, 1500}, {"B3", 1000, 3000}, {"B4", 5000, 15000}, {"B5", 10000, 30000}, {"B6", 50000, 150000},
    };
    return sizes;
}

const char *to_string(GraphVariant v) {
    switch (v) {
    case GraphVariant::ConnectedVaried: return "connected-varied";
    case GraphVariant::ConnectedEqual: return "connected-equal";
    case GraphVariant::Disconnected: return "disconnected";
    default: return "dense-small";
    }
}

bool parse_graph_variant(const std::string &s, GraphVariant &out) {
    for (GraphVariant v : {GraphVariant::ConnectedVaried, GraphVariant::ConnectedEqual, GraphVariant::Disconnected, GraphVariant::DenseSmall})
        if (s == to_string(v)) { out = v; return true; }
    return false;
}

Graph GraphDataset::to_graph() const {
    Graph g(vertices, false);
    for (const Edge &e : edges) g.add_edge(e.u, e.v, e.weight);
    return g;
}

namespace {

// Builds one connected component on vertex ids [base, base + n) with m edges.
void build_component(int base, int n, long m, bool equal_weights, Rng &rng, std::vector<Edge> &out,
                     std::unordered_set<std::uint64_t> &seen, std::uint64_t key_scale) {
    if (n <= 0) return;
    auto weight = [&]() { return equal_weights ? 10.0 : static_cast<double>(rng.range(1, 1000)); };
    auto add = [&](int a, int b) {
        const std::uint64_t lo = static_cast<std::uint64_t>(a < b ? a : b), hi = static_cast<std::uint64_t>(a < b ? b : a);
        if (a == b || !seen.insert(lo * key_scale + hi).second) return false;
        out.push_back({a, b, weight()});
        return true;
    };
    // random spanning tree: shuffle the vertices, attach each to a random earlier one
    std::vector<int> perm(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) perm[static_cast<std::size_t>(i)] = base + i;
    for (int i = n - 1; i > 0; i--) {
        int j = static_cast<int>(rng.range(0, i));
        std::swap(perm[static_cast<std::size_t>(i)], perm[static_cast<std::size_t>(j)]);
    }
    for (int k = 1; k < n; k++) add(perm[static_cast<std::size_t>(k)], perm[static_cast<std::size_t>(rng.range(0, k - 1))]);
    long have = n - 1;
    while (have < m) {
        int a = base + static_cast<int>(rng.range(0, n - 1)), b = base + static_cast<int>(rng.range(0, n - 1));
        if (add(a, b)) have++;
    }
}

} // namespace

GraphDataset make_graph_dataset(int n, int m, GraphVariant variant, std::uint64_t seed, const std::string &name) {
    if (n < 1) throw std::invalid_argument("a benchmark graph needs at least one vertex");
    GraphDataset d;
    d.variant = variant;
    d.vertices = n;
    d.seed = seed;
    d.name = name.empty() ? std::string(to_string(variant)) + "-" + std::to_string(n) : name;
    const int comps = variant == GraphVariant::Disconnected ? (n >= 4 ? 4 : 1) : 1;
    const long max_pairs = static_cast<long>(n) * (n - 1) / 2;
    if (m < n - comps) throw std::invalid_argument("too few edges to connect every component");
    if (m > max_pairs) throw std::invalid_argument("more edges than vertex pairs");
    Rng rng(seed ^ (static_cast<std::uint64_t>(variant) * 0x9E3779B97F4A7C15ULL));
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(static_cast<std::size_t>(m) * 2);
    d.edges.reserve(static_cast<std::size_t>(m));
    const bool equal = variant == GraphVariant::ConnectedEqual;
    int base = 0;
    long placed = 0;
    for (int c = 0; c < comps; c++) {
        const int size = n / comps + (c < n % comps ? 1 : 0);
        long mc = c == comps - 1 ? m - placed : static_cast<long>(m) * size / n;
        const long pairs = static_cast<long>(size) * (size - 1) / 2;
        if (mc < size - 1) mc = size - 1;
        if (mc > pairs) mc = pairs;
        build_component(base, size, mc, equal, rng, d.edges, seen, static_cast<std::uint64_t>(n) + 1);
        placed += mc;
        base += size;
    }
    d.components = comps;
    return d;
}

const char *to_string(KeyOrder k) {
    switch (k) {
    case KeyOrder::Random: return "random";
    case KeyOrder::Sorted: return "sorted";
    case KeyOrder::Reversed: return "reversed";
    default: return "few-distinct";
    }
}

std::vector<std::int64_t> make_sort_keys(std::size_t n, KeyOrder order, std::uint64_t seed) {
    Rng rng(seed + 101);
    std::vector<std::int64_t> a(n);
    std::int64_t acc = 0;
    for (std::size_t i = 0; i < n; i++) {
        switch (order) {
        case KeyOrder::Random: a[i] = rng.range(0, 999999999); break;
        case KeyOrder::Sorted: acc += rng.range(0, 1000); a[i] = acc; break;
        case KeyOrder::Reversed: acc += rng.range(0, 1000); a[n - 1 - i] = acc; break;
        default: a[i] = rng.range(0, 9); break;
        }
    }
    return a;
}

std::vector<PlacementItem> make_placement_items(std::size_t n, std::uint64_t seed) {
    Rng rng(seed + 202);
    std::vector<PlacementItem> items(n);
    for (PlacementItem &it : items) {
        it.cost_lakh = rng.range(10, 100);
        it.benefit = it.cost_lakh * 2 + rng.range(0, 50);   // weakly correlated: hard for branch-and-bound
    }
    return items;
}

std::vector<SessionItem> make_sessions(std::size_t n, std::uint64_t seed) {
    Rng rng(seed + 303);
    std::vector<SessionItem> s(n);
    for (std::size_t i = 0; i < n; i++) {
        s[i].id = static_cast<int>(i + 1);
        s[i].start_min = rng.range(0, 1439);
        s[i].end_min = s[i].start_min + rng.range(10, 90);
    }
    return s;
}

std::uint64_t fnv1a(const std::string &bytes) {
    std::uint64_t h = 14695981039346656037ULL;   // FNV offset basis
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace ev
