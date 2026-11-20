#include "rp/routing/spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace rp {

namespace {
// Grid cells in degrees: ~555 m north-south and 480–565 m east-west in Germany.
constexpr double kCellLat = 0.005;
constexpr double kCellLon = 0.0075;
} // namespace

int SpatialIndex::cell_x(double lon) const {
    return std::clamp(static_cast<int>((lon - min_lon_) / kCellLon), 0, nx_ - 1);
}

int SpatialIndex::cell_y(double lat) const {
    return std::clamp(static_cast<int>((lat - min_lat_) / kCellLat), 0, ny_ - 1);
}

SpatialIndex::SpatialIndex(const Graph& g)
    : g_(g) {
    if (g.num_nodes() == 0) {
        return;
    }

    double max_lat = g.node_pos[0].lat, max_lon = g.node_pos[0].lon;
    min_lat_ = max_lat;
    min_lon_ = max_lon;
    for (const auto& p : g.node_pos) {
        min_lat_ = std::min(min_lat_, p.lat);
        max_lat = std::max(max_lat, p.lat);
        min_lon_ = std::min(min_lon_, p.lon);
        max_lon = std::max(max_lon, p.lon);
    }
    nx_ = static_cast<int>((max_lon - min_lon_) / kCellLon) + 1;
    ny_ = static_cast<int>((max_lat - min_lat_) / kCellLat) + 1;
    // The shorter side counts; cells get narrower towards the pole.
    cell_m_ = std::min(haversine_m({min_lat_, min_lon_}, {min_lat_ + kCellLat, min_lon_}),
                       haversine_m({max_lat, min_lon_}, {max_lat, min_lon_ + kCellLon}));

    const auto cell_of = [&](LatLon p) {
        return static_cast<std::size_t>(cell_y(p.lat)) * nx_ + cell_x(p.lon);
    };
    cell_first_.assign(static_cast<std::size_t>(nx_) * ny_ + 1, 0);
    for (const auto& p : g.node_pos) {
        ++cell_first_[cell_of(p) + 1];
    }
    std::partial_sum(cell_first_.begin(), cell_first_.end(), cell_first_.begin());
    cell_nodes_.resize(g.num_nodes());
    std::vector<std::uint32_t> fill(cell_first_.begin(), cell_first_.end() - 1);
    for (std::uint32_t v = 0; v < g.num_nodes(); ++v) {
        cell_nodes_[fill[cell_of(g.node_pos[v])]++] = v;
    }
}

std::uint32_t SpatialIndex::nearest(LatLon p) const {
    if (cell_nodes_.empty()) {
        return kNoEdge;
    }
    const int cx = cell_x(p.lon), cy = cell_y(p.lat);
    std::uint32_t best = kNoEdge;
    double best_d = std::numeric_limits<double>::infinity();
    const double cos_lat = std::cos(deg_to_rad(p.lat));

    // Ring r = cells exactly r steps from the start cell. Nodes in ring r are at
    // least (r-1) cell widths away, so once that exceeds the best distance no
    // outer ring can have a closer node.
    const int max_r = std::max(nx_, ny_);
    for (int r = 0; r <= max_r; ++r) {
        if (r >= 1 && (r - 1) * cell_m_ > best_d) {
            break;
        }
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != r) {
                    continue;
                }
                if (x < 0 || y < 0 || x >= nx_ || y >= ny_) {
                    continue;
                }
                const std::size_t c = static_cast<std::size_t>(y) * nx_ + x;
                for (std::uint32_t k = cell_first_[c]; k < cell_first_[c + 1]; ++k) {
                    const double d = flat_m(p, g_.node_pos[cell_nodes_[k]], cos_lat);
                    if (d < best_d) {
                        best_d = d;
                        best = cell_nodes_[k];
                    }
                }
            }
        }
    }
    return best;
}

} // namespace rp
