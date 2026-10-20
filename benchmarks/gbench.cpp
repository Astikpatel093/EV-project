// ev33_gbench - Google Benchmark micro-benchmarks (optional; the reproducible CSV comes from
// ev33_bench). Google Benchmark chooses the iteration count itself and reports mean CPU time.
//
//   ./build/benchmarks/ev33_gbench --benchmark_filter=Dijkstra
#include <benchmark/benchmark.h>

#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/greedy.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/datagen/benchmark_generator.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/graph/spanning_tree.hpp"

using namespace ev;

static void BM_MergeSort(benchmark::State &st) {
    const auto keys = make_sort_keys(static_cast<std::size_t>(st.range(0)), KeyOrder::Random, kBenchmarkSeed);
    for (auto _ : st) {
        auto v = keys;
        merge_sort(v, [](std::int64_t a, std::int64_t b) { return a < b; });
        benchmark::DoNotOptimize(v.data());
    }
    st.SetComplexityN(st.range(0));
}
BENCHMARK(BM_MergeSort)->RangeMultiplier(10)->Range(1000, 1000000)->Complexity(benchmark::oNLogN);

static void BM_QuickSort(benchmark::State &st) {
    const auto keys = make_sort_keys(static_cast<std::size_t>(st.range(0)), KeyOrder::Random, kBenchmarkSeed);
    for (auto _ : st) {
        auto v = keys;
        quick_sort(v, [](std::int64_t a, std::int64_t b) { return a < b; });
        benchmark::DoNotOptimize(v.data());
    }
    st.SetComplexityN(st.range(0));
}
BENCHMARK(BM_QuickSort)->RangeMultiplier(10)->Range(1000, 1000000)->Complexity(benchmark::oNLogN);

static void BM_Dijkstra(benchmark::State &st) {
    const auto &size = benchmark_graph_sizes()[static_cast<std::size_t>(st.range(0))];
    const Graph g = make_graph_dataset(size.vertices, size.edges, GraphVariant::ConnectedVaried, kBenchmarkSeed).to_graph();
    for (auto _ : st) benchmark::DoNotOptimize(dijkstra(g, 0).dist.data());
    st.SetLabel(size.name);
}
BENCHMARK(BM_Dijkstra)->DenseRange(0, 5);

static void BM_Kruskal(benchmark::State &st) {
    const auto &size = benchmark_graph_sizes()[static_cast<std::size_t>(st.range(0))];
    const Graph g = make_graph_dataset(size.vertices, size.edges, GraphVariant::ConnectedVaried, kBenchmarkSeed).to_graph();
    for (auto _ : st) benchmark::DoNotOptimize(kruskal(g).total_weight);
    st.SetLabel(size.name);
}
BENCHMARK(BM_Kruskal)->DenseRange(0, 5);

static void BM_Prim(benchmark::State &st) {
    const auto &size = benchmark_graph_sizes()[static_cast<std::size_t>(st.range(0))];
    const Graph g = make_graph_dataset(size.vertices, size.edges, GraphVariant::ConnectedVaried, kBenchmarkSeed).to_graph();
    for (auto _ : st) benchmark::DoNotOptimize(prim(g).total_weight);
    st.SetLabel(size.name);
}
BENCHMARK(BM_Prim)->DenseRange(0, 5);

static void BM_KnapsackDP(benchmark::State &st) {
    const auto items = make_placement_items(static_cast<std::size_t>(st.range(0)), kBenchmarkSeed);
    std::vector<std::int64_t> w, v;
    std::int64_t total = 0;
    for (const auto &it : items) { w.push_back(it.cost_lakh); v.push_back(it.benefit); total += it.cost_lakh; }
    for (auto _ : st) benchmark::DoNotOptimize(knapsack_01(w, v, total / 4).value);
}
BENCHMARK(BM_KnapsackDP)->RangeMultiplier(2)->Range(25, 800);

static void BM_IntervalPartitioning(benchmark::State &st) {
    const auto s = make_sessions(static_cast<std::size_t>(st.range(0)), kBenchmarkSeed);
    std::vector<Interval> iv;
    for (const auto &x : s) iv.push_back({x.id, static_cast<double>(x.start_min), static_cast<double>(x.end_min)});
    for (auto _ : st) benchmark::DoNotOptimize(interval_partitioning(iv).resources);
}
BENCHMARK(BM_IntervalPartitioning)->RangeMultiplier(10)->Range(1000, 1000000);

BENCHMARK_MAIN();
