#pragma once

// Polygon method: route along a regular n-gon with the start as one vertex and
// the centre ρ metres away in direction θ. We begin with ρ = k / (2n·sin(π/n)),
// a perimeter of k as the crow flies. Roads are longer than that, so
// ρ ← ρ·√(k/L) is repeated until the route is within 5 % of k. Several
// directions θ run in parallel and give the candidates.

#include "rp/routing/round_trip.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rp {

// One planner per thread, all with the same POIs.
std::vector<RoundTrip> polygon_method(std::span<RoundTripPlanner* const> planners, std::uint32_t start, double target_m,
                                      int variants, const Sliders& s);

} // namespace rp
