#pragma once

// ALT: A* with landmarks and the triangle inequality (Goldberg & Harrelson 2005).
//
// For 16 landmarks on the edge of the region we precompute the distance to and
// from every node. They use the lowest possible edge costs
// (CostModel::lower_bound), so the bound holds for any slider setting:
//   π(v) = max over landmarks l: max(d(l→t) − d(l→v), d(v→l) − d(t→l), 0)

#include "rp/graph/graph.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace rp {

// State of one search, kept separate so several threads can use the same
// Landmarks at once.
struct LandmarkQuery {
    std::uint32_t target = 0;
    std::array<std::uint32_t, 8> active{};
    std::uint32_t count = 0;
};

class Landmarks {
public:
    // Loads the cached file if its fingerprint matches the graph and cost model,
    // otherwise computes the tables and saves them.
    static Landmarks load_or_build(const Graph& g, const std::filesystem::path& file);

    // Picks the eight landmarks with the best bound for s -> t.
    LandmarkQuery prepare(std::uint32_t s, std::uint32_t t) const;

    // Lower bound on the remaining cost from v to the query target.
    double bound(const LandmarkQuery& q, std::uint32_t v) const;

private:
    // Node-major: all landmark values of a node are next to each other, which
    // is what bound() reads.
    std::size_t at(std::uint32_t v, std::uint32_t l) const {
        return static_cast<std::size_t>(v) * count_ + l;
    }

    std::uint32_t n_ = 0, count_ = 0;
    std::vector<float> from_;
    std::vector<float> to_;
};

} // namespace rp
