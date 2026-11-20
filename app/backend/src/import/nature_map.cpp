#include "rp/import/nature_map.hpp"

#include <algorithm>
#include <cmath>

namespace rp {

namespace {
// Grid cell size in degrees: ~55 m north-south, 48–57 m east-west in Germany.
constexpr double kCellLat = 0.0005;
constexpr double kCellLon = 0.00075;
} // namespace

NatureMap::NatureMap(LatLon min, LatLon max)
    : min_(min),
      nx_(static_cast<int>((max.lon - min.lon) / kCellLon) + 1),
      ny_(static_cast<int>((max.lat - min.lat) / kCellLat) + 1),
      cells_(static_cast<std::size_t>(nx_) * ny_, 0) {
}

double NatureMap::col_f(double lon) const {
    return (lon - min_.lon) / kCellLon;
}
double NatureMap::row_f(double lat) const {
    return (lat - min_.lat) / kCellLat;
}

void NatureMap::fill(const std::vector<std::pair<LatLon, LatLon>>& border) {
    // Intersect the boundary with a line through the middle of each row and fill
    // between pairs of intersections (even-odd rule).
    std::vector<std::pair<int, double>> cuts; // (row, intersection column)
    for (auto [a, b] : border) {
        double ra = row_f(a.lat), rb = row_f(b.lat);
        if (ra > rb) {
            std::swap(a, b);
            std::swap(ra, rb);
        }
        // Rows with midpoints in [ra, rb); horizontal segments do not intersect.
        const int r0 = std::max(0, static_cast<int>(std::ceil(ra - 0.5)));
        const int r1 = std::min(ny_ - 1, static_cast<int>(std::ceil(rb - 0.5)) - 1);
        const double ca = col_f(a.lon), cb = col_f(b.lon);
        for (int r = r0; r <= r1; ++r) {
            cuts.push_back({r, ca + (cb - ca) * (r + 0.5 - ra) / (rb - ra)});
        }
    }
    std::ranges::sort(cuts);

    for (std::size_t i = 0; i + 1 < cuts.size(); i += 2) {
        if (cuts[i].first != cuts[i + 1].first) {
            --i;
            continue;
        } // invalid ring: skip rest of row
        const int c0 = std::max(0, static_cast<int>(std::ceil(cuts[i].second - 0.5)));
        const int c1 = std::min(nx_ - 1, static_cast<int>(std::ceil(cuts[i + 1].second - 0.5)) - 1);
        const std::size_t row = static_cast<std::size_t>(cuts[i].first) * nx_;
        for (int c = c0; c <= c1; ++c) {
            cells_[row + c] = 1;
        }
    }
}

bool NatureMap::contains(LatLon p) const {
    const int c = static_cast<int>(std::floor(col_f(p.lon)));
    const int r = static_cast<int>(std::floor(row_f(p.lat)));
    if (c < 0 || r < 0 || c >= nx_ || r >= ny_) {
        return false;
    }
    return cells_[static_cast<std::size_t>(r) * nx_ + c] != 0;
}

} // namespace rp
