// ev33_bench - reproducible performance measurements written to benchmarks/results.csv.
//
//   ev33_bench [--quick] [--only FAMILY] [--reps N] [--seed 12345] [--out benchmarks/results.csv]
//
// Method (docs/benchmark_methodology.md):
//  * inputs are generated before timing from a fixed seed (ev/datagen/benchmark_generator.hpp);
//  * each measurement is repeated (default 5 times, fewer for slow cases) and the median, minimum
//    and maximum wall-clock times (std::chrono::steady_clock) are reported;
//  * results are validated OUTSIDE the timed region (against another algorithm or an optimality
//    certificate) and a checksum of every result goes into a volatile sink so the compiler cannot
//    remove the work;
//  * peak heap bytes are counted by replacing the global operator new/delete in this executable;
//  * algorithms with quadratic or worse cost are only run on sizes where they finish in seconds.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#include "ev/algorithms/branch_and_bound.hpp"
#include "ev/algorithms/divide_conquer.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/greedy.hpp"
#include "ev/algorithms/searching.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/analysis/pipeline.hpp"
#include "ev/core/timer.hpp"
#include "ev/datagen/benchmark_generator.hpp"
#include "ev/datagen/scenario_generator.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/csv.hpp"
#include "ev/io/scenario_io.hpp"
#include "ev/planner/exact_planner.hpp"

// ------------------------------------------------------------------ heap accounting
namespace {
std::size_t g_current = 0, g_peak = 0;
}
void *operator new(std::size_t n) {
    void *p = std::malloc(n + 16);
    if (!p) throw std::bad_alloc();
    *static_cast<std::size_t *>(p) = n;
    g_current += n;
    if (g_current > g_peak) g_peak = g_current;
    return static_cast<char *>(p) + 16;
}
void operator delete(void *p) noexcept {
    if (!p) return;
    char *base = static_cast<char *>(p) - 16;
    g_current -= *reinterpret_cast<std::size_t *>(base);
    std::free(base);
}
void operator delete(void *p, std::size_t) noexcept { operator delete(p); }
void *operator new[](std::size_t n) { return operator new(n); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { operator delete(p); }

using namespace ev;

namespace {

volatile std::uint64_t g_sink = 0;   // results flow here so the optimiser keeps the work

std::string compiler() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string build_type() {
#ifdef EV33_BUILD_TYPE
    std::string t = EV33_BUILD_TYPE;
#else
    std::string t = "unknown";
#endif
#ifdef NDEBUG
    return t + " (NDEBUG)";
#else
    return t + " (assertions on)";
#endif
}

struct Row {
    std::string algorithm, family, dataset, variant;
    std::int64_t input_size = 0, vertices = -1, edges = -1, candidates = -1, budget = -1;
    int reps = 0;
    double median = 0, min = 0, max = 0;
    std::size_t peak = 0;
    std::string validation, note;
};

struct Harness {
    std::uint64_t seed = kBenchmarkSeed;
    int reps = 5;
    bool quick = false;
    std::string only;
    std::vector<Row> rows;
    std::string comp = compiler(), btype = build_type();

    bool wanted(const std::string &family) const { return only.empty() || only == family; }

    // Times `work` `r` times (setup excluded); validation happens later in `check`.
    template <class Setup, class Work>
    Row measure(Row row, int r, Setup setup, Work work) {
        std::vector<double> t;
        std::size_t peak = 0;
        for (int i = 0; i < r; i++) {
            auto input = setup();
            const std::size_t base = g_current;
            g_peak = g_current;
            Stopwatch w;
            std::uint64_t res = work(input);
            t.push_back(w.elapsed_ms());
            g_sink = g_sink + res;
            if (g_peak - base > peak) peak = g_peak - base;
        }
        merge_sort(t, [](double a, double b) { return a < b; });
        row.reps = r;
        row.min = t.front();
        row.max = t.back();
        row.median = t.size() % 2 ? t[t.size() / 2] : (t[t.size() / 2 - 1] + t[t.size() / 2]) / 2;
        row.peak = peak;
        return row;
    }

    void add(Row row) {
        std::printf("%-22s %-16s %-28s n=%-9lld median %10.3f ms  [%s]%s%s\n", row.algorithm.c_str(), row.family.c_str(), row.dataset.c_str(),
                    static_cast<long long>(row.input_size), row.median, row.validation.c_str(), row.note.empty() ? "" : "  ", row.note.c_str());
        std::fflush(stdout);
        rows.push_back(row);
    }

    std::string csv() const {
        std::string t = "algorithm,family,dataset,variant,input_size,vertices,edges,candidates,budget,seed,compiler,build_type,repetitions,"
                        "median_ms,min_ms,max_ms,peak_bytes,validation,note\n";
        char buf[160];
        for (const Row &r : rows) {
            t += csv_escape(r.algorithm) + "," + csv_escape(r.family) + "," + csv_escape(r.dataset) + "," + csv_escape(r.variant) + "," +
                 std::to_string(r.input_size) + ",";
            t += (r.vertices >= 0 ? std::to_string(r.vertices) : "") + "," + (r.edges >= 0 ? std::to_string(r.edges) : "") + "," +
                 (r.candidates >= 0 ? std::to_string(r.candidates) : "") + "," + (r.budget >= 0 ? std::to_string(r.budget) : "") + ",";
            t += std::to_string(seed) + "," + csv_escape(comp) + "," + csv_escape(btype) + "," + std::to_string(r.reps) + ",";
            std::snprintf(buf, sizeof buf, "%.6f,%.6f,%.6f,%zu,", r.median, r.min, r.max, r.peak);
            t += buf + csv_escape(r.validation) + "," + csv_escape(r.note) + "\n";
        }
        return t;
    }
};

std::uint64_t mix(std::uint64_t h, std::uint64_t x) { return (h ^ x) * 1099511628211ULL; }

// Optimality certificate for shortest paths: every edge satisfies d[v] <= d[u] + w and every
// tree edge is tight. Θ(E) and independent of how the distances were computed.
bool certify_sssp(const Graph &g, const ShortestPathTree &t) {
    for (int u = 0; u < g.vertex_count(); u++) {
        if (!t.reachable(u)) continue;
        for (const Arc &a : g.out(u))
            if (t.dist[static_cast<std::size_t>(a.to)] > t.dist[static_cast<std::size_t>(u)] + a.weight + 1e-6) return false;
    }
    for (int v = 0; v < g.vertex_count(); v++) {
        const int p = t.parent[static_cast<std::size_t>(v)];
        if (p < 0) continue;
        if (std::fabs(t.dist[static_cast<std::size_t>(v)] - t.dist[static_cast<std::size_t>(p)] - g.edge(t.parent_edge[static_cast<std::size_t>(v)]).weight) > 1e-6)
            return false;
    }
    return true;
}

// ------------------------------------------------------------------ families
void bench_sorting(Harness &h) {
    std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000};
    if (h.quick) sizes = {1000, 10000, 100000};
    struct Alg { const char *name; int kind; };   // 0 merge, 1..3 quick pivots
    const Alg algs[] = {{"merge_sort", 0}, {"quick_sort_median3", 1}, {"quick_sort_first", 2}, {"quick_sort_random", 3}};
    for (KeyOrder order : {KeyOrder::Random, KeyOrder::Sorted, KeyOrder::Reversed, KeyOrder::FewDistinct})
        for (std::size_t n : sizes) {
            const std::vector<std::int64_t> keys = make_sort_keys(n, order, h.seed);
            for (const Alg &a : algs) {
                const bool quadratic = a.kind == 2 && (order == KeyOrder::Sorted || order == KeyOrder::Reversed || order == KeyOrder::FewDistinct);
                if (quadratic && n > 10000) continue;   // Θ(n^2): skipped above 10,000 keys
                Row row;
                row.algorithm = a.name;
                row.family = "sorting";
                row.dataset = "keys-" + std::string(to_string(order));
                row.variant = to_string(order);
                row.input_size = static_cast<std::int64_t>(n);
                std::vector<std::int64_t> result;
                const int reps = n >= 1000000 ? 3 : h.reps;
                row = h.measure(row, reps, [&] { return keys; }, [&](std::vector<std::int64_t> v) {
                    auto less = [](std::int64_t x, std::int64_t y) { return x < y; };
                    if (a.kind == 0) merge_sort(v, less);
                    else quick_sort(v, less, a.kind == 1 ? PivotRule::MedianOfThree : a.kind == 2 ? PivotRule::First : PivotRule::Random);
                    result = v;
                    return static_cast<std::uint64_t>(v[v.size() / 2]);
                });
                std::int64_t sum_in = 0, sum_out = 0;
                for (std::int64_t k : keys) sum_in += k;
                for (std::int64_t k : result) sum_out += k;
                row.validation = is_sorted_by(result, [](std::int64_t x, std::int64_t y) { return x < y; }) && sum_in == sum_out ? "pass" : "FAIL";
                if (quadratic) row.note = "worst case (first-element pivot)";
                h.add(row);
            }
        }
}

void bench_search(Harness &h) {
    std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000};
    for (std::size_t n : sizes) {
        std::vector<std::int64_t> keys = make_sort_keys(n, KeyOrder::Sorted, h.seed);
        std::vector<std::int64_t> queries = make_sort_keys(100000, KeyOrder::Random, h.seed + 1);
        for (std::size_t i = 0; i < queries.size(); i += 2) queries[i] = keys[static_cast<std::size_t>(queries[i]) % n];   // half are hits
        for (int rec = 0; rec < 2; rec++) {
            Row row;
            row.algorithm = rec ? "binary_search_recursive" : "binary_search_iterative";
            row.family = "searching";
            row.dataset = "keys-sorted";
            row.variant = "100000 queries";
            row.input_size = static_cast<std::int64_t>(n);
            std::uint64_t hits = 0;
            row = h.measure(row, h.reps, [] { return 0; }, [&](int) {
                std::uint64_t hh = 0;
                for (std::int64_t q : queries) hh += (rec ? binary_search_recursive(keys, q) : binary_search_iterative(keys, q)) >= 0;
                hits = hh;
                return hh;
            });
            std::uint64_t expect = 0;
            for (std::int64_t q : queries) expect += lower_bound_index(keys, q) < keys.size() && keys[lower_bound_index(keys, q)] == q;
            row.validation = hits == expect ? "pass" : "FAIL";
            row.note = std::to_string(hits) + " hits";
            h.add(row);
        }
    }
}

void bench_graphs(Harness &h) {
    for (const GraphSize &gs : benchmark_graph_sizes()) {
        if (h.quick && gs.vertices > 10000) continue;
        for (GraphVariant var : {GraphVariant::ConnectedVaried, GraphVariant::ConnectedEqual, GraphVariant::Disconnected}) {
            GraphDataset d = make_graph_dataset(gs.vertices, gs.edges, var, h.seed, std::string(gs.name) + "-" + to_string(var));
            const Graph g = d.to_graph();   // the SAME graph object is used by every algorithm below
            auto base = [&](const char *alg, const char *family) {
                Row r;
                r.algorithm = alg;
                r.family = family;
                r.dataset = d.name;
                r.variant = to_string(var);
                r.input_size = gs.vertices;
                r.vertices = gs.vertices;
                r.edges = static_cast<std::int64_t>(d.edges.size());
                return r;
            };
            // ---- Dijkstra from vertex 0 (validated by an optimality certificate)
            ShortestPathTree tree;
            Row rd = h.measure(base("dijkstra", "shortest_path"), h.reps, [] { return 0; }, [&](int) {
                tree = dijkstra(g, 0);
                std::uint64_t c = 0;
                for (double x : tree.dist) c = mix(c, static_cast<std::uint64_t>(std::isfinite(x) ? x : 0));
                return c;
            });
            int reach = 0;
            for (double x : tree.dist) reach += std::isfinite(x);
            rd.validation = certify_sssp(g, tree) ? "pass" : "FAIL";
            rd.note = "source 0; " + std::to_string(reach) + " vertices reachable";
            h.add(rd);
            // ---- Bellman-Ford on the same graph (up to B4)
            if (gs.vertices <= 5000) {
                ShortestPathTree bf;
                Row rb = h.measure(base("bellman_ford", "shortest_path"), gs.vertices >= 5000 ? 1 : h.reps, [] { return 0; }, [&](int) {
                    bf = bellman_ford(g, 0);
                    return static_cast<std::uint64_t>(bf.rounds);
                });
                bool same = true;
                for (std::size_t v = 0; v < bf.dist.size(); v++) same = same && bf.dist[v] == tree.dist[v];
                rb.validation = same && !bf.negative_cycle ? "pass" : "FAIL";
                rb.note = "source 0; " + std::to_string(bf.rounds) + " rounds; equals Dijkstra";
                h.add(rb);
            }
            // ---- Floyd-Warshall (Θ(V^3): B1 and B2 only)
            if (gs.vertices <= 500) {
                AllPairsShortestPaths ap;
                Row rf = h.measure(base("floyd_warshall", "shortest_path"), 3, [] { return 0; }, [&](int) {
                    ap = floyd_warshall(g);
                    return static_cast<std::uint64_t>(ap.at(0, gs.vertices - 1) < kInfinity);
                });
                bool same = true;
                for (int v = 0; v < gs.vertices; v++) same = same && ap.at(0, v) == tree.dist[static_cast<std::size_t>(v)];
                rf.validation = same ? "pass" : "FAIL";
                rf.note = "all pairs; row 0 equals Dijkstra";
                h.add(rf);
            }
            // ---- Prim and Kruskal on the same graph
            SpanningForest fp, fk;
            Row rp = h.measure(base("prim", "spanning_tree"), h.reps, [] { return 0; }, [&](int) {
                fp = prim(g);
                return static_cast<std::uint64_t>(fp.total_weight);
            });
            Row rk = h.measure(base("kruskal", "spanning_tree"), h.reps, [] { return 0; }, [&](int) {
                fk = kruskal(g);
                return static_cast<std::uint64_t>(fk.total_weight);
            });
            const bool agree = fp.total_weight == fk.total_weight && fp.components == fk.components &&
                               static_cast<int>(fp.edges.size()) == gs.vertices - fp.components;
            const std::string what = (fp.is_spanning_tree() ? "spanning tree" : "spanning forest of " + std::to_string(fp.components) + " trees") +
                                     ", weight " + std::to_string(static_cast<long long>(fp.total_weight));
            rp.validation = rk.validation = agree ? "pass" : "FAIL";
            rp.note = rk.note = what + (agree ? "; Prim = Kruskal" : "; Prim != Kruskal");
            h.add(rp);
            h.add(rk);
        }
    }
}

// Dense graphs (half of all vertex pairs joined): all-pairs shortest paths by Floyd-Warshall
// against Dijkstra run from every source, and heap-based Prim against the O(V^2) matrix Prim.
void bench_dense(Harness &h) {
    for (int n : {100, 200, 400}) {
        GraphDataset d = make_graph_dataset(n, n * (n - 1) / 4, GraphVariant::DenseSmall, h.seed, "D" + std::to_string(n) + "-dense-small");
        const Graph g = d.to_graph();
        auto base = [&](const char *alg, const char *family) {
            Row r;
            r.algorithm = alg;
            r.family = family;
            r.dataset = d.name;
            r.variant = "dense";
            r.input_size = n;
            r.vertices = n;
            r.edges = static_cast<std::int64_t>(d.edges.size());
            return r;
        };
        AllPairsShortestPaths ap;
        Row rf = h.measure(base("floyd_warshall", "dense_graph"), 3, [] { return 0; }, [&](int) {
            ap = floyd_warshall(g);
            return static_cast<std::uint64_t>(ap.at(0, n - 1));
        });
        std::vector<ShortestPathTree> all;
        Row rd = h.measure(base("dijkstra_all_sources", "dense_graph"), 3, [] { return 0; }, [&](int) {
            all.clear();
            std::uint64_t c = 0;
            for (int s = 0; s < n; s++) {
                all.push_back(dijkstra(g, s));
                c = mix(c, static_cast<std::uint64_t>(all.back().dist[static_cast<std::size_t>(n - 1)]));
            }
            return c;
        });
        bool same = true;
        for (int s = 0; s < n && same; s++)
            for (int v = 0; v < n; v++)
                if (std::fabs(ap.at(s, v) - all[static_cast<std::size_t>(s)].dist[static_cast<std::size_t>(v)]) > 1e-9) { same = false; break; }
        rf.validation = rd.validation = same ? "pass" : "FAIL";
        rf.note = rd.note = same ? "all n x n distances agree" : "distance tables differ";
        h.add(rf);
        h.add(rd);

        std::vector<std::vector<double>> dist(static_cast<std::size_t>(n), std::vector<double>(static_cast<std::size_t>(n), kInfinity));
        for (const Edge &e : d.edges) {
            double &a = dist[static_cast<std::size_t>(e.u)][static_cast<std::size_t>(e.v)];
            a = std::min(a, e.weight);
            dist[static_cast<std::size_t>(e.v)][static_cast<std::size_t>(e.u)] = a;
        }
        SpanningForest fh, fm;
        Row rh = h.measure(base("prim", "dense_graph"), h.reps, [] { return 0; }, [&](int) {
            fh = prim(g);
            return static_cast<std::uint64_t>(fh.total_weight);
        });
        Row rm = h.measure(base("prim_dense", "dense_graph"), h.reps, [] { return 0; }, [&](int) {
            fm = prim_dense(dist);
            return static_cast<std::uint64_t>(fm.total_weight);
        });
        const bool agree = std::fabs(fh.total_weight - fm.total_weight) < 1e-6 && fh.components == fm.components;
        rh.validation = rm.validation = agree ? "pass" : "FAIL";
        rh.note = rm.note = "tree weight " + std::to_string(static_cast<long long>(fh.total_weight)) + (agree ? "; heap Prim = matrix Prim" : "; differ");
        h.add(rh);
        h.add(rm);
    }
}

void bench_knapsack(Harness &h) {
    for (std::size_t n : {25u, 50u, 100u, 200u, 400u, 800u}) {
        std::vector<PlacementItem> items = make_placement_items(n, h.seed);
        std::vector<std::int64_t> w, v;
        std::int64_t total = 0;
        for (const PlacementItem &it : items) { w.push_back(it.cost_lakh); v.push_back(it.benefit); total += it.cost_lakh; }
        for (int pct : {10, 25, 50}) {
            const std::int64_t W = total * pct / 100;
            auto base = [&](const char *alg) {
                Row r;
                r.algorithm = alg;
                r.family = "placement";
                r.dataset = "candidates-" + std::to_string(n);
                r.variant = "budget " + std::to_string(pct) + "% of total cost";
                r.input_size = static_cast<std::int64_t>(n);
                r.candidates = static_cast<std::int64_t>(n);
                r.budget = W;
                return r;
            };
            KnapsackResult dp;
            Row r1 = h.measure(base("knapsack_dp"), h.reps, [] { return 0; }, [&](int) {
                dp = knapsack_01(w, v, W);
                return static_cast<std::uint64_t>(dp.value);
            });
            std::int64_t used = 0;
            for (std::size_t i = 0; i < n; i++) used += dp.take[i] ? w[i] : 0;
            r1.validation = used <= W ? "pass" : "FAIL";
            r1.note = "benefit " + std::to_string(dp.value) + "; table " + std::to_string(n) + " x " + std::to_string(W + 1);
            h.add(r1);
            BranchBoundResult bb;
            Row r2 = h.measure(base("knapsack_bnb_lc"), h.reps, [] { return 0; }, [&](int) {
                bb = knapsack_branch_and_bound(w, v, W, Frontier::LeastCost, 2000000);
                return static_cast<std::uint64_t>(bb.value);
            });
            r2.validation = bb.stats.complete ? (bb.value == dp.value ? "pass" : "FAIL") : (bb.value <= dp.value ? "pass" : "FAIL");
            r2.note = std::to_string(bb.stats.generated) + " nodes" + (bb.stats.complete ? ", optimal" : ", node limit reached (not proven optimal)");
            h.add(r2);
            std::int64_t gval = 0;
            Row r3 = h.measure(base("greedy_ratio"), h.reps, [] { return 0; }, [&](int) {
                std::vector<std::size_t> ord(n);
                for (std::size_t i = 0; i < n; i++) ord[i] = i;
                merge_sort(ord, [&](std::size_t a, std::size_t b) { return v[a] * w[b] > v[b] * w[a] || (v[a] * w[b] == v[b] * w[a] && a < b); });
                std::int64_t cw = 0, cv = 0;
                for (std::size_t i : ord)
                    if (cw + w[i] <= W) { cw += w[i]; cv += v[i]; }
                gval = cv;
                return static_cast<std::uint64_t>(cv);
            });
            r3.validation = gval <= dp.value ? "pass" : "FAIL";
            char pct_s[32];
            std::snprintf(pct_s, sizeof pct_s, "%.2f", 100.0 * static_cast<double>(gval) / static_cast<double>(dp.value ? dp.value : 1));
            r3.note = "benefit " + std::to_string(gval) + " = " + pct_s + "% of optimal";
            h.add(r3);
        }
    }
}

void bench_sessions(Harness &h) {
    std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000};
    if (h.quick) sizes = {1000, 10000, 100000};
    for (std::size_t n : sizes) {
        std::vector<SessionItem> s = make_sessions(n, h.seed);
        std::vector<Interval> iv;
        for (const SessionItem &x : s) iv.push_back({x.id, static_cast<double>(x.start_min), static_cast<double>(x.end_min)});
        auto base = [&](const char *alg) {
            Row r;
            r.algorithm = alg;
            r.family = "scheduling";
            r.dataset = "sessions-" + std::to_string(n);
            r.variant = "one day";
            r.input_size = static_cast<std::int64_t>(n);
            return r;
        };
        const int reps = n >= 1000000 ? 3 : h.reps;
        std::vector<std::size_t> pick;
        Row a = h.measure(base("activity_selection"), reps, [] { return 0; }, [&](int) {
            pick = activity_selection(iv);
            return static_cast<std::uint64_t>(pick.size());
        });
        bool ok = true;
        for (std::size_t k = 1; k < pick.size(); k++) ok = ok && iv[pick[k]].start >= iv[pick[k - 1]].end;
        a.validation = ok ? "pass" : "FAIL";
        a.note = std::to_string(pick.size()) + " sessions on one bay";
        h.add(a);
        PartitionResult part;
        Row b = h.measure(base("interval_partitioning"), reps, [] { return 0; }, [&](int) {
            part = interval_partitioning(iv);
            return static_cast<std::uint64_t>(part.resources);
        });
        const int depth = max_overlap(iv);
        b.validation = part.resources == depth ? "pass" : "FAIL";
        b.note = std::to_string(part.resources) + " bays = maximum overlap " + std::to_string(depth);
        h.add(b);
    }
}

void bench_matrix(Harness &h) {
    Rng rng(h.seed);
    for (std::size_t n : {32u, 64u, 128u, 256u, 512u}) {
        if (h.quick && n > 256) continue;
        Matrix A(n, n), B(n, n);
        for (std::size_t i = 0; i < n; i++)
            for (std::size_t j = 0; j < n; j++) { A(i, j) = rng.uniform(-1, 1); B(i, j) = rng.uniform(-1, 1); }
        Matrix c1, c2;
        auto base = [&](const char *alg) {
            Row r;
            r.algorithm = alg;
            r.family = "matrix";
            r.dataset = "random-" + std::to_string(n) + "x" + std::to_string(n);
            r.variant = "dense";
            r.input_size = static_cast<std::int64_t>(n);
            return r;
        };
        const int reps = n >= 512 ? 2 : 3;
        Row a = h.measure(base("matmul_classical"), reps, [] { return 0; }, [&](int) {
            c1 = multiply_classical(A, B);
            return static_cast<std::uint64_t>(c1(0, 0) * 1000);
        });
        Row b = h.measure(base("matmul_strassen_cutoff64"), reps, [] { return 0; }, [&](int) {
            c2 = multiply_strassen(A, B, 64);
            return static_cast<std::uint64_t>(c2(0, 0) * 1000);
        });
        const double diff = c1.max_abs_diff(c2);
        a.validation = b.validation = diff < 1e-8 ? "pass" : "FAIL";
        char note[64];
        std::snprintf(note, sizeof note, "max |classical - Strassen| = %.2e", diff);
        a.note = b.note = note;
        h.add(a);
        h.add(b);
    }
}

void bench_planner(Harness &h) {
    for (const char *profile : {"small", "medium", "large"}) {
        Scenario s = generate_scenario(profile, 33);
        ScenarioContext ctx(s);
        RunOptions o;
        PlanningRun run(ctx, o);   // placement fixed for both planners
        const std::size_t sample = std::min<std::size_t>(s.trips.size(), 200);
        auto base = [&](const char *alg) {
            Row r;
            r.algorithm = alg;
            r.family = "trip_planning";
            r.dataset = std::string("scenario-") + profile;
            r.variant = std::to_string(sample) + " trips";
            r.input_size = static_cast<std::int64_t>(sample);
            r.vertices = s.network.node_count();
            r.edges = s.network.edge_count();
            r.candidates = static_cast<std::int64_t>(s.candidates.size());
            r.budget = s.config.budget / kPaisePerLakh;
            return r;
        };
        int feas_leg = 0, feas_exact = 0, unsafe = 0;
        Row a = h.measure(base("leg_planner_dijkstra"), 3, [] { return 0; }, [&](int) {
            Router router(ctx.model, RoutingMethod::Dijkstra);
            LegPlanner lp(s, router, run.sites);
            int f = 0;
            for (std::size_t i = 0; i < sample; i++) {
                TripPlan p = lp.plan(s.trips[i]);
                f += p.feasible();
                const ElectricVehicle &v = s.vehicles[static_cast<std::size_t>(s.vehicle_index(p.vehicle_id))];
                if (p.feasible() && p.min_kwh < v.reserve_kwh - 1e-9) unsafe++;
            }
            feas_leg = f;
            return static_cast<std::uint64_t>(f);
        });
        Row b = h.measure(base("exact_state_space"), 3, [] { return 0; }, [&](int) {
            ExactPlanner ex(s, ctx.model, run.sites);
            int f = 0;
            for (std::size_t i = 0; i < sample; i++) {
                TripPlan p = ex.plan(s.trips[i]);
                f += p.feasible();
                const ElectricVehicle &v = s.vehicles[static_cast<std::size_t>(s.vehicle_index(p.vehicle_id))];
                if (p.feasible() && p.min_kwh < v.reserve_kwh - 1e-9) unsafe++;
            }
            feas_exact = f;
            return static_cast<std::uint64_t>(f);
        });
        a.validation = b.validation = unsafe == 0 ? "pass" : "FAIL";
        a.note = std::to_string(feas_leg) + " feasible (battery step n/a)";
        b.note = std::to_string(feas_exact) + " feasible (battery step " + std::to_string(s.config.battery_step_kwh).substr(0, 4) + " kWh)";
        h.add(a);
        h.add(b);
    }
}

} // namespace

int main(int argc, char **argv) {
    Harness h;
    std::string out = "benchmarks/results.csv";
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--quick") h.quick = true;
        else if (a == "--only" && i + 1 < argc) h.only = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--reps" && i + 1 < argc) h.reps = std::max(1, std::atoi(argv[++i]));
        else if (a == "--seed" && i + 1 < argc) h.seed = static_cast<std::uint64_t>(std::strtoull(argv[++i], nullptr, 10));
        else {
            std::cerr << "usage: ev33_bench [--quick] [--only sorting|searching|graphs|dense|placement|scheduling|matrix|planning] [--reps N] [--seed N] [--out FILE]\n";
            return 1;
        }
    }
    std::printf("EV33 benchmarks: seed %llu, %s, %s, %d repetitions (median reported)\n", static_cast<unsigned long long>(h.seed),
                h.comp.c_str(), h.btype.c_str(), h.reps);
    Stopwatch total;
    if (h.wanted("sorting")) bench_sorting(h);
    if (h.wanted("searching")) bench_search(h);
    if (h.wanted("graphs")) bench_graphs(h);
    if (h.wanted("dense")) bench_dense(h);
    if (h.wanted("placement")) bench_knapsack(h);
    if (h.wanted("scheduling")) bench_sessions(h);
    if (h.wanted("matrix")) bench_matrix(h);
    if (h.wanted("planning")) bench_planner(h);
    int failed = 0;
    for (const Row &r : h.rows) failed += r.validation != "pass";
    write_text_file(out, h.csv());
    std::printf("\n%zu measurements, %d validation failures, %.1f s; written to %s\n", h.rows.size(), failed, total.elapsed_ms() / 1000, out.c_str());
    return failed ? 2 : 0;
}
