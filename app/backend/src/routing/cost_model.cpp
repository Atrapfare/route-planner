#include "rp/routing/cost_model.hpp"

#include <algorithm>

namespace rp {

namespace {

// Map slider values (0..100) piecewise linearly through three anchors:
// 0 -> left, 50 -> mid, 100 -> right.
double anchors(double slider, double left, double mid, double right) {
    const double t = std::clamp(slider, 0.0, 100.0) / 50.0;
    return t <= 1 ? left + (mid - left) * t : mid + (right - mid) * (t - 1);
}

// Road class factor h (Broach): busy roads feel longer, unless they have a
// cycle lane.
double road_factor(RoadClass c, bool cycle_infra) {
    switch (c) {
        case RoadClass::Cycleway:
        case RoadClass::LivingStreet:
            return 0.8;
        case RoadClass::Tertiary:
            return cycle_infra ? 1.0 : 1.2;
        case RoadClass::Secondary:
            return cycle_infra ? 1.0 : 1.8;
        case RoadClass::Primary:
            return cycle_infra ? 1.0 : 2.2;
        case RoadClass::Trunk:
            return cycle_infra ? 1.0 : 2.5;
        case RoadClass::Steps:
            return 10.0;
        default:
            return 1.0;
    }
}

// Surface factor s (1 = asphalt), modelled after Hrnčíř.
double surface_factor(Surface s) {
    switch (s) {
        case Surface::Paved:
            return 1.0;
        case Surface::Paving:
            return 1.1;
        case Surface::Unknown:
            return 1.2;
        case Surface::Gravel:
            return 1.5;
        case Surface::Cobble:
            return 1.6;
        case Surface::Unpaved:
            return 2.0;
    }
    return 1.2;
}

} // namespace

CostModel::CostModel(const Graph& g, const Sliders& s)
    : g_(g),
      beta_(anchors(s.hilly, 12, 6, 0)),
      gamma_(anchors(s.trail, 3, 1, 0)),
      delta_(anchors(s.bike, 0, 0.15, 0.25)),
      eps_(anchors(s.nature, 0, 0.15, 0.25)),
      zeta_(anchors(s.curvy, 1, 0, 0)) {
}

double CostModel::cost(std::uint32_t e) const {
    const double len = g_.length_m[e];
    const std::uint8_t f = g_.flags[e];
    const double c = len * road_factor(g_.road_class[e], f & kCycleInfra)
                   + beta_ * g_.ascent_m[e]
                   + gamma_ * (surface_factor(g_.surface[e]) - 1) * len
                   + delta_ * !(f & kBikeRoute) * len
                   + eps_ * !(f & kNature) * len
                   + zeta_ * std::min(g_.curvy_deg_km[e] / 1000.0, 1.0) * len;
    return (used_ && (*used_)[e]) ? 2 * c : c;
}

double CostModel::lower_bound(const Graph& g, std::uint32_t e) {
    return g.length_m[e] * road_factor(g.road_class[e], g.flags[e] & kCycleInfra);
}

} // namespace rp
