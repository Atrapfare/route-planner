#pragma once

// The methods pick corners and call build(), the local search puts its edge
// sequences together itself and calls evaluate(). choose() then picks the
// routes to show from the candidate pool.

#include "rp/graph/graph.hpp"
#include "rp/routing/cost_model.hpp"
#include "rp/routing/dijkstra.hpp"
#include "rp/routing/spatial_index.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rp {

struct RoundTrip {
    std::vector<std::uint32_t> edges;
    double length_m = 0;
    double ascent_m = 0;
    double overlap_pct = 0;
    // Share of the length on cycle routes, in nature and unpaved, in percent.
    double bike_pct = 0, nature_pct = 0, unpaved_pct = 0;
    double curvy_deg_km = 0;
    // Slider cost per km, without the penalty for repeated edges.
    double cost_per_km = 0;
    bool within_tolerance = true;
};

// Picks the routes to show from the candidate pool:
//   1. length within ±5 % (if none is, only the closest one, marked as a miss)
//   2. cost/km at most 20 % above the cheapest
//   3. overlap at most two percentage points above the lowest
//   4. sorted by how well ascent/km, cycle routes, nature, unpaved and
//      curviness fit the sliders, then by cost; each further variant shares
//      less than 70 % of its path with the ones before
// If fewer than `variants` routes pass, a second round drops 2 and 3 and
// allows 85 % shared path.
std::vector<RoundTrip> choose(const Graph& g, std::vector<RoundTrip> pool, double target_m, int variants,
                              const Sliders& s);

// Fraction (0..1) of the shorter route that the other one also uses, in
// either direction.
double shared(const Graph& g, const RoundTrip& a, const RoundTrip& b);

// Ascent and descent along the whole path, ignoring changes under 5 m since the
// last counted height. /api/route uses this too. The cost model works with the
// per-edge ascent_m from the import instead (2 m threshold).
struct Climb {
    double ascent_m = 0, descent_m = 0;
};
Climb climb_along(const Graph& g, std::uint32_t start, std::span<const std::uint32_t> edges);

class RoundTripPlanner {
public:
    RoundTripPlanner(const Graph& g, const SpatialIndex& index, Dijkstra& dijkstra);

    // Routes start -> corners -> start. nullopt if a corner is off the map or a
    // leg cannot be routed.
    std::optional<RoundTrip> build(std::uint32_t start, const std::vector<LatLon>& corners, const Sliders& s);

    // Moves each corner to a nearby node that fits the sliders better. The
    // candidates are the corner itself and eight points on a circle of
    // radius_m. If allowed is set, only nodes with allowed[v] != 0 count.
    // With all sliders at 50 nothing moves.
    std::vector<LatLon> pick_corners(const std::vector<LatLon>& corners, const Sliders& s, double radius_m,
                                     const std::vector<std::uint8_t>* allowed) const;

    // Stops (node IDs) that every following build() has to visit. The method's
    // corners go in between.
    void set_pois(std::vector<std::uint32_t> poi_nodes) {
        pois_ = std::move(poi_nodes);
    }
    const std::vector<std::uint32_t>& pois() const {
        return pois_;
    }

    // Only for the ablation: when off, pick_corners() leaves the corners alone.
    void set_corner_bias(bool on) {
        corner_bias_ = on;
    }

    const Graph& graph() const {
        return g_;
    }
    const SpatialIndex& index() const {
        return index_;
    }
    Dijkstra& dijkstra() {
        return dijkstra_;
    }

    // Metrics and cost/km of a finished edge sequence (build() and local search).
    RoundTrip evaluate(std::vector<std::uint32_t> edges, const Sliders& s) const;

private:
    RoundTrip measure(std::vector<std::uint32_t> edges) const;

    const Graph& g_;
    const SpatialIndex& index_;
    Dijkstra& dijkstra_;
    std::vector<std::uint8_t> used_;
    std::vector<std::uint32_t> pois_;
    bool corner_bias_ = true;
};

// One planner and Dijkstra per worker thread; graph, grid and landmarks are shared.
struct Workers {
    // threads = 0: number of cores, at most 8.
    Workers(const Graph& g, const SpatialIndex& index, const Landmarks* lm, std::size_t threads = 0);

    RoundTripPlanner& main() {
        return *planners[0];
    }
    void set_pois(const std::vector<std::uint32_t>& p) {
        for (auto* pl : planners) {
            pl->set_pois(p);
        }
    }
    void set_corner_bias(bool on) {
        for (auto* pl : planners) {
            pl->set_corner_bias(on);
        }
    }

    std::vector<std::unique_ptr<Dijkstra>> dijkstras;
    std::vector<std::unique_ptr<RoundTripPlanner>> owned;
    std::vector<RoundTripPlanner*> planners;
};

} // namespace rp
