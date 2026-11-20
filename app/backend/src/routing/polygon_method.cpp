#include "rp/routing/polygon_method.hpp"

#include "rp/routing/parallel.hpp"

#include <cmath>
#include <numbers>
#include <optional>

namespace rp {

namespace {

constexpr int kCorners = 4;
constexpr int kMaxTries = 8;
constexpr double kTolerance = 0.05;
// After a failed build, first rotate the whole polygon by these angles ...
constexpr double kTurns[] = {15, -15, 30, -30, 45, -45};
constexpr int kMaxTurns = 6;
// ... then shrink, but never below 60 % of the initial radius.
constexpr double kMinRadius = 0.6;

// All vertices except the start. The centre is ρ metres from the start in
// direction θ, so seen from the centre the start is at θ+180°.
std::vector<LatLon> corners(LatLon start, double theta, double rho) {
    const LatLon center = move(start, theta, rho);
    std::vector<LatLon> c;
    for (int i = 1; i < kCorners; ++i) {
        c.push_back(move(center, theta + 180.0 + i * 360.0 / kCorners, rho));
    }
    return c;
}

std::optional<RoundTrip> one_direction(RoundTripPlanner& planner, std::uint32_t start, double theta, double target_m,
                                       const Sliders& s) {
    const LatLon p = planner.graph().node_pos[start];
    const double rho0 = target_m / (2 * kCorners * std::sin(std::numbers::pi / kCorners));
    double rho = rho0;
    double turn = 0;
    int failures = 0;
    std::optional<RoundTrip> best;

    for (int t = 0; t < kMaxTries + kMaxTurns; ++t) {
        const auto picked = planner.pick_corners(corners(p, theta + turn, rho), s, 0.4 * target_m / kCorners, nullptr);
        const auto trip = planner.build(start, picked, s);
        if (!trip) {
            // Build failed: rotate first, then shrink (see kTurns, kMinRadius).
            if (failures < kMaxTurns) {
                turn = kTurns[failures++];
            } else if (rho > kMinRadius * rho0 + 1e-6) {
                rho = std::max(rho * 0.85, kMinRadius * rho0);
            } else {
                break;
            }
            continue;
        }
        if (!best || std::abs(trip->length_m - target_m) < std::abs(best->length_m - target_m)) {
            best = trip;
        }
        if (std::abs(trip->length_m - target_m) <= kTolerance * target_m) {
            break;
        }
        rho *= std::sqrt(target_m / trip->length_m); // shrink if too long; expand if too short
    }
    return best;
}

} // namespace

std::vector<RoundTrip> polygon_method(std::span<RoundTripPlanner* const> planners, std::uint32_t start, double target_m,
                                      int variants, const Sliders& s) {
    const int directions = 2 * variants;
    std::vector<std::optional<RoundTrip>> results(directions);
    parallel_for(planners.size(), results.size(), [&](std::size_t w, std::size_t v) {
        results[v] = one_direction(*planners[w], start, v * 360.0 / directions, target_m, s);
    });
    std::vector<RoundTrip> result;
    for (auto& r : results) {
        if (r) {
            result.push_back(std::move(*r));
        }
    }
    return result;
}

} // namespace rp
