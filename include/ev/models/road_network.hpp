// road_network.hpp - road intersections (nodes) and road segments (edges).
//
// Node and edge ids are the external ids from the data files (any integers). Internally the
// network stores nodes in a vector and keeps an id -> position index, so algorithms work on the
// dense positions 0..n-1. Roads are two-way; energy may differ by direction because of elevation.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ev/graph/graph.hpp"

namespace ev {

struct GeoPoint {
    double lat = 0;   // degrees, [-90, 90]
    double lon = 0;   // degrees, [-180, 180]
};
bool valid_geo(const GeoPoint &p);
// Great-circle distance in km (haversine, Earth radius 6371 km).
double haversine_km(const GeoPoint &a, const GeoPoint &b);

struct RoadNode {
    int id = 0;
    std::optional<GeoPoint> location;   // absent when coordinates are not known
    double elevation_m = 0;             // used by the energy model; 0 when unknown
    std::string type;                   // e.g. "junction", "town", "highway" (optional)
    std::string name;                   // optional description
};

struct RoadEdge {
    int id = 0;
    int from = 0, to = 0;                      // node ids
    double distance_km = 0;                    // > 0
    std::optional<double> travel_time_min;     // when absent: distance / scenario speed
    std::optional<double> energy_kwh;          // optional measured/estimated energy for the
                                               // scenario's reference vehicle (scaled per vehicle)
};

class RoadNetwork {
public:
    // Both throw std::invalid_argument with a reason (duplicate id, unknown node, bad value).
    void add_node(const RoadNode &node);
    void add_edge(const RoadEdge &edge);

    int node_count() const { return static_cast<int>(nodes_.size()); }
    int edge_count() const { return static_cast<int>(edges_.size()); }
    const std::vector<RoadNode> &nodes() const { return nodes_; }
    const std::vector<RoadEdge> &edges() const { return edges_; }
    const RoadNode &node(int index) const { return nodes_.at(static_cast<std::size_t>(index)); }

    // Position of a node id, or -1 if the id is unknown.
    int index_of(int node_id) const;
    bool has_node(int node_id) const { return index_of(node_id) >= 0; }
    // Edge endpoints as positions (from_index, to_index).
    int from_index(int edge_pos) const { return edge_from_[static_cast<std::size_t>(edge_pos)]; }
    int to_index(int edge_pos) const { return edge_to_[static_cast<std::size_t>(edge_pos)]; }

    // Undirected graph on positions with weights = km (edge ids = edge positions).
    Graph distance_graph() const;
    // Number of connected components and a component label per node position.
    int components(std::vector<int> *label = nullptr) const;

    // Local planar coordinates in km (x east, y north) for drawing and zoning: an equirectangular
    // projection around the mean location. Nodes without a location get (0, 0).
    std::vector<std::pair<double, double>> planar_km() const;
    bool all_located() const;

private:
    std::vector<RoadNode> nodes_;
    std::vector<RoadEdge> edges_;
    std::vector<int> edge_from_, edge_to_;
    std::unordered_map<int, int> node_pos_;
    std::unordered_map<int, int> edge_ids_;
};

} // namespace ev
