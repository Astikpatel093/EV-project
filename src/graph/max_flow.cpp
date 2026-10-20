// max_flow.cpp - Ford-Fulkerson (DFS), Edmonds-Karp (BFS), min cut, bipartite matching.
#include "ev/graph/max_flow.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace ev {

FlowNetwork::FlowNetwork(int vertices) {
    if (vertices < 0) throw std::invalid_argument("negative vertex count");
    adj_.resize(static_cast<std::size_t>(vertices));
}

void FlowNetwork::check(int v) const {
    if (v < 0 || v >= vertex_count()) throw std::out_of_range("flow network vertex " + std::to_string(v) + " out of range");
}

int FlowNetwork::add_edge(int u, int v, std::int64_t cap) {
    check(u);
    check(v);
    if (cap < 0) throw std::invalid_argument("capacity must not be negative");
    auto &au = adj_[static_cast<std::size_t>(u)];
    auto &av = adj_[static_cast<std::size_t>(v)];
    au.push_back({v, cap, 0, static_cast<int>(av.size()) + (u == v ? 1 : 0)});
    av.push_back({u, 0, 0, static_cast<int>(au.size()) - 1});   // residual (reverse) edge
    where_.emplace_back(u, static_cast<int>(au.size()) - 1);
    return static_cast<int>(where_.size()) - 1;
}

std::int64_t FlowNetwork::flow(int id) const {
    if (id < 0 || id >= static_cast<int>(where_.size())) throw std::out_of_range("edge id out of range");
    const auto &w = where_[static_cast<std::size_t>(id)];
    return adj_[static_cast<std::size_t>(w.first)][static_cast<std::size_t>(w.second)].flow;
}

void FlowNetwork::reset() {
    for (auto &list : adj_)
        for (auto &e : list) e.flow = 0;
}

// one augmenting path found by iterative depth-first search; returns the amount pushed
std::int64_t FlowNetwork::augment_dfs(int s, int t) {
    const std::size_t n = adj_.size();
    std::vector<int> prev_v(n, -1), prev_e(n, -1);
    std::vector<bool> seen(n, false);
    std::vector<int> stack{s};
    seen[static_cast<std::size_t>(s)] = true;
    while (!stack.empty() && !seen[static_cast<std::size_t>(t)]) {
        int u = stack.back();
        stack.pop_back();
        const auto &list = adj_[static_cast<std::size_t>(u)];
        for (std::size_t i = 0; i < list.size(); i++) {
            const E &e = list[i];
            if (e.cap - e.flow > 0 && !seen[static_cast<std::size_t>(e.to)]) {
                seen[static_cast<std::size_t>(e.to)] = true;
                prev_v[static_cast<std::size_t>(e.to)] = u;
                prev_e[static_cast<std::size_t>(e.to)] = static_cast<int>(i);
                stack.push_back(e.to);
            }
        }
    }
    if (!seen[static_cast<std::size_t>(t)]) return 0;
    std::int64_t push = std::numeric_limits<std::int64_t>::max();
    for (int v = t; v != s; v = prev_v[static_cast<std::size_t>(v)]) {
        const E &e = adj_[static_cast<std::size_t>(prev_v[static_cast<std::size_t>(v)])][static_cast<std::size_t>(prev_e[static_cast<std::size_t>(v)])];
        if (e.cap - e.flow < push) push = e.cap - e.flow;
    }
    for (int v = t; v != s; v = prev_v[static_cast<std::size_t>(v)]) {
        E &e = adj_[static_cast<std::size_t>(prev_v[static_cast<std::size_t>(v)])][static_cast<std::size_t>(prev_e[static_cast<std::size_t>(v)])];
        e.flow += push;
        adj_[static_cast<std::size_t>(e.to)][static_cast<std::size_t>(e.rev)].flow -= push;
    }
    return push;
}

// one shortest augmenting path (fewest edges) found by breadth-first search
std::int64_t FlowNetwork::augment_bfs(int s, int t) {
    const std::size_t n = adj_.size();
    std::vector<int> prev_v(n, -1), prev_e(n, -1), queue;
    std::vector<bool> seen(n, false);
    queue.push_back(s);
    seen[static_cast<std::size_t>(s)] = true;
    for (std::size_t head = 0; head < queue.size() && !seen[static_cast<std::size_t>(t)]; head++) {
        int u = queue[head];
        const auto &list = adj_[static_cast<std::size_t>(u)];
        for (std::size_t i = 0; i < list.size(); i++) {
            const E &e = list[i];
            if (e.cap - e.flow > 0 && !seen[static_cast<std::size_t>(e.to)]) {
                seen[static_cast<std::size_t>(e.to)] = true;
                prev_v[static_cast<std::size_t>(e.to)] = u;
                prev_e[static_cast<std::size_t>(e.to)] = static_cast<int>(i);
                queue.push_back(e.to);
            }
        }
    }
    if (!seen[static_cast<std::size_t>(t)]) return 0;
    std::int64_t push = std::numeric_limits<std::int64_t>::max();
    for (int v = t; v != s; v = prev_v[static_cast<std::size_t>(v)]) {
        const E &e = adj_[static_cast<std::size_t>(prev_v[static_cast<std::size_t>(v)])][static_cast<std::size_t>(prev_e[static_cast<std::size_t>(v)])];
        if (e.cap - e.flow < push) push = e.cap - e.flow;
    }
    for (int v = t; v != s; v = prev_v[static_cast<std::size_t>(v)]) {
        E &e = adj_[static_cast<std::size_t>(prev_v[static_cast<std::size_t>(v)])][static_cast<std::size_t>(prev_e[static_cast<std::size_t>(v)])];
        e.flow += push;
        adj_[static_cast<std::size_t>(e.to)][static_cast<std::size_t>(e.rev)].flow -= push;
    }
    return push;
}

FlowResult FlowNetwork::max_flow(int s, int t, FlowMethod method) {
    check(s);
    check(t);
    if (s == t) throw std::invalid_argument("source and sink must differ");
    FlowResult r;
    for (;;) {
        std::int64_t pushed = method == FlowMethod::FordFulkersonDfs ? augment_dfs(s, t) : augment_bfs(s, t);
        if (pushed == 0) break;
        r.value += pushed;
        r.augmentations++;
    }
    return r;
}

std::vector<bool> FlowNetwork::min_cut_source_side(int s) const {
    check(s);
    std::vector<bool> side(adj_.size(), false);
    std::vector<int> stack{s};
    side[static_cast<std::size_t>(s)] = true;
    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        for (const E &e : adj_[static_cast<std::size_t>(u)])
            if (e.cap - e.flow > 0 && !side[static_cast<std::size_t>(e.to)]) {
                side[static_cast<std::size_t>(e.to)] = true;
                stack.push_back(e.to);
            }
    }
    return side;
}

std::int64_t FlowNetwork::cut_capacity(const std::vector<bool> &side) const {
    std::int64_t c = 0;
    for (const auto &w : where_) {
        const E &e = adj_[static_cast<std::size_t>(w.first)][static_cast<std::size_t>(w.second)];
        if (side[static_cast<std::size_t>(w.first)] && !side[static_cast<std::size_t>(e.to)]) c += e.cap;
    }
    return c;
}

int bipartite_matching(int left, int right, const std::vector<std::vector<int>> &adj, std::vector<int> *match_left) {
    if (left < 0 || right < 0 || static_cast<int>(adj.size()) != left)
        throw std::invalid_argument("bipartite matching: adjacency must have one list per left vertex");
    for (const auto &list : adj)
        for (int r : list)
            if (r < 0 || r >= right) throw std::out_of_range("bipartite matching: right vertex out of range");
    std::vector<int> ml(static_cast<std::size_t>(left), -1), mr(static_cast<std::size_t>(right), -1);
    std::vector<int> seen(static_cast<std::size_t>(right), -1);
    int size = 0;
    // Kuhn's algorithm with an explicit stack (no deep recursion on long augmenting paths)
    for (int root = 0; root < left; root++) {
        struct Frame { int u; std::size_t next; };
        std::vector<Frame> st{{root, 0}};
        std::vector<int> via;   // right vertex chosen at each frame
        bool found = false;
        while (!st.empty() && !found) {
            Frame &f = st.back();
            const auto &list = adj[static_cast<std::size_t>(f.u)];
            if (f.next == list.size()) {
                st.pop_back();
                if (!via.empty()) via.pop_back();
                continue;
            }
            int r = list[f.next++];
            if (seen[static_cast<std::size_t>(r)] == root) continue;
            seen[static_cast<std::size_t>(r)] = root;
            via.push_back(r);
            if (mr[static_cast<std::size_t>(r)] < 0) { found = true; break; }
            st.push_back({mr[static_cast<std::size_t>(r)], 0});
        }
        if (!found) continue;
        // flip the alternating path: frame k's vertex takes via[k]
        for (std::size_t k = 0; k < st.size(); k++) {
            int u = st[k].u, r = via[k];
            ml[static_cast<std::size_t>(u)] = r;
            mr[static_cast<std::size_t>(r)] = u;
        }
        size++;
    }
    if (match_left) *match_left = ml;
    return size;
}

} // namespace ev
