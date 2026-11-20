#pragma once

// HTTP API for the frontend. All endpoints are GET and return JSON:
//   /api/bounds                                   extent of the loaded graph
//   /api/nearest?lat=..&lon=..                    nearest node to a map click
//   /api/route?from_lat&from_lon&to_lat&to_lon     route from A to B
//              [&hilly&bike&nature&trail&curvy]    optional sliders (0..100; 50 = neutral)
//   /api/pois?q=..[&lat&lon][&limit]               search POIs by name
//   /api/round-route?lat&lon&length_km             round trips from a start point
//              [&variants&method&poi&via&ls + sliders]

#include "rp/graph/graph.hpp"
#include "rp/routing/landmarks.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace rp {

// Blocks until the server stops; on_ready is called once it accepts requests.
// Returns false if the port cannot be opened. landmarks may be nullptr (plain
// Dijkstra), threads = 0 uses the number of cores (at most 8).
bool run_server(const Graph& g, const Landmarks* landmarks, std::size_t threads, const std::string& host, int port,
                const std::function<void()>& on_ready);

} // namespace rp
