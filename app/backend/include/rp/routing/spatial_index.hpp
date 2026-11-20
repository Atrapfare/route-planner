#pragma once

// Nearest graph node to a coordinate. The region is split into ~500 m cells;
// the search starts in the point's cell and goes outwards ring by ring until
// no closer node is possible.

#include "rp/graph/graph.hpp"

#include <cstdint>
#include <vector>

namespace rp {

class SpatialIndex {
public:
    explicit SpatialIndex(const Graph& g);

    // Nearest node to p by straight-line distance; kNoEdge if graph is empty.
    std::uint32_t nearest(LatLon p) const;

private:
    int cell_x(double lon) const;
    int cell_y(double lat) const;

    const Graph& g_;
    double min_lat_ = 0, min_lon_ = 0;
    int nx_ = 0, ny_ = 0;
    double cell_m_ = 0;                // shorter side of a cell in metres

    // Nodes of cell c: cell_nodes_[cell_first_[c]..cell_first_[c+1]-1]
    std::vector<std::uint32_t> cell_first_;
    std::vector<std::uint32_t> cell_nodes_;
};

} // namespace rp
