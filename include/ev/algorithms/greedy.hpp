// greedy.hpp - activity selection, interval partitioning and Huffman coding.
// (Dijkstra, Prim and Kruskal are greedy too; they live in ev/graph/.)
//
// Charging sessions are half-open intervals [start, end): a session ending at 10:00 and another
// starting at 10:00 can use the same charging bay.
//
// The two interval problems answer DIFFERENT questions:
//  * activity_selection: ONE bay - which sessions can it serve? (maximum number of pairwise
//    compatible sessions; earliest-finish-first is optimal by an exchange argument). Θ(n log n).
//  * interval_partitioning: ALL sessions must be served - how many bays are needed and which bay
//    takes each session? (earliest-start-first with a min-heap of bay finish times; the result
//    equals the maximum number of sessions overlapping at any instant, which is optimal). Θ(n log n).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ev {

struct Interval {
    int id = 0;
    double start = 0, end = 0;   // half-open [start, end); end > start is required
};

// Throws std::invalid_argument if any interval has end <= start or a non-finite bound.
void validate_intervals(const std::vector<Interval> &iv);

// Indices (into iv) of a maximum set of mutually compatible intervals, in order of finish time.
std::vector<std::size_t> activity_selection(const std::vector<Interval> &iv);
// Exhaustive oracle for small n (n <= 20): size of a maximum compatible set.
std::size_t activity_selection_brute(const std::vector<Interval> &iv);

struct PartitionResult {
    int resources = 0;              // bays needed
    std::vector<int> assignment;    // assignment[i] = bay (0-based) of interval i
};
PartitionResult interval_partitioning(const std::vector<Interval> &iv);
// Maximum number of intervals containing a common instant (the depth; a lower bound on bays).
int max_overlap(const std::vector<Interval> &iv);

// ------------------------------------------------------------------ Huffman coding
// Builds an optimal prefix code for the byte frequencies of `text`.
//  * empty input -> std::invalid_argument (there is nothing to code)
//  * one distinct symbol -> that symbol gets the code "0" (one bit per symbol)
// Ties in the priority queue are broken by (weight, smallest symbol in subtree) so the codes are
// the same on every platform.
class HuffmanCode {
public:
    explicit HuffmanCode(const std::string &text);
    explicit HuffmanCode(const std::map<unsigned char, std::uint64_t> &frequencies);

    std::size_t symbols() const { return codes_.size(); }
    // code of a symbol as a string of '0'/'1'; throws std::invalid_argument if the symbol is unknown
    const std::string &code(unsigned char symbol) const;
    const std::map<unsigned char, std::string> &codes() const { return codes_; }

    // Encodes to a bit vector; throws std::invalid_argument on a symbol that is not in the code.
    std::vector<bool> encode(const std::string &text) const;
    // Decodes; throws std::invalid_argument if the bits do not end on a symbol boundary.
    std::string decode(const std::vector<bool> &bits) const;

    // weighted average code length in bits per symbol for the building frequencies
    double average_bits() const;
    std::uint64_t total_bits() const;   // sum over symbols of frequency * code length

private:
    struct Node {
        std::uint64_t weight = 0;
        int symbol = -1;                     // leaf symbol, -1 for internal nodes
        int min_symbol = 0;                  // tie-break key
        int left = -1, right = -1;           // children (indices into nodes_)
    };
    void build(const std::map<unsigned char, std::uint64_t> &freq);
    void assign(int node, std::string &prefix);

    std::vector<Node> nodes_;
    int root_ = -1;
    std::map<unsigned char, std::string> codes_;
    std::map<unsigned char, std::uint64_t> freq_;
};

} // namespace ev
