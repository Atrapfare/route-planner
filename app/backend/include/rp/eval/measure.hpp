#pragma once

// Measurement series (route_planner --measure DIR). Runs the round-trip search
// directly, without HTTP, for every start point, length and slider profile and
// writes the raw results.
//
//   sweep     single sliders at 0/25/75/100 plus combined profiles, all lengths,
//             isochrone method with local search
//   ablation  0/100 and the combined profiles again, each with one mechanism
//             switched off (corner bias, selection, cost model), 25 km only
//   methods   polygon method and isochrone without local search, 25 km
//
// Files in DIR:
//   requests.csv  one row per request (settings, pool size, timings)
//   routes.csv    one row per returned route, linked to its request by req
//   meta.json     graph, threads, local search budget, seed, compiler, duration

#include "rp/graph/graph.hpp"
#include "rp/routing/landmarks.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rp {

struct MeasureOptions {
    std::filesystem::path out_dir;   // created if needed
    std::size_t threads = 0;         // method worker count (0 = automatic)
    std::size_t random_starts = 100; // random start points in addition to five fixed locations
    std::uint64_t seed = 20261116;   // fixed seed for reproducible start points
    std::string graph_name;          // metadata only
};

// Without landmarks (nullptr) plain Dijkstra is used.
void run_measurement(const Graph& g, const Landmarks* landmarks, const MeasureOptions& opt);

// Compares Dijkstra and ALT on random pairs (visited nodes, time). Aborts if
// the costs ever differ, since ALT has to be exact.
void run_alt_benchmark(const Graph& g, Landmarks& landmarks);

} // namespace rp
