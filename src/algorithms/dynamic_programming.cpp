// dynamic_programming.cpp - knapsack, matrix chain, LCS, optimal BST, Held-Karp.
#include "ev/algorithms/dynamic_programming.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace ev {

// ======================================================================== knapsack
static void check_items(const std::vector<std::int64_t> &w, const std::vector<std::int64_t> &v, std::int64_t cap) {
    if (w.size() != v.size()) throw std::invalid_argument("knapsack: weights and values differ in length");
    if (cap < 0) throw std::invalid_argument("knapsack: capacity must not be negative");
    for (std::size_t i = 0; i < w.size(); i++) {
        if (w[i] < 0) throw std::invalid_argument("knapsack: item " + std::to_string(i) + " has a negative weight");
        if (v[i] < 0) throw std::invalid_argument("knapsack: item " + std::to_string(i) + " has a negative value");
    }
}

KnapsackResult knapsack_01(const std::vector<std::int64_t> &w, const std::vector<std::int64_t> &v, std::int64_t cap,
                           std::int64_t max_cells) {
    check_items(w, v, cap);
    const std::size_t n = w.size();
    KnapsackResult res;
    res.take.assign(n, false);
    if (n == 0 || cap == 0) {
        // only zero-weight items fit; take those with positive value
        for (std::size_t i = 0; i < n; i++)
            if (w[i] == 0 && v[i] > 0) { res.take[i] = true; res.value += v[i]; }
        return res;
    }
    const double cells = static_cast<double>(n) * (static_cast<double>(cap) + 1.0);
    if (cells > static_cast<double>(max_cells))
        throw std::length_error("knapsack table would need " + std::to_string(static_cast<long long>(cells)) +
                                " cells; scale the weights or use branch-and-bound");
    const std::size_t W = static_cast<std::size_t>(cap);
    std::vector<std::int64_t> best(W + 1, 0);
    // took[i][c]: item i improved best[c] when processed -> used to rebuild the chosen set
    std::vector<std::vector<bool>> took(n, std::vector<bool>(W + 1, false));
    for (std::size_t i = 0; i < n; i++) {
        const std::size_t wi = static_cast<std::size_t>(w[i]);
        if (w[i] > cap) continue;
        // iterate capacities downwards so each item is used at most once
        for (std::size_t c = W + 1; c-- > wi;) {
            std::int64_t with = best[c - wi] + v[i];
            if (with > best[c]) {   // strictly better: ties keep the subset without item i
                best[c] = with;
                took[i][c] = true;
            }
        }
    }
    std::size_t c = W;
    for (std::size_t i = n; i-- > 0;) {
        if (took[i][c]) {
            res.take[i] = true;
            res.value += v[i];
            res.weight += w[i];
            c -= static_cast<std::size_t>(w[i]);
        }
    }
    return res;
}

KnapsackResult knapsack_brute(const std::vector<std::int64_t> &w, const std::vector<std::int64_t> &v, std::int64_t cap) {
    check_items(w, v, cap);
    const std::size_t n = w.size();
    if (n > 25) throw std::invalid_argument("knapsack brute force is limited to 25 items");
    KnapsackResult best;
    best.take.assign(n, false);
    for (std::uint32_t mask = 0; mask < (1u << n); mask++) {
        std::int64_t tw = 0, tv = 0;
        for (std::size_t i = 0; i < n; i++)
            if (mask >> i & 1u) { tw += w[i]; tv += v[i]; }
        if (tw <= cap && tv > best.value) {
            best.value = tv;
            best.weight = tw;
            for (std::size_t i = 0; i < n; i++) best.take[i] = (mask >> i & 1u) != 0;
        }
    }
    return best;
}

// ======================================================================== matrix chain
static std::string paren(const std::vector<std::vector<int>> &split, int i, int j) {
    if (i == j) return "A" + std::to_string(i + 1);
    int k = split[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    return "(" + paren(split, i, k) + paren(split, k + 1, j) + ")";
}

MatrixChainResult matrix_chain_order(const std::vector<std::uint64_t> &dims) {
    if (dims.size() < 2) throw std::invalid_argument("matrix chain needs at least one matrix (two dimensions)");
    for (std::uint64_t d : dims)
        if (d == 0) throw std::invalid_argument("matrix dimensions must be positive");
    const std::size_t n = dims.size() - 1;
    const std::uint64_t INF = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::vector<std::uint64_t>> m(n, std::vector<std::uint64_t>(n, 0));
    MatrixChainResult res;
    res.split.assign(n, std::vector<int>(n, 0));
    for (std::size_t len = 2; len <= n; len++)
        for (std::size_t i = 0; i + len - 1 < n; i++) {
            std::size_t j = i + len - 1;
            m[i][j] = INF;
            for (std::size_t k = i; k < j; k++) {
                // products of three dimensions can be large: saturate instead of wrapping around
                long double cost = static_cast<long double>(m[i][k]) + static_cast<long double>(m[k + 1][j]) +
                                   static_cast<long double>(dims[i]) * dims[k + 1] * dims[j + 1];
                std::uint64_t c = cost >= static_cast<long double>(INF) ? INF : static_cast<std::uint64_t>(cost);
                if (c < m[i][j]) { m[i][j] = c; res.split[i][j] = static_cast<int>(k); }
            }
        }
    res.cost = m[0][n - 1];
    res.order = paren(res.split, 0, static_cast<int>(n) - 1);
    return res;
}

std::uint64_t matrix_chain_left_to_right(const std::vector<std::uint64_t> &dims) {
    if (dims.size() < 2) throw std::invalid_argument("matrix chain needs at least one matrix (two dimensions)");
    std::uint64_t cost = 0;
    for (std::size_t k = 1; k + 1 < dims.size(); k++) cost += dims[0] * dims[k] * dims[k + 1];
    return cost;
}

// ======================================================================== LCS
template <class Seq, class Out>
static std::size_t lcs_core(const Seq &a, const Seq &b, Out &out) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<std::uint32_t>> L(n + 1, std::vector<std::uint32_t>(m + 1, 0));
    for (std::size_t i = 1; i <= n; i++)
        for (std::size_t j = 1; j <= m; j++)
            L[i][j] = a[i - 1] == b[j - 1] ? L[i - 1][j - 1] + 1 : (L[i - 1][j] >= L[i][j - 1] ? L[i - 1][j] : L[i][j - 1]);
    // walk back from the corner to recover one LCS
    std::size_t i = n, j = m;
    out.resize(L[n][m]);
    std::size_t k = L[n][m];
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { out[--k] = a[i - 1]; i--; j--; }
        else if (L[i - 1][j] >= L[i][j - 1]) i--;
        else j--;
    }
    return L[n][m];
}

LcsResult<int> lcs(const std::vector<int> &a, const std::vector<int> &b) {
    LcsResult<int> r;
    r.length = lcs_core(a, b, r.sequence);
    return r;
}

std::string lcs(const std::string &a, const std::string &b) {
    std::string out;
    lcs_core(a, b, out);
    return out;
}

// ======================================================================== optimal BST
static void check_bst(const std::vector<double> &p, const std::vector<double> &q) {
    if (p.empty() || q.size() != p.size()) throw std::invalid_argument("optimal BST: p and q must both have n+1 entries");
    for (double x : p) if (!(x >= 0)) throw std::invalid_argument("optimal BST: probabilities must be >= 0");
    for (double x : q) if (!(x >= 0)) throw std::invalid_argument("optimal BST: probabilities must be >= 0");
}

double optimal_bst(const std::vector<double> &p, const std::vector<double> &q, std::vector<std::vector<int>> *root) {
    check_bst(p, q);
    const std::size_t n = p.size() - 1;
    // e[i][j], w[i][j] for 1 <= i <= n+1, i-1 <= j <= n  (CLRS 15.5)
    std::vector<std::vector<double>> e(n + 2, std::vector<double>(n + 1, 0)), w(n + 2, std::vector<double>(n + 1, 0));
    std::vector<std::vector<int>> rt(n + 2, std::vector<int>(n + 1, 0));
    for (std::size_t i = 1; i <= n + 1; i++) { e[i][i - 1] = q[i - 1]; w[i][i - 1] = q[i - 1]; }
    for (std::size_t len = 1; len <= n; len++)
        for (std::size_t i = 1; i + len - 1 <= n; i++) {
            std::size_t j = i + len - 1;
            e[i][j] = std::numeric_limits<double>::infinity();
            w[i][j] = w[i][j - 1] + p[j] + q[j];
            for (std::size_t r = i; r <= j; r++) {
                double t = e[i][r - 1] + e[r + 1][j] + w[i][j];
                if (t < e[i][j]) { e[i][j] = t; rt[i][j] = static_cast<int>(r); }
            }
        }
    if (root) *root = rt;
    return n == 0 ? q[0] : e[1][n];
}

static double balanced_cost(const std::vector<double> &p, const std::vector<double> &q, std::size_t i, std::size_t j, int depth) {
    // keys i..j at this depth (1-based); gaps are leaves one level below
    if (i > j) return q[j] * (depth);   // dummy key d_j reached at this depth
    std::size_t r = i + (j - i) / 2;
    return p[r] * (depth) + balanced_cost(p, q, i, r - 1, depth + 1) + balanced_cost(p, q, r + 1, j, depth + 1);
}

double balanced_bst_cost(const std::vector<double> &p, const std::vector<double> &q) {
    check_bst(p, q);
    const std::size_t n = p.size() - 1;
    if (n == 0) return q[0];
    return balanced_cost(p, q, 1, n, 1);
}

// ======================================================================== TSP
static void check_dist(const std::vector<std::vector<double>> &d, std::size_t max_n) {
    if (d.empty()) throw std::invalid_argument("TSP needs at least one vertex");
    if (d.size() > max_n) throw std::invalid_argument("TSP instance too large for this method");
    for (const auto &row : d)
        if (row.size() != d.size()) throw std::invalid_argument("TSP distance matrix must be square");
}

TourResult tsp_held_karp(const std::vector<std::vector<double>> &d) {
    check_dist(d, 20);
    const std::size_t n = d.size();
    const double INF = std::numeric_limits<double>::infinity();
    TourResult res;
    if (n == 1) { res.tour = {0, 0}; return res; }
    const std::size_t FULL = std::size_t(1) << (n - 1);   // subsets of vertices 1..n-1
    // dp[S][j]: shortest path from 0 visiting exactly S (bit j-1 for vertex j) and ending at j
    std::vector<double> dp(FULL * n, INF);
    std::vector<int> par(FULL * n, -1);
    for (std::size_t j = 1; j < n; j++) dp[(std::size_t(1) << (j - 1)) * n + j] = d[0][j];
    for (std::size_t S = 1; S < FULL; S++)
        for (std::size_t j = 1; j < n; j++) {
            if (!(S >> (j - 1) & 1u)) continue;
            double cur = dp[S * n + j];
            if (cur == INF) continue;
            for (std::size_t k = 1; k < n; k++) {
                if (S >> (k - 1) & 1u) continue;
                std::size_t T = S | (std::size_t(1) << (k - 1));
                double c = cur + d[j][k];
                if (c < dp[T * n + k]) { dp[T * n + k] = c; par[T * n + k] = static_cast<int>(j); }
            }
        }
    double best = INF;
    int last = -1;
    for (std::size_t j = 1; j < n; j++) {
        double c = dp[(FULL - 1) * n + j] + d[j][0];
        if (c < best) { best = c; last = static_cast<int>(j); }
    }
    res.length = best;
    if (last < 0) return res;
    std::vector<int> rev;
    std::size_t S = FULL - 1;
    int j = last;
    while (j > 0) {
        rev.push_back(j);
        int p = par[S * n + static_cast<std::size_t>(j)];
        S &= ~(std::size_t(1) << (j - 1));
        j = p < 0 ? 0 : p;
    }
    res.tour.push_back(0);
    for (std::size_t k = rev.size(); k-- > 0;) res.tour.push_back(rev[k]);
    res.tour.push_back(0);
    return res;
}

static void permute(std::vector<int> &perm, std::size_t k, const std::vector<std::vector<double>> &d, TourResult &best) {
    if (k == perm.size()) {
        double len = 0;
        int prev = 0;
        for (int v : perm) { len += d[static_cast<std::size_t>(prev)][static_cast<std::size_t>(v)]; prev = v; }
        len += d[static_cast<std::size_t>(prev)][0];
        if (len < best.length) {
            best.length = len;
            best.tour.assign(1, 0);
            best.tour.insert(best.tour.end(), perm.begin(), perm.end());
            best.tour.push_back(0);
        }
        return;
    }
    for (std::size_t i = k; i < perm.size(); i++) {
        std::swap(perm[k], perm[i]);
        permute(perm, k + 1, d, best);
        std::swap(perm[k], perm[i]);
    }
}

TourResult tsp_brute(const std::vector<std::vector<double>> &d) {
    check_dist(d, 11);
    TourResult best;
    best.length = std::numeric_limits<double>::infinity();
    if (d.size() == 1) { best.length = 0; best.tour = {0, 0}; return best; }
    std::vector<int> perm;
    for (std::size_t i = 1; i < d.size(); i++) perm.push_back(static_cast<int>(i));
    permute(perm, 0, d, best);
    if (best.length == std::numeric_limits<double>::infinity()) best.tour.clear();
    return best;
}

} // namespace ev
