// greedy.cpp - activity selection, interval partitioning, Huffman coding.
#include "ev/algorithms/greedy.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/algorithms/heap.hpp"
#include "ev/algorithms/sorting.hpp"

namespace ev {

void validate_intervals(const std::vector<Interval> &iv) {
    for (const Interval &x : iv) {
        if (!std::isfinite(x.start) || !std::isfinite(x.end))
            throw std::invalid_argument("interval " + std::to_string(x.id) + " has a non-finite bound");
        if (!(x.end > x.start))
            throw std::invalid_argument("interval " + std::to_string(x.id) + " must end after it starts");
    }
}

std::vector<std::size_t> activity_selection(const std::vector<Interval> &iv) {
    validate_intervals(iv);
    std::vector<std::size_t> order(iv.size());
    for (std::size_t i = 0; i < iv.size(); i++) order[i] = i;
    // earliest finish first; ties by start then index for a deterministic answer
    merge_sort(order, [&](std::size_t a, std::size_t b) {
        if (iv[a].end != iv[b].end) return iv[a].end < iv[b].end;
        if (iv[a].start != iv[b].start) return iv[a].start < iv[b].start;
        return a < b;
    });
    std::vector<std::size_t> chosen;
    bool any = false;
    double free_at = 0;
    for (std::size_t i : order) {
        if (!any || iv[i].start >= free_at) {   // half-open: start == previous end is compatible
            chosen.push_back(i);
            free_at = iv[i].end;
            any = true;
        }
    }
    return chosen;
}

std::size_t activity_selection_brute(const std::vector<Interval> &iv) {
    validate_intervals(iv);
    if (iv.size() > 20) throw std::invalid_argument("brute force is limited to 20 intervals");
    const std::size_t n = iv.size();
    std::size_t best = 0;
    for (std::uint32_t mask = 0; mask < (1u << n); mask++) {
        bool ok = true;
        std::size_t cnt = 0;
        for (std::size_t i = 0; i < n && ok; i++) {
            if (!(mask >> i & 1u)) continue;
            cnt++;
            for (std::size_t j = i + 1; j < n && ok; j++)
                if ((mask >> j & 1u) && iv[i].start < iv[j].end && iv[j].start < iv[i].end) ok = false;
        }
        if (ok && cnt > best) best = cnt;
    }
    return best;
}

PartitionResult interval_partitioning(const std::vector<Interval> &iv) {
    validate_intervals(iv);
    PartitionResult res;
    res.assignment.assign(iv.size(), -1);
    std::vector<std::size_t> order(iv.size());
    for (std::size_t i = 0; i < iv.size(); i++) order[i] = i;
    merge_sort(order, [&](std::size_t a, std::size_t b) {
        if (iv[a].start != iv[b].start) return iv[a].start < iv[b].start;
        return a < b;
    });
    // min-heap of (finish time, bay): the bay that frees up first
    struct Bay { double free_at; int bay; };
    struct ByFree {
        bool operator()(const Bay &x, const Bay &y) const {
            return x.free_at < y.free_at || (x.free_at == y.free_at && x.bay < y.bay);
        }
    };
    BinaryHeap<Bay, ByFree> heap;
    for (std::size_t i : order) {
        if (!heap.empty() && heap.top().free_at <= iv[i].start) {
            Bay b = heap.pop();
            res.assignment[i] = b.bay;
            heap.push({iv[i].end, b.bay});
        } else {
            res.assignment[i] = res.resources;
            heap.push({iv[i].end, res.resources});
            res.resources++;
        }
    }
    return res;
}

int max_overlap(const std::vector<Interval> &iv) {
    validate_intervals(iv);
    // sweep: +1 at start, -1 at end; at equal times process ends first (half-open intervals)
    struct Ev { double t; int delta; };
    std::vector<Ev> ev;
    ev.reserve(iv.size() * 2);
    for (const Interval &x : iv) {
        ev.push_back({x.start, +1});
        ev.push_back({x.end, -1});
    }
    merge_sort(ev, [](const Ev &a, const Ev &b) { return a.t < b.t || (a.t == b.t && a.delta < b.delta); });
    int cur = 0, best = 0;
    for (const Ev &e : ev) {
        cur += e.delta;
        if (cur > best) best = cur;
    }
    return best;
}

// ======================================================================== Huffman
HuffmanCode::HuffmanCode(const std::string &text) {
    if (text.empty()) throw std::invalid_argument("Huffman coding needs at least one symbol");
    std::map<unsigned char, std::uint64_t> f;
    for (char c : text) f[static_cast<unsigned char>(c)]++;
    build(f);
}

HuffmanCode::HuffmanCode(const std::map<unsigned char, std::uint64_t> &frequencies) {
    std::map<unsigned char, std::uint64_t> f;
    for (const auto &kv : frequencies)
        if (kv.second > 0) f.insert(kv);
    if (f.empty()) throw std::invalid_argument("Huffman coding needs at least one symbol with a positive frequency");
    build(f);
}

void HuffmanCode::build(const std::map<unsigned char, std::uint64_t> &freq) {
    freq_ = freq;
    struct Item { std::uint64_t w; int min_symbol; int node; };
    struct Lighter {
        bool operator()(const Item &a, const Item &b) const {
            return a.w < b.w || (a.w == b.w && a.min_symbol < b.min_symbol);
        }
    };
    BinaryHeap<Item, Lighter> heap;
    for (const auto &kv : freq) {
        Node leaf;
        leaf.weight = kv.second;
        leaf.symbol = kv.first;
        leaf.min_symbol = kv.first;
        nodes_.push_back(leaf);
        heap.push({leaf.weight, leaf.min_symbol, static_cast<int>(nodes_.size()) - 1});
    }
    while (heap.size() > 1) {
        Item a = heap.pop(), b = heap.pop();
        Node in;
        in.weight = a.w + b.w;
        in.min_symbol = a.min_symbol < b.min_symbol ? a.min_symbol : b.min_symbol;
        in.left = a.node;
        in.right = b.node;
        nodes_.push_back(in);
        heap.push({in.weight, in.min_symbol, static_cast<int>(nodes_.size()) - 1});
    }
    root_ = heap.top().node;
    std::string prefix;
    if (nodes_[static_cast<std::size_t>(root_)].symbol >= 0) {
        codes_[static_cast<unsigned char>(nodes_[static_cast<std::size_t>(root_)].symbol)] = "0";   // single symbol
    } else {
        assign(root_, prefix);
    }
}

void HuffmanCode::assign(int node, std::string &prefix) {
    const Node &nd = nodes_[static_cast<std::size_t>(node)];
    if (nd.symbol >= 0) { codes_[static_cast<unsigned char>(nd.symbol)] = prefix; return; }
    prefix.push_back('0');
    assign(nd.left, prefix);
    prefix.back() = '1';
    assign(nd.right, prefix);
    prefix.pop_back();
}

const std::string &HuffmanCode::code(unsigned char symbol) const {
    auto it = codes_.find(symbol);
    if (it == codes_.end()) throw std::invalid_argument("symbol is not part of this Huffman code");
    return it->second;
}

std::vector<bool> HuffmanCode::encode(const std::string &text) const {
    std::vector<bool> bits;
    for (char c : text)
        for (char b : code(static_cast<unsigned char>(c))) bits.push_back(b == '1');
    return bits;
}

std::string HuffmanCode::decode(const std::vector<bool> &bits) const {
    std::string out;
    const Node &root = nodes_[static_cast<std::size_t>(root_)];
    if (root.symbol >= 0) {   // single-symbol code: every bit must be 0
        for (bool b : bits) {
            if (b) throw std::invalid_argument("invalid bit for a single-symbol Huffman code");
            out.push_back(static_cast<char>(root.symbol));
        }
        return out;
    }
    int cur = root_;
    for (bool b : bits) {
        const Node &nd = nodes_[static_cast<std::size_t>(cur)];
        cur = b ? nd.right : nd.left;
        const Node &nx = nodes_[static_cast<std::size_t>(cur)];
        if (nx.symbol >= 0) {
            out.push_back(static_cast<char>(nx.symbol));
            cur = root_;
        }
    }
    if (cur != root_) throw std::invalid_argument("Huffman bit stream ends in the middle of a code");
    return out;
}

std::uint64_t HuffmanCode::total_bits() const {
    std::uint64_t t = 0;
    for (const auto &kv : freq_) t += kv.second * codes_.at(kv.first).size();
    return t;
}

double HuffmanCode::average_bits() const {
    std::uint64_t n = 0;
    for (const auto &kv : freq_) n += kv.second;
    return n ? static_cast<double>(total_bits()) / static_cast<double>(n) : 0.0;
}

} // namespace ev
