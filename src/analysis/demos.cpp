// demos.cpp - textbook examples for every algorithm, with the expected answers.
#include "ev/analysis/demos.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "ev/algorithms/backtracking.hpp"
#include "ev/algorithms/branch_and_bound.hpp"
#include "ev/algorithms/divide_conquer.hpp"
#include "ev/algorithms/dynamic_programming.hpp"
#include "ev/algorithms/greedy.hpp"
#include "ev/algorithms/number_theory.hpp"
#include "ev/algorithms/searching.hpp"
#include "ev/algorithms/sorting.hpp"
#include "ev/algorithms/string_matching.hpp"
#include "ev/graph/max_flow.hpp"
#include "ev/graph/shortest_paths.hpp"
#include "ev/graph/spanning_tree.hpp"

namespace ev {

namespace {

struct Demo {
    std::ostream &out;
    int failures = 0;
    void check(const std::string &name, const std::string &input, const std::string &got, const std::string &expected) {
        const bool ok = got == expected;
        if (!ok) failures++;
        out << (ok ? "  [ok]   " : "  [FAIL] ") << name << "\n         input:    " << input << "\n         result:   " << got
            << "\n         expected: " << expected << "\n";
    }
};

template <class T>
std::string join(const std::vector<T> &v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

std::string num(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

} // namespace

int run_algorithm_demos(std::ostream &out) {
    Demo d{out};
    out << "EV33 algorithm demonstrations (textbook inputs, answers checkable by hand)\n";

    out << "\nSorting and searching\n";
    std::vector<int> a = {38, 27, 43, 3, 9, 82, 10};
    std::vector<int> m = a, q = a;
    merge_sort(m);
    quick_sort(q);
    d.check("merge sort", join(a), join(m), "[3, 9, 10, 27, 38, 43, 82]");
    d.check("quick sort (median of three)", join(a), join(q), "[3, 9, 10, 27, 38, 43, 82]");
    d.check("binary search (iterative) for 27", join(m), std::to_string(binary_search_iterative(m, 27)), "3");
    d.check("binary search (recursive) for 50", join(m), std::to_string(binary_search_recursive(m, 50)), "-1");

    out << "\nDivide and conquer\n";
    std::vector<double> arr = {-2, 1, -3, 4, -1, 2, 1, -5, 4};
    SubarrayResult sa = max_subarray_dc(arr);
    d.check("maximum subarray", "[-2, 1, -3, 4, -1, 2, 1, -5, 4]",
            "sum " + num(sa.sum) + " at [" + std::to_string(sa.lo) + ".." + std::to_string(sa.hi) + "]", "sum 6 at [3..6]");
    d.check("fast power 3^13 mod 1000", "3, 13, 1000", std::to_string(powmod(3, 13, 1000)), "323");
    std::vector<std::vector<int>> knows = {{0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}, {1, 0, 1, 0}};
    int celeb = find_celebrity(4, [&](int x, int y) { return knows[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)] == 1; });
    d.check("celebrity", "4 people; everyone knows 2, 2 knows nobody", std::to_string(celeb), "2");
    Matrix A(2, 2), B(2, 2);
    A(0, 0) = 1; A(0, 1) = 2; A(1, 0) = 3; A(1, 1) = 4;
    B(0, 0) = 5; B(0, 1) = 6; B(1, 0) = 7; B(1, 1) = 8;
    Matrix C1 = multiply_classical(A, B), C2 = multiply_strassen(A, B, 1);
    auto mstr = [](const Matrix &x) { return "[[" + num(x(0, 0)) + ", " + num(x(0, 1)) + "], [" + num(x(1, 0)) + ", " + num(x(1, 1)) + "]]"; };
    d.check("classical matrix product", "[[1,2],[3,4]] x [[5,6],[7,8]]", mstr(C1), "[[19, 22], [43, 50]]");
    d.check("Strassen matrix product", "[[1,2],[3,4]] x [[5,6],[7,8]]", mstr(C2), "[[19, 22], [43, 50]]");

    out << "\nGreedy\n";
    std::vector<Interval> iv = {{1, 1, 4}, {2, 3, 5}, {3, 0, 6}, {4, 5, 7}, {5, 3, 9}, {6, 5, 9}, {7, 6, 10}, {8, 8, 11}, {9, 8, 12}, {10, 2, 14}, {11, 12, 16}};
    d.check("activity selection (CLRS 16.1)", "11 activities", std::to_string(activity_selection(iv).size()), "4");
    std::vector<Interval> lec = {{1, 9, 10.5}, {2, 9, 12.5}, {3, 9, 10.5}, {4, 11, 12.5}, {5, 11, 14}, {6, 13, 14.5}, {7, 13, 14.5}, {8, 14, 16.5}, {9, 15, 16.5}, {10, 15, 16.5}};
    d.check("interval partitioning (KT 4.1)", "10 lectures", std::to_string(interval_partitioning(lec).resources), "3");
    HuffmanCode h(std::map<unsigned char, std::uint64_t>{{'a', 45}, {'b', 13}, {'c', 12}, {'d', 16}, {'e', 9}, {'f', 5}});
    d.check("Huffman total bits (CLRS 16.3)", "a:45 b:13 c:12 d:16 e:9 f:5", std::to_string(h.total_bits()), "224");

    out << "\nGraphs\n";
    Graph g(5, true);
    g.add_edge(0, 1, 10); g.add_edge(0, 3, 5); g.add_edge(1, 2, 1); g.add_edge(1, 3, 2); g.add_edge(2, 4, 4);
    g.add_edge(3, 1, 3); g.add_edge(3, 2, 9); g.add_edge(3, 4, 2); g.add_edge(4, 0, 7); g.add_edge(4, 2, 6);
    ShortestPathTree t = dijkstra(g, 0);
    std::vector<int> dist;
    for (double x : t.dist) dist.push_back(static_cast<int>(x));
    d.check("Dijkstra (CLRS 24.6)", "5 vertices, 10 arcs, source 0", join(dist), "[0, 8, 9, 5, 7]");
    d.check("Dijkstra route 0 -> 2", "same graph", join(t.path_to(2)), "[0, 3, 1, 2]");
    Graph bf(5, true);
    bf.add_edge(0, 1, 6); bf.add_edge(0, 3, 7); bf.add_edge(1, 2, 5); bf.add_edge(1, 3, 8); bf.add_edge(1, 4, -4);
    bf.add_edge(2, 1, -2); bf.add_edge(3, 2, -3); bf.add_edge(3, 4, 9); bf.add_edge(4, 0, 2); bf.add_edge(4, 2, 7);
    ShortestPathTree tb = bellman_ford(bf, 0);
    std::vector<int> db;
    for (double x : tb.dist) db.push_back(static_cast<int>(x));
    d.check("Bellman-Ford with negative arcs (CLRS 24.4)", "5 vertices, source 0", join(db), "[0, 2, 4, 7, -2]");
    Graph u(9, false);
    const int E[14][3] = {{0, 1, 4}, {0, 7, 8}, {1, 2, 8}, {1, 7, 11}, {2, 3, 7}, {2, 8, 2}, {2, 5, 4}, {3, 4, 9}, {3, 5, 14}, {4, 5, 10}, {5, 6, 2}, {6, 7, 1}, {6, 8, 6}, {7, 8, 7}};
    for (const auto &e : E) u.add_edge(e[0], e[1], e[2]);
    d.check("Prim MST weight (CLRS 23.4)", "9 vertices, 14 edges", num(prim(u).total_weight), "37");
    d.check("Kruskal MST weight (CLRS 23.4)", "same graph", num(kruskal(u).total_weight), "37");
    FlowNetwork fn(6);
    const int F[9][3] = {{0, 1, 16}, {0, 2, 13}, {1, 3, 12}, {2, 1, 4}, {2, 4, 14}, {3, 2, 9}, {3, 5, 20}, {4, 3, 7}, {4, 5, 4}};
    for (const auto &e : F) fn.add_edge(e[0], e[1], e[2]);
    d.check("Edmonds-Karp max flow (CLRS 26.6)", "6 vertices", std::to_string(fn.max_flow(0, 5, FlowMethod::EdmondsKarpBfs).value), "23");

    out << "\nDynamic programming\n";
    KnapsackResult k = knapsack_01({10, 20, 30}, {60, 100, 120}, 50);
    d.check("0/1 knapsack", "weights [10,20,30], values [60,100,120], capacity 50", std::to_string(k.value), "220");
    MatrixChainResult mc = matrix_chain_order({30, 35, 15, 5, 10, 20, 25});
    d.check("matrix-chain order (CLRS 15.2)", "dims [30,35,15,5,10,20,25]", std::to_string(mc.cost) + " " + mc.order, "15125 ((A1(A2A3))((A4A5)A6))");
    d.check("LCS (CLRS 15.4)", "ABCBDAB vs BDCABA", std::to_string(lcs(std::string("ABCBDAB"), std::string("BDCABA")).size()), "4");
    double obst = optimal_bst({0, 0.15, 0.10, 0.05, 0.10, 0.20}, {0.05, 0.10, 0.05, 0.05, 0.05, 0.10});
    d.check("optimal BST expected cost (CLRS 15.5)", "5 keys", num(std::round(obst * 100) / 100), "2.75");
    std::vector<std::vector<double>> tsp = {{0, 10, 15, 20}, {10, 0, 35, 25}, {15, 35, 0, 30}, {20, 25, 30, 0}};
    d.check("Held-Karp TSP", "4 cities", num(tsp_held_karp(tsp).length), "80");

    out << "\nBacktracking and branch-and-bound\n";
    d.check("N-Queens(8) solutions", "8x8 board", std::to_string(nqueens_count(8)), "92");
    SubsetSumResult ss = subset_sum_backtracking({3, 34, 4, 12, 5, 2}, 9);
    d.check("subset sum", "{3,34,4,12,5,2}, target 9", ss.outcome == SearchOutcome::Found ? "found" : "not found", "found");
    BranchBoundResult bb = knapsack_branch_and_bound({10, 20, 30}, {60, 100, 120}, 50, Frontier::LeastCost);
    d.check("knapsack branch-and-bound (least cost)", "same as the DP example", std::to_string(bb.value), "220");

    out << "\nNumber theory and string matching\n";
    CrtResult cr = chinese_remainder({2, 3, 2}, {3, 5, 7});
    d.check("Chinese Remainder Theorem", "x = 2 mod 3, 3 mod 5, 2 mod 7", std::to_string(cr.x) + " mod " + std::to_string(cr.modulus), "23 mod 105");
    std::vector<std::size_t> hits = match_kmp("ABABDABACDABABCABAB", "ABABCABAB");
    std::vector<long> hl(hits.begin(), hits.end());
    d.check("KMP", "text ABABDABACDABABCABAB, pattern ABABCABAB", join(hl), "[10]");
    std::vector<std::size_t> rk = match_rabin_karp("AABAACAADAABAABA", "AABA");
    std::vector<long> rl(rk.begin(), rk.end());
    d.check("Rabin-Karp (overlapping matches)", "AABAACAADAABAABA / AABA", join(rl), "[0, 9, 12]");

    out << "\n" << (d.failures == 0 ? "All demonstrations match their expected answers." : std::to_string(d.failures) + " demonstration(s) differ!") << "\n";
    return d.failures;
}

} // namespace ev
