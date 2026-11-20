#include "rp/routing/isochrone_method.hpp"

#include "rp/routing/parallel.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <set>

namespace rp {

namespace {

constexpr int kCorners = 4;
constexpr int kSamples = 360;          // points per ellipse for measuring its arc length
constexpr int kMaxCalibrations = 2;    // rescales of the perimeter after the first build
constexpr std::size_t kMaxPool = 24;
constexpr double kTolerance = 0.05;

// Aspect ratios and direction offsets, in the order of the paper.
constexpr double kAspects[] = {1, 1.0 / 2, 2, 1.0 / 3, 3, 1.0 / 4, 4, 1.0 / 5,  5, 1.0 / 6,
                               6, 1.0 / 7, 7, 1.0 / 8, 8, 1.0 / 9, 9, 1.0 / 10, 10};
constexpr double kOffsets[] = {0, 90, 180, 270, 30, 120, 210, 300, 60, 150, 240, 330};
constexpr std::size_t kNumAspects = std::size(kAspects), kNumOffsets = std::size(kOffsets);

double bearing(LatLon a, LatLon b) {
    const double east = (b.lon - a.lon) * std::cos(deg_to_rad(a.lat));
    return std::atan2(east, b.lat - a.lat) * 180.0 / std::numbers::pi;
}

// Corners on an ellipse through start pointing in direction dir, with the given
// perimeter in metres and aspect = width / length. start itself is left out.
std::vector<LatLon> ellipse_corners(LatLon start, double dir, double aspect, double perimeter) {
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = std::numbers::pi + 2 * std::numbers::pi * i / kSamples;
        pts.push_back({std::cos(t) + 1, aspect * std::sin(t)});
    }
    double unit = 0;
    for (int i = 1; i <= kSamples; ++i) {
        unit += std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
    }
    const double scale = perimeter / unit;

    std::vector<LatLon> corners;
    double walked = 0;
    int next = 1;
    for (int i = 1; i <= kSamples && next < kCorners; ++i) {
        walked += std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
        if (walked < unit * next / kCorners) {
            continue;
        }
        const double along = pts[i].first * scale, across = pts[i].second * scale;
        corners.push_back(move(move(start, dir, along), dir + 90, across));
        ++next;
    }
    return corners;
}

struct Candidate {
    double dir, aspect, perimeter;
    std::vector<LatLon> corners;
};

} // namespace

std::vector<RoundTrip> isochrone_method(std::span<RoundTripPlanner* const> planners, std::uint32_t start,
                                        double target_m, int variants, const Sliders& s) {
    RoundTripPlanner& main = *planners[0];
    const Graph& g = main.graph();
    const LatLon p = g.node_pos[start];

    // 1. Isochrone: nodes within k/2 network distance of the start.
    const auto reached = main.dijkstra().within(start, target_m / 2);
    std::vector<std::uint8_t> in_iso(g.num_nodes(), 0);
    for (const auto& [v, d] : reached) {
        in_iso[v] = 1;
    }

    // 2. Farthest node in a straight line, at distance x and bearing β. That
    //    gives the reference perimeter k' = 2x (at most k). flat_m() with a 2 %
    //    margin filters the nodes, haversine only runs for possible maxima.
    const double cos_lat = std::cos(deg_to_rad(p.lat));
    double x = 0, beta = 0;
    for (const auto& [v, d] : reached) {
        if (1.02 * flat_m(p, g.node_pos[v], cos_lat) < x) {
            continue;
        }
        const double air = haversine_m(p, g.node_pos[v]);
        if (air > x) {
            x = air;
            beta = bearing(p, g.node_pos[v]);
        }
    }
    if (x <= 0) {
        return {};
    }
    const double k_ref = std::min(2 * x, target_m);
    const double radius = 0.4 * target_m / kCorners; // maximum distance corners can shift under bias

    const auto inside = [&](const std::vector<LatLon>& corners, std::vector<std::uint32_t>& nodes) {
        nodes.clear();
        for (const auto& c : corners) {
            const std::uint32_t v = main.index().nearest(c);
            if (v == kNoEdge || !in_iso[v]) {
                return false;
            }
            nodes.push_back(v);
        }
        return true;
    };

    const std::size_t total = 10 * kNumAspects * kNumOffsets;
    const auto make = [&](std::size_t idx) {
        const int step = 10 - static_cast<int>(idx / (kNumAspects * kNumOffsets));
        const double aspect = kAspects[(idx / kNumOffsets) % kNumAspects];
        const double dir = beta + kOffsets[idx % kNumOffsets];
        const double perimeter = step / 10.0 * k_ref;
        return Candidate{dir, aspect, perimeter,
                         main.pick_corners(ellipse_corners(p, dir, aspect, perimeter), s, radius, &in_iso)};
    };

    const auto route = [&](RoundTripPlanner& pl, Candidate c) -> std::optional<RoundTrip> {
        std::optional<RoundTrip> best;
        std::vector<std::uint32_t> nodes;
        for (int k = 0; k <= kMaxCalibrations; ++k) {
            const auto trip = pl.build(start, c.corners, s);
            if (!trip) {
                break;
            }
            if (!best || std::abs(trip->length_m - target_m) < std::abs(best->length_m - target_m)) {
                best = trip;
            }
            if (std::abs(trip->length_m - target_m) <= kTolerance * target_m) {
                break;
            }
            c.perimeter *= std::sqrt(target_m / trip->length_m);
            c.corners = pl.pick_corners(ellipse_corners(p, c.dir, c.aspect, c.perimeter), s, radius, &in_iso);
            if (!inside(c.corners, nodes)) {
                break;
            }
        }
        return best;
    };

    // Candidates are collected in a fixed order, routed in parallel and then
    // evaluated in the same order, so the result does not depend on which
    // thread finishes first.
    std::vector<RoundTrip> pool;
    std::set<std::vector<std::uint32_t>> seen;
    const std::size_t enough = std::max<std::size_t>(variants + 2, 8);
    const std::size_t block = 2 * planners.size();
    std::size_t hits = 0, idx = 0;
    std::vector<std::uint32_t> nodes;

    while (idx < total && pool.size() < kMaxPool && hits < enough) {
        std::vector<Candidate> batch;
        while (idx < total && batch.size() < block) {
            Candidate c = make(idx++);
            if (inside(c.corners, nodes) && seen.insert(nodes).second) {
                batch.push_back(std::move(c));
            }
        }
        std::vector<std::optional<RoundTrip>> results(batch.size());
        parallel_for(planners.size(), batch.size(), [&](std::size_t w, std::size_t i) {
            results[i] = route(*planners[w], std::move(batch[i]));
        });
        for (auto& r : results) {
            if (!r || pool.size() >= kMaxPool || hits >= enough) {
                continue;
            }
            if (std::abs(r->length_m - target_m) <= kTolerance * target_m) {
                ++hits;
            }
            pool.push_back(std::move(*r));
        }
    }

    return pool;
}

} // namespace rp
