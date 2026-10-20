// graph.cpp - adjacency-list graph.
#include "ev/graph/graph.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace ev {

Graph::Graph(int vertices, bool directed) : directed_(directed) {
    if (vertices < 0) throw std::invalid_argument("a graph cannot have a negative number of vertices");
    adj_.resize(static_cast<std::size_t>(vertices));
}

void Graph::check_vertex(int v) const {
    if (v < 0 || v >= vertex_count())
        throw std::out_of_range("vertex " + std::to_string(v) + " is not in the graph (it has " +
                                std::to_string(vertex_count()) + " vertices)");
}

int Graph::add_edge(int u, int v, double weight) {
    check_vertex(u);
    check_vertex(v);
    if (!std::isfinite(weight)) throw std::invalid_argument("edge weight must be a finite number");
    const int id = edge_count();
    edges_.push_back({u, v, weight});
    adj_[static_cast<std::size_t>(u)].push_back({v, weight, id});
    if (!directed_ && u != v) adj_[static_cast<std::size_t>(v)].push_back({u, weight, id});
    if (weight < 0) negative_ = true;
    return id;
}

const std::vector<Arc> &Graph::out(int u) const {
    check_vertex(u);
    return adj_[static_cast<std::size_t>(u)];
}

const Edge &Graph::edge(int id) const {
    if (id < 0 || id >= edge_count()) throw std::out_of_range("edge id out of range");
    return edges_[static_cast<std::size_t>(id)];
}

} // namespace ev
