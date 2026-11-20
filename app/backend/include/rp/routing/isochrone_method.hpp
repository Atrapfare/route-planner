#pragma once

// Isochrone polygon method (Lewis & Corcoran 2024, Algorithm 1).

#include "rp/routing/round_trip.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rp {

// One planner per worker, all with the same POIs. planners[0] also computes
// the isochrone.
std::vector<RoundTrip> isochrone_method(std::span<RoundTripPlanner* const> planners, std::uint32_t start,
                                        double target_m, int variants, const Sliders& s);

} // namespace rp
