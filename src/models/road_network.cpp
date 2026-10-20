// road_network.cpp - validated road network.
#include "ev/models/road_network.hpp"

#include <cmath>
#include <stdexcept>

#include "ev/graph/spanning_tree.hpp"

namespace ev {

bool valid_geo(const GeoPoint &p) {
    return std::isfinite(p.lat) && std::isfinite(p.lon) && p.lat >= -90.0 && p.lat <= 90.0 && p.lon >= -180.0 &&
           p.lon <= 180.0;
}

double haversine_km(const GeoPoint &a, const GeoPoint &b) {
    const double kPi = 3.14159265358979323846, R = 6371.0;
    double p1 = a.lat * kPi / 180, p2 = b.lat * kPi / 180;
    double dp = p2 - p1, dl = (b.lon - a.lon) * kPi / 180;
    double h = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * R * std::asin(std::sqrt(h < 1 ? h : 1));
}

void RoadNetwork::add_node(const RoadNode &n) {
    if (node_pos_.count(n.id)) throw std::invalid_argument("duplicate node id " + std::to_string(n.id));
    if (n.location && !valid_geo(*n.location))
        throw std::invalid_argument("node " + std::to_string(n.id) +
                                    " has invalid coordinates (latitude must be in [-90, 90], longitude in [-180, 180])");
    if (!std::isfinite(n.elevation_m)) throw std::invalid_argument("node " + std::to_string(n.id) + " has a non-finite elevation");
    node_pos_[n.id] = static_cast<int>(nodes_.size());
    nodes_.push_back(n);
}

void RoadNetwork::add_edge(const RoadEdge &e) {
    if (edge_ids_.count(e.id)) throw std::invalid_argument("duplicate road id " + std::to_string(e.id));
    int a = index_of(e.from), b = index_of(e.to);
    if (a < 0) throw std::invalid_argument("road " + std::to_string(e.id) + " refers to unknown node " + std::to_string(e.from));
    if (b < 0) throw std::invalid_argument("road " + std::to_string(e.id) + " refers to unknown node " + std::to_string(e.to));
    if (a == b) throw std::invalid_argument("road " + std::to_string(e.id) + " is a self-loop (from == to)");
    if (!std::isfinite(e.distance_km) || e.distance_km <= 0)
        throw std::invalid_argument("road " + std::to_string(e.id) + " must have a positive, finite distance");
    if (e.distance_km > 100000.0)   // longer than twice round the Earth: certainly a data error
        throw std::invalid_argument("road " + std::to_string(e.id) + " is unrealistically long (over 100,000 km)");
    if (e.travel_time_min && (!std::isfinite(*e.travel_time_min) || *e.travel_time_min <= 0 || *e.travel_time_min > 1e6))
        throw std::invalid_argument("road " + std::to_string(e.id) + " must have a positive, plausible travel time");
    if (e.energy_kwh && (!std::isfinite(*e.energy_kwh) || std::fabs(*e.energy_kwh) > 1e6))
        throw std::invalid_argument("road " + std::to_string(e.id) + " has an invalid energy estimate");
    edge_ids_[e.id] = static_cast<int>(edges_.size());
    edges_.push_back(e);
    edge_from_.push_back(a);
    edge_to_.push_back(b);
}

int RoadNetwork::index_of(int id) const {
    auto it = node_pos_.find(id);
    return it == node_pos_.end() ? -1 : it->second;
}

Graph RoadNetwork::distance_graph() const {
    Graph g(node_count(), false);
    for (int k = 0; k < edge_count(); k++) g.add_edge(edge_from_[static_cast<std::size_t>(k)], edge_to_[static_cast<std::size_t>(k)], edges_[static_cast<std::size_t>(k)].distance_km);
    return g;
}

int RoadNetwork::components(std::vector<int> *label) const {
    DisjointSet ds(node_count());
    for (int k = 0; k < edge_count(); k++) ds.unite(edge_from_[static_cast<std::size_t>(k)], edge_to_[static_cast<std::size_t>(k)]);
    if (label) {
        label->assign(static_cast<std::size_t>(node_count()), -1);
        std::unordered_map<int, int> root_label;
        for (int v = 0; v < node_count(); v++) {
            int r = ds.find(v);
            auto it = root_label.find(r);
            if (it == root_label.end()) it = root_label.emplace(r, static_cast<int>(root_label.size())).first;
            (*label)[static_cast<std::size_t>(v)] = it->second;
        }
    }
    return ds.sets();
}

bool RoadNetwork::all_located() const {
    for (const RoadNode &n : nodes_)
        if (!n.location) return false;
    return !nodes_.empty();
}

std::vector<std::pair<double, double>> RoadNetwork::planar_km() const {
    std::vector<std::pair<double, double>> xy(nodes_.size(), {0.0, 0.0});
    double lat0 = 0, lon0 = 0;
    int cnt = 0;
    for (const RoadNode &n : nodes_)
        if (n.location) { lat0 += n.location->lat; lon0 += n.location->lon; cnt++; }
    if (!cnt) return xy;
    lat0 /= cnt;
    lon0 /= cnt;
    const double kPi = 3.14159265358979323846;
    const double ky = 110.574, kx = 111.320 * std::cos(lat0 * kPi / 180);
    for (std::size_t i = 0; i < nodes_.size(); i++)
        if (nodes_[i].location) xy[i] = {(nodes_[i].location->lon - lon0) * kx, (nodes_[i].location->lat - lat0) * ky};
    return xy;
}

} // namespace ev
