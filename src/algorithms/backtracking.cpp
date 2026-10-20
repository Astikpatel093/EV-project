// backtracking.cpp - N-Queens and subset sum.
#include "ev/algorithms/backtracking.hpp"

#include <stdexcept>

#include "ev/algorithms/sorting.hpp"

namespace ev {

namespace {

struct Queens {
    int n = 0;
    std::uint32_t full = 0;
    std::uint64_t count = 0, nodes = 0;
    std::vector<int> cols;
    std::vector<int> *first = nullptr;
    bool have_first = false;

    // colmask: columns in use; d1/d2: diagonals attacked in this row
    void place(int row, std::uint32_t colmask, std::uint32_t d1, std::uint32_t d2) {
        nodes++;
        if (row == n) {
            if (first && !have_first) { *first = cols; have_first = true; }
            count++;
            return;
        }
        std::uint32_t avail = full & ~(colmask | d1 | d2);
        while (avail) {
            std::uint32_t bit = avail & (0u - avail);   // lowest free column
            avail ^= bit;
            int col = 0;
            while (!((bit >> col) & 1u)) col++;
            cols[static_cast<std::size_t>(row)] = col;
            place(row + 1, colmask | bit, ((d1 | bit) << 1) & full, (d2 | bit) >> 1);
        }
    }
};

struct SubsetSearch {
    std::vector<std::int64_t> v;        // items, decreasing
    std::vector<std::int64_t> suffix;   // suffix[i] = v[i] + ... + v[n-1]
    std::vector<bool> pick;
    std::int64_t target;
    std::uint64_t nodes = 0, limit;
    bool aborted = false;

    bool rec(std::size_t i, std::int64_t sum) {
        nodes++;
        if (limit && nodes > limit) { aborted = true; return false; }
        if (sum == target) return true;
        if (i == v.size()) return false;
        if (sum + suffix[i] < target) return false;   // even taking everything left is too little
        if (sum + v[i] <= target) {
            pick[i] = true;
            if (rec(i + 1, sum + v[i])) return true;
            pick[i] = false;
            if (aborted) return false;
        }
        return rec(i + 1, sum);
    }
};

} // namespace

std::uint64_t nqueens_count(int n, std::vector<int> *first, std::uint64_t *nodes) {
    if (n < 0 || n > 20) throw std::invalid_argument("N-Queens supports 0 <= n <= 20");
    if (n == 0) {
        if (nodes) *nodes = 1;
        if (first) first->clear();
        return 1;
    }
    Queens q;
    q.n = n;
    q.full = (1u << n) - 1u;
    q.cols.assign(static_cast<std::size_t>(n), -1);
    q.first = first;
    if (first) first->clear();
    q.place(0, 0, 0, 0);
    if (nodes) *nodes = q.nodes;
    return q.count;
}

SubsetSumResult subset_sum_backtracking(const std::vector<std::int64_t> &items, std::int64_t target,
                                        std::uint64_t node_limit) {
    for (std::int64_t x : items)
        if (x < 0) throw std::invalid_argument("subset sum items must not be negative");
    SubsetSumResult res;
    res.take.assign(items.size(), false);
    if (target < 0) return res;
    std::vector<std::size_t> order(items.size());
    for (std::size_t i = 0; i < order.size(); i++) order[i] = i;
    merge_sort(order, [&](std::size_t a, std::size_t b) { return items[a] > items[b] || (items[a] == items[b] && a < b); });
    SubsetSearch s;
    s.target = target;
    s.limit = node_limit;
    for (std::size_t i : order) s.v.push_back(items[i]);
    s.suffix.assign(s.v.size() + 1, 0);
    for (std::size_t i = s.v.size(); i-- > 0;) s.suffix[i] = s.suffix[i + 1] + s.v[i];
    s.pick.assign(s.v.size(), false);
    bool ok = s.rec(0, 0);
    res.nodes = s.nodes;
    if (ok) {
        res.outcome = SearchOutcome::Found;
        for (std::size_t k = 0; k < order.size(); k++)
            if (s.pick[k]) res.take[order[k]] = true;
    } else {
        res.outcome = s.aborted ? SearchOutcome::NodeLimit : SearchOutcome::Impossible;
    }
    return res;
}

bool subset_sum_dp(const std::vector<std::int64_t> &items, std::int64_t target) {
    for (std::int64_t x : items)
        if (x < 0) throw std::invalid_argument("subset sum items must not be negative");
    if (target < 0) return false;
    if (target > 50000000) throw std::length_error("subset sum DP table too large");
    std::vector<bool> reach(static_cast<std::size_t>(target) + 1, false);
    reach[0] = true;
    for (std::int64_t x : items) {
        if (x == 0 || x > target) continue;
        for (std::int64_t s = target; s >= x; s--)
            if (reach[static_cast<std::size_t>(s - x)]) reach[static_cast<std::size_t>(s)] = true;
    }
    return reach[static_cast<std::size_t>(target)];
}

} // namespace ev
