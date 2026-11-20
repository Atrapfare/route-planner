#pragma once

// Raster over the region with ~55 m cells, marked if they lie in a nature
// area. Checking an edge is then a single lookup instead of a polygon test
// against every area.

#include "rp/graph/geo.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace rp {

class NatureMap {
public:
    NatureMap(LatLon min, LatLon max);

    // Marks all cells inside the area given by its boundary segments.
    // Holes work through the even-odd rule.
    void fill(const std::vector<std::pair<LatLon, LatLon>>& border);

    bool contains(LatLon p) const;

private:
    double col_f(double lon) const;
    double row_f(double lat) const;

    LatLon min_;
    int nx_, ny_;
    std::vector<std::uint8_t> cells_; // row-major, 1 = nature
};

} // namespace rp
