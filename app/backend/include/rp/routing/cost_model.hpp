#pragma once

// Edge costs for cycling:
//
// cost(e) = length·h                    base road-class factor h (Broach)
//         + β·ascent                    climbing penalty per metre (Storandt, Hrnčíř)
//         + γ·(s−1)·length              poor-surface penalty, surface factor s
//         + δ·[not on cycle route]·length
//         + ε·[not in nature]·length
//         + ζ·min(curviness/1000°, 1)·length
//
// Costs have to stay nonnegative (Funke & Storandt), so a preference is a
// penalty on everything else, never a bonus. The five sliders set β, γ, δ, ε
// and ζ.

#include "rp/graph/graph.hpp"

#include <cstdint>
#include <vector>

namespace rp {

// Frontend sliders, each 0..100 (50 = neutral).
struct Sliders {
    double hilly = 50;
    double bike = 50;
    double nature = 50;
    double trail = 50;
    double curvy = 50;
};

class CostModel {
public:
    CostModel(const Graph& g, const Sliders& s);

    // Cost of edge e in perceived metres; never below lower_bound(g, e).
    double cost(std::uint32_t e) const;

    // Lowest possible cost of e over all slider settings (length · h, all other
    // terms can be zero). ALT is built on this.
    static double lower_bound(const Graph& g, std::uint32_t e);

    // Edges with used[e] != 0 cost double, so a round trip avoids riding the
    // same road twice. nullptr turns this off.
    void set_used(const std::vector<std::uint8_t>* used) {
        used_ = used;
    }

private:
    const Graph& g_;
    double beta_, gamma_, delta_, eps_, zeta_;
    const std::vector<std::uint8_t>* used_ = nullptr;
};

} // namespace rp
