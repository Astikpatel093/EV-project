// heap.hpp - binary min-heap (priority queue) used by Dijkstra, Prim, Huffman, interval
// partitioning, the trip planners and the day simulation.
//
// Stored as an implicit complete binary tree in a std::vector: children of i are 2i+1 and 2i+2.
// push and pop are O(log n); top is O(1). `Less(a, b)` returns true when a must come out before b.
// The heap is written by hand (rather than std::priority_queue) because it is part of the
// assessed data-structure work, and so its behaviour on ties is documented: elements that compare
// equal leave in an unspecified order, so callers that need determinism add a tie-break field.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ev {

template <class T, class Less>
class BinaryHeap {
public:
    explicit BinaryHeap(Less less = Less()) : less_(less) {}

    bool empty() const { return a_.empty(); }
    std::size_t size() const { return a_.size(); }
    void reserve(std::size_t n) { a_.reserve(n); }
    void clear() { a_.clear(); }

    const T &top() const {
        if (a_.empty()) throw std::out_of_range("BinaryHeap::top on an empty heap");
        return a_.front();
    }

    void push(T value) {
        a_.push_back(std::move(value));
        sift_up(a_.size() - 1);
    }

    T pop() {
        if (a_.empty()) throw std::out_of_range("BinaryHeap::pop on an empty heap");
        T out = std::move(a_.front());
        if (a_.size() > 1) {
            a_.front() = std::move(a_.back());
            a_.pop_back();
            sift_down(0);
        } else {
            a_.pop_back();
        }
        return out;
    }

private:
    void sift_up(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (!less_(a_[i], a_[p])) break;
            std::swap(a_[i], a_[p]);
            i = p;
        }
    }
    void sift_down(std::size_t i) {
        const std::size_t n = a_.size();
        for (;;) {
            std::size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < n && less_(a_[l], a_[m])) m = l;
            if (r < n && less_(a_[r], a_[m])) m = r;
            if (m == i) return;
            std::swap(a_[i], a_[m]);
            i = m;
        }
    }

    std::vector<T> a_;
    Less less_;
};

} // namespace ev
