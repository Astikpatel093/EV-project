// branch_and_bound.cpp - knapsack branch-and-bound with FIFO, LIFO and least-cost frontiers.
#include "ev/algorithms/branch_and_bound.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/algorithms/heap.hpp"
#include "ev/algorithms/sorting.hpp"

namespace ev {

const char *frontier_name(Frontier f) {
    switch (f) {
    case Frontier::FIFO: return "FIFO";
    case Frontier::LIFO: return "LIFO";
    default: return "LC";
    }
}

namespace {

struct Node {
    std::size_t level;     // items order[0..level-1] are decided
    long parent;           // index into the node pool, -1 for the root
    std::int64_t weight, value;
    double bound;
    bool took;             // decision taken on item order[level-1]
};

} // namespace

BranchBoundResult knapsack_branch_and_bound(const std::vector<std::int64_t> &w, const std::vector<std::int64_t> &v,
                                            std::int64_t W, Frontier mode, std::uint64_t node_limit) {
    if (w.size() != v.size()) throw std::invalid_argument("knapsack: weights and values differ in length");
    if (W < 0) throw std::invalid_argument("knapsack: capacity must not be negative");
    for (std::size_t i = 0; i < w.size(); i++)
        if (w[i] < 0 || v[i] < 0) throw std::invalid_argument("knapsack: item " + std::to_string(i) + " is negative");
    if (node_limit == 0) node_limit = 4000000;
    const std::size_t n = w.size();

    // decreasing value/weight; zero-weight items first (infinite ratio); ties by index
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; i++) order[i] = i;
    merge_sort(order, [&](std::size_t i, std::size_t j) {
        if ((w[i] == 0) != (w[j] == 0)) return w[i] == 0;
        long double li = static_cast<long double>(v[i]) * w[j], lj = static_cast<long double>(v[j]) * w[i];
        if (li != lj) return li > lj;
        return i < j;
    });

    auto bound = [&](std::size_t level, std::int64_t weight, std::int64_t value) {
        double b = static_cast<double>(value);
        std::int64_t cw = weight;
        for (std::size_t t = level; t < n; t++) {
            std::size_t i = order[t];
            if (cw + w[i] <= W) { cw += w[i]; b += static_cast<double>(v[i]); }
            else { b += static_cast<double>(v[i]) * static_cast<double>(W - cw) / static_cast<double>(w[i]); break; }
        }
        return b;
    };

    BranchBoundResult res;
    res.take.assign(n, false);
    // incumbent: greedy fill in ratio order
    std::vector<bool> greedy(n, false);
    std::int64_t gw = 0, best = 0;
    for (std::size_t i : order)
        if (gw + w[i] <= W) { gw += w[i]; best += v[i]; greedy[i] = true; }
    long best_node = -1;

    std::vector<Node> pool;
    auto make = [&](std::size_t level, long parent, std::int64_t weight, std::int64_t value, bool took) {
        pool.push_back({level, parent, weight, value, bound(level, weight, value), took});
        res.stats.generated++;
        return static_cast<long>(pool.size()) - 1;
    };
    auto promising = [&](long x) { return std::floor(pool[static_cast<std::size_t>(x)].bound + 1e-9) > static_cast<double>(best); };

    // frontier containers; only the one matching `mode` is used
    std::vector<long> fifo;
    std::size_t fifo_head = 0;
    std::vector<long> stack;
    struct ByBound {
        const std::vector<Node> *pool;
        bool operator()(long a, long b) const {   // "less" for the heap = higher bound first
            const Node &x = (*pool)[static_cast<std::size_t>(a)], &y = (*pool)[static_cast<std::size_t>(b)];
            return x.bound > y.bound || (x.bound == y.bound && a < b);
        }
    };
    BinaryHeap<long, ByBound> heap(ByBound{&pool});

    auto push = [&](long x) {
        if (mode == Frontier::FIFO) fifo.push_back(x);
        else if (mode == Frontier::LIFO) stack.push_back(x);
        else heap.push(x);
    };
    auto size = [&]() -> std::size_t {
        if (mode == Frontier::FIFO) return fifo.size() - fifo_head;
        if (mode == Frontier::LIFO) return stack.size();
        return heap.size();
    };
    auto pop = [&]() -> long {
        if (mode == Frontier::FIFO) return fifo[fifo_head++];
        if (mode == Frontier::LIFO) { long x = stack.back(); stack.pop_back(); return x; }
        return heap.pop();
    };

    push(make(0, -1, 0, 0, false));
    while (size() > 0) {
        if (size() > res.stats.max_frontier) res.stats.max_frontier = size();
        long x = pop();
        if (!promising(x)) {
            if (mode == Frontier::LeastCost) break;   // every remaining bound is no better
            continue;
        }
        const std::size_t lv = pool[static_cast<std::size_t>(x)].level;
        if (lv >= n) continue;
        if (res.stats.generated + 2 > node_limit) { res.stats.complete = false; break; }
        res.stats.expanded++;
        const std::size_t item = order[lv];
        const std::int64_t pw = pool[static_cast<std::size_t>(x)].weight, pv = pool[static_cast<std::size_t>(x)].value;
        long kids[2];
        int nk = 0;
        if (pw + w[item] <= W) kids[nk++] = make(lv + 1, x, pw + w[item], pv + v[item], true);   // include
        kids[nk++] = make(lv + 1, x, pw, pv, false);                                            // exclude
        // LIFO pops the last pushed child first: push exclude first so include is explored first
        if (mode == Frontier::LIFO && nk == 2) { long t = kids[0]; kids[0] = kids[1]; kids[1] = t; }
        for (int t = 0; t < nk; t++) {
            long c = kids[t];
            const Node &cn = pool[static_cast<std::size_t>(c)];
            if (cn.value > best) { best = cn.value; best_node = c; }
            if (cn.level < n && promising(c)) push(c);
        }
        // FIFO: drop consumed prefix now and then to bound memory
        if (mode == Frontier::FIFO && fifo_head > 4096 && fifo_head * 2 > fifo.size()) {
            fifo.erase(fifo.begin(), fifo.begin() + static_cast<long>(fifo_head));
            fifo_head = 0;
        }
    }

    res.value = best;
    if (best_node < 0) {
        res.take = greedy;
    } else {
        for (long x = best_node; x > 0; x = pool[static_cast<std::size_t>(x)].parent)
            if (pool[static_cast<std::size_t>(x)].took) res.take[order[pool[static_cast<std::size_t>(x)].level - 1]] = true;
    }
    for (std::size_t i = 0; i < n; i++)
        if (res.take[i]) res.weight += w[i];
    return res;
}

} // namespace ev
