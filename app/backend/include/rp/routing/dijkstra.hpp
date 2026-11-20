#pragma once

// shortest_path() routes the single legs (with ALT if landmarks are set),
// within() computes the isochrone. A search changes the internal arrays, so
// every worker needs its own instance.
//
// Distance and predecessor arrays are allocated once for the whole graph and
// only the touched entries are reset afterwards, so short searches stay cheap
// on large graphs.

#include "rp/routing/cost_model.hpp"
#include "rp/routing/landmarks.hpp"
#include "rp/graph/graph.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rp {

// Path cost, physical length and edges in travel order.
struct Path {
    double cost = 0;
    double length_m = 0;
    std::vector<std::uint32_t> edges;
};

class Dijkstra {
public:
    explicit Dijkstra(const Graph& g);

    // Cheapest path under the given costs, nullopt if target is unreachable.
    std::optional<Path> shortest_path(std::uint32_t source, std::uint32_t target, const CostModel& cost);

    // Isochrone: all nodes within max_m metres of real distance (the sliders
    // play no role), as (node, distance) in the order they were settled.
    std::vector<std::pair<std::uint32_t, double>> within(std::uint32_t source, double max_m);

    // Switches shortest_path() to A* with landmark bounds (nullptr = plain
    // Dijkstra). The landmarks have to outlive this object.
    void set_landmarks(const Landmarks* lm) {
        landmarks_ = lm;
    }

    // Nodes settled by the last search (for benchmarking).
    std::uint32_t settled() const {
        return settled_;
    }

private:
    void reset();

    const Graph& g_;
    std::vector<double> dist_;
    std::vector<std::uint32_t> pred_;
    std::vector<std::uint32_t> touched_;
    std::uint32_t settled_ = 0;
    const Landmarks* landmarks_ = nullptr;
};

} // namespace rp
