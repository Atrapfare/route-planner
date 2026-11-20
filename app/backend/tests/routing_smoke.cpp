#include "rp/graph/graph.hpp"
#include "rp/routing/cost_model.hpp"
#include "rp/routing/dijkstra.hpp"
#include "rp/routing/round_trip.hpp"
#include "rp/routing/spatial_index.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Small hand-made graph: four one-way streets in a loop plus a one-way shortcut
// from node 0 to node 2.
rp::Graph example_graph() {
    rp::Graph g;
    g.node_pos = {{0, 0}, {0, 0.01}, {0.01, 0.01}, {0.01, 0}};
    g.node_elevation_m = {0, 0, 0, 0};
    g.first_out = {0, 2, 3, 4, 5};
    g.head = {1, 2, 2, 3, 0};
    g.twin = {rp::kNoEdge, rp::kNoEdge, rp::kNoEdge, rp::kNoEdge, rp::kNoEdge};
    g.length_m = {1000, 500, 1000, 1000, 1000};
    g.ascent_m = {0, 0, 0, 0, 0};
    g.descent_m = {0, 0, 0, 0, 0};
    g.curvy_deg_km = {0, 0, 0, 0, 0};
    g.road_class.assign(5, rp::RoadClass::Residential);
    g.surface.assign(5, rp::Surface::Paved);
    g.flags.assign(5, 0);
    g.geo_first = {0, 0, 0, 0, 0, 0};
    return g;
}

void test_routing() {
    const rp::Graph g = example_graph();
    rp::SpatialIndex index(g);
    rp::Dijkstra search(g);
    const rp::Sliders neutral{};
    const rp::CostModel cost(g, neutral);

    check(index.nearest(g.node_pos[2]) == 2, "coordinate snapping must find node 2");
    const auto outward = search.shortest_path(0, 2, cost);
    check(outward && outward->edges == std::vector<std::uint32_t>{1} && outward->length_m == 500,
          "the outward route must take the one-way shortcut");
    const auto return_path = search.shortest_path(2, 0, cost);
    check(return_path && return_path->edges == std::vector<std::uint32_t>{3, 4}
              && return_path->length_m == 2000,
          "the return route must follow the directed loop, not reverse the shortcut");
    const auto within = search.within(0, 500);
    check(within.size() == 2, "the 500 m radius must contain only the start and shortcut endpoint");
    check(search.shortest_path(0, 2, cost)->edges == outward->edges,
          "search state must reset between radius and shortest-path queries");

    rp::RoundTripPlanner planner(g, index, search);
    const auto loop = planner.build(0, {g.node_pos[1], g.node_pos[2]}, neutral);
    check(loop && loop->edges == std::vector<std::uint32_t>{0, 2, 3, 4}
              && loop->length_m == 4000,
          "corners must produce a closed four-street loop");
    const auto selected = rp::choose(g, {*loop}, 4000, 1, neutral);
    check(selected.size() == 1 && selected[0].within_tolerance,
          "a target-length loop must be returned as within tolerance");
    const auto miss = rp::choose(g, {*loop}, 3000, 1, neutral);
    check(miss.size() == 1 && !miss[0].within_tolerance,
          "an out-of-tolerance fallback must be explicitly marked");

    planner.set_pois({2});
    const auto poi_loop = planner.build(0, {}, neutral);
    check(poi_loop && poi_loop->edges == std::vector<std::uint32_t>{1, 3, 4}
              && poi_loop->length_m == 2500,
          "the POI-only tour must visit node 2 and return to the start");
    planner.set_pois({});
    check(!planner.build(0, {}, neutral), "a loop without corners or POIs must be rejected");
}

} // namespace

int main() {
    try {
        test_routing();
        std::cout << "Synthetic routing smoke test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Synthetic routing smoke test failed: " << e.what() << '\n';
        return 1;
    }
}
