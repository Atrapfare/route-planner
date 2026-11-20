#pragma once

// Pareto local search (Lewis & Corcoran 2024, Algorithms 3 and 4). Improves the
// pool routes by replacing sections or inserting small loops. Like in the paper
// it only searches the graph made of the pool's roads, but with Dijkstra on the
// slider costs instead of BFS. The objectives are rounded to keep the archive
// small:
//   f1 = |k − length| (10 m), f2 = overlap (1 %), f3 = cost per km
// Returns at most 24 nondominated routes.

#include "rp/routing/round_trip.hpp"

#include <cstdint>
#include <vector>

namespace rp {

// Time budget per request, the same for server and measurements.
inline constexpr double kLocalSearchBudgetMs = 500;

std::vector<RoundTrip> local_search(RoundTripPlanner& planner, std::uint32_t start, const std::vector<RoundTrip>& pool,
                                    double target_m, const Sliders& s, double budget_ms = kLocalSearchBudgetMs);

} // namespace rp
