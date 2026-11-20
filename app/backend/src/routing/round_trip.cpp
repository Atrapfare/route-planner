#include "rp/routing/round_trip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace rp {

namespace {
// Corners more than 1 km from the network are rejected.
constexpr double kMaxSnapM = 1000.0;

// Threshold for the ascent of a whole tour (also /api/route). With the 2 m from
// the import, SRTM noise still adds up over a tour; 5 m came close to what
// Komoot shows for the same route.
constexpr double kTourMinClimbM = 5.0;

// Slider axes in order: hilly, cycle route, nature, unpaved, curvy.
using Axes = std::array<double, 5>;

Axes bias_of(const Sliders& s) {
    return {(s.hilly - 50) / 50, (s.bike - 50) / 50, (s.nature - 50) / 50, (s.trail - 50) / 50, (s.curvy - 50) / 50};
}

// Node features for corner selection: elevation and shares of outgoing
// cycle-route, nature, unpaved and curvy edges.
Axes node_features(const Graph& g, std::uint32_t v) {
    Axes f{g.node_elevation_m[v], 0, 0, 0, 0};
    const std::uint32_t n = g.first_out[v + 1] - g.first_out[v];
    for (std::uint32_t e = g.first_out[v]; e < g.first_out[v + 1]; ++e) {
        f[1] += (g.flags[e] & kBikeRoute) != 0;
        f[2] += (g.flags[e] & kNature) != 0;
        f[3] += g.surface[e] == Surface::Gravel || g.surface[e] == Surface::Unpaved;
        f[4] += g.curvy_deg_km[e];
    }
    for (int k = 1; k < 5 && n > 0; ++k) {
        f[k] /= n;
    }
    return f;
}

// Scales every feature to 0..1 over the candidates, so elevation and
// percentages can be compared.
void normalize(std::vector<Axes>& rows) {
    for (int k = 0; k < 5; ++k) {
        double lo = rows[0][k], hi = rows[0][k];
        for (const auto& r : rows) {
            lo = std::min(lo, r[k]);
            hi = std::max(hi, r[k]);
        }
        for (auto& r : rows) {
            r[k] = hi > lo ? (r[k] - lo) / (hi - lo) : 0.5;
        }
    }
}

// 2-opt on the stop order: reverse sections as long as that makes the
// straight-line loop start -> stops -> start shorter. Fixes the detours the
// angle sort leaves behind. The start stays fixed; with at most 13 stops
// (3 corners, 10 POIs) this is cheap.
void improve_order(const Graph& g, std::uint32_t start, std::vector<std::uint32_t>& stops) {
    std::vector<std::uint32_t> t;
    t.reserve(stops.size() + 2);
    t.push_back(start);
    t.insert(t.end(), stops.begin(), stops.end());
    t.push_back(start);
    const auto d = [&](std::size_t a, std::size_t b) {
        return haversine_m(g.node_pos[t[a]], g.node_pos[t[b]]);
    };
    // At least 1 m, otherwise rounding could keep it going forever.
    constexpr double kMinGainM = 1.0;
    for (bool improved = true; improved;) {
        improved = false;
        for (std::size_t i = 1; i + 1 < t.size(); ++i) {
            for (std::size_t j = i + 1; j + 1 < t.size(); ++j) {
                const double gain = d(i - 1, i) + d(j, j + 1) - d(i - 1, j) - d(i, j + 1);
                if (gain > kMinGainM) {
                    std::reverse(t.begin() + static_cast<std::ptrdiff_t>(i),
                                 t.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    improved = true;
                }
            }
        }
    }
    stops.assign(t.begin() + 1, t.end() - 1);
}
} // namespace

std::vector<LatLon> RoundTripPlanner::pick_corners(const std::vector<LatLon>& corners, const Sliders& s,
                                                   double radius_m, const std::vector<std::uint8_t>* allowed) const {
    const Axes bias = bias_of(s);
    if (!corner_bias_ || std::ranges::all_of(bias, [](double b) {
            return b == 0;
        })) {
        return corners;
    }

    std::vector<LatLon> out;
    for (const LatLon& c : corners) {
        std::vector<std::uint32_t> cand;
        for (int i = 0; i <= 8; ++i) {
            const LatLon q = i == 0 ? c : move(c, i * 45.0, radius_m);
            const std::uint32_t v = index_.nearest(q);
            if (v == kNoEdge || haversine_m(q, g_.node_pos[v]) > kMaxSnapM) {
                continue;
            }
            if (allowed && !(*allowed)[v]) {
                continue;
            }
            cand.push_back(v);
        }
        if (cand.empty()) {
            out.push_back(c);
            continue;
        }

        // Score = sum of bias × normalised feature, the highest wins. On a tie
        // the first one stays, usually the original corner.
        std::vector<Axes> f;
        for (const auto v : cand) {
            f.push_back(node_features(g_, v));
        }
        normalize(f);
        std::size_t best = 0;
        double best_score = -1e9;
        for (std::size_t i = 0; i < cand.size(); ++i) {
            double score = 0;
            for (int k = 0; k < 5; ++k) {
                score += bias[k] * f[i][k];
            }
            if (score > best_score + 1e-9) {
                best_score = score;
                best = i;
            }
        }
        out.push_back(g_.node_pos[cand[best]]);
    }
    return out;
}

RoundTripPlanner::RoundTripPlanner(const Graph& g, const SpatialIndex& index, Dijkstra& dijkstra)
    : g_(g),
      index_(index),
      dijkstra_(dijkstra),
      used_(g.num_edges(), 0) {
}

Workers::Workers(const Graph& g, const SpatialIndex& index, const Landmarks* lm, std::size_t threads) {
    if (threads == 0) {
        threads = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 8);
    }
    for (std::size_t i = 0; i < threads; ++i) {
        dijkstras.push_back(std::make_unique<Dijkstra>(g));
        dijkstras.back()->set_landmarks(lm);
        owned.push_back(std::make_unique<RoundTripPlanner>(g, index, *dijkstras.back()));
        planners.push_back(owned.back().get());
    }
}

std::optional<RoundTrip> RoundTripPlanner::build(std::uint32_t start, const std::vector<LatLon>& corners,
                                                 const Sliders& s) {
    // 1. Snap corners to nodes and order them together with the POIs.
    std::vector<std::uint32_t> stops;
    for (const LatLon& c : corners) {
        const std::uint32_t v = index_.nearest(c);
        if (v == kNoEdge || haversine_m(c, g_.node_pos[v]) > kMaxSnapM) {
            return std::nullopt;
        }
        stops.push_back(v);
    }
    if (!pois_.empty()) {
        stops.insert(stops.end(), pois_.begin(), pois_.end());
        // Sort the stops by angle around the centroid of start and POIs,
        // beginning in the direction of the start, so the route does not cross
        // itself. improve_order() fixes what the sorting gets wrong.
        LatLon m = g_.node_pos[start];
        for (const auto p : pois_) {
            m.lat += g_.node_pos[p].lat;
            m.lon += g_.node_pos[p].lon;
        }
        m = {m.lat / (pois_.size() + 1), m.lon / (pois_.size() + 1)};
        const auto angle = [&](std::uint32_t v) {
            const LatLon p = g_.node_pos[v];
            return std::atan2(p.lat - m.lat, (p.lon - m.lon) * std::cos(deg_to_rad(m.lat)));
        };
        const double a0 = angle(start);
        const auto rel = [&](std::uint32_t v) {
            double a = angle(v) - a0;
            while (a <= 0) {
                a += 2 * std::numbers::pi;
            }
            return a;
        };
        std::ranges::sort(stops, {}, rel);
        improve_order(g_, start, stops);
    }
    stops.push_back(start);

    // 2. Route the legs one after another; edges used by earlier legs cost
    //    twice as much (in both directions). Reset used_ afterwards.
    CostModel cost(g_, s);
    cost.set_used(&used_);
    std::vector<std::uint32_t> edges;
    std::uint32_t from = start;
    bool ok = true;
    for (const std::uint32_t to : stops) {
        if (to == from) {
            continue;
        }
        const auto leg = dijkstra_.shortest_path(from, to, cost);
        if (!leg) {
            ok = false;
            break;
        }
        for (const auto e : leg->edges) {
            edges.push_back(e);
            used_[e] = 1;
            if (g_.twin[e] != kNoEdge) {
                used_[g_.twin[e]] = 1;
            }
        }
        from = to;
    }
    for (const auto e : edges) {
        used_[e] = 0;
        if (g_.twin[e] != kNoEdge) {
            used_[g_.twin[e]] = 0;
        }
    }
    if (!ok || edges.empty()) {
        return std::nullopt;
    }

    // 3. Remove out-and-back spurs (nested ones too) with a stack. Spurs that
    //    end at a POI stay, the tour has to get there after all.
    std::vector<std::uint32_t> clean;
    const auto is_poi = [&](std::uint32_t v) {
        return std::ranges::find(pois_, v) != pois_.end();
    };
    for (const auto e : edges) {
        if (!clean.empty() && g_.twin[clean.back()] == e && !is_poi(g_.head[clean.back()])) {
            clean.pop_back();
        } else {
            clean.push_back(e);
        }
    }
    if (clean.empty()) {
        return std::nullopt;
    }

    return evaluate(std::move(clean), s);
}

RoundTrip RoundTripPlanner::evaluate(std::vector<std::uint32_t> edges, const Sliders& s) const {
    RoundTrip t = measure(std::move(edges));
    const CostModel plain(g_, s);
    double total = 0;
    for (const auto e : t.edges) {
        total += plain.cost(e);
    }
    t.cost_per_km = t.length_m > 0 ? total / (t.length_m / 1000.0) : 0;
    return t;
}

RoundTrip RoundTripPlanner::measure(std::vector<std::uint32_t> edges) const {
    RoundTrip t;
    double bike = 0, nature = 0, unpaved = 0, curvy = 0, repeated = 0;
    // Count traversals per road, treating opposite directions as one road.
    std::unordered_map<std::uint32_t, int> times;
    if (!edges.empty()) {
        t.ascent_m = climb_along(g_, g_.head[edges.back()], edges).ascent_m;
    }
    for (const auto e : edges) {
        const double len = g_.length_m[e];
        const std::uint8_t f = g_.flags[e];
        t.length_m += len;
        if (f & kBikeRoute) {
            bike += len;
        }
        if (f & kNature) {
            nature += len;
        }
        curvy += g_.curvy_deg_km[e] * len;
        if (g_.surface[e] == Surface::Gravel || g_.surface[e] == Surface::Unpaved) {
            unpaved += len;
        }
        const std::uint32_t street = std::min(e, g_.twin[e]); // kNoEdge exceeds all real edge IDs
        if (times[street]++ > 0) {
            repeated += len;
        }
    }
    const auto pct = [&](double x) {
        return t.length_m > 0 ? 100 * x / t.length_m : 0;
    };
    t.overlap_pct = pct(repeated);
    t.bike_pct = pct(bike);
    t.nature_pct = pct(nature);
    t.unpaved_pct = pct(unpaved);
    t.curvy_deg_km = t.length_m > 0 ? curvy / t.length_m : 0;
    t.edges = std::move(edges);
    return t;
}

Climb climb_along(const Graph& g, std::uint32_t start, std::span<const std::uint32_t> edges) {
    // The threshold applies across edges, so the per-edge ascent_m cannot just
    // be summed up. ref = last counted elevation. No extra samples every 100 m
    // like in the import, the OSM points are dense enough.
    Climb c;
    double ref = g.node_elevation_m[start];
    const auto visit = [&](double h) {
        if (h - ref >= kTourMinClimbM) {
            c.ascent_m += h - ref;
            ref = h;
        } else if (ref - h >= kTourMinClimbM) {
            c.descent_m += ref - h;
            ref = h;
        }
    };
    for (const auto e : edges) {
        for (std::uint32_t i = g.geo_first[e]; i < g.geo_first[e + 1]; ++i) {
            visit(g.geo_elevation_m[i]);
        }
        visit(g.node_elevation_m[g.head[e]]);
    }
    return c;
}

double shared(const Graph& g, const RoundTrip& a, const RoundTrip& b) {
    std::unordered_set<std::uint32_t> streets;
    for (const auto e : b.edges) {
        streets.insert(std::min(e, g.twin[e]));
    }
    double common = 0;
    for (const auto e : a.edges) {
        if (streets.count(std::min(e, g.twin[e]))) {
            common += g.length_m[e];
        }
    }
    return common / std::min(a.length_m, b.length_m);
}

std::vector<RoundTrip> choose(const Graph& g, std::vector<RoundTrip> pool, double target_m, int variants,
                              const Sliders& s) {
    constexpr double kTolerance = 0.05, kCostBand = 1.20, kOverlapBand = 2.0, kMaxShared = 0.70, kRelaxedShared = 0.85;
    if (pool.empty()) {
        return {};
    }

    // 1. Keep only tours within the length tolerance; if there are none,
    //    return the closest one, marked as a miss.
    const auto off = [&](const RoundTrip& t) {
        return std::abs(t.length_m - target_m) / target_m;
    };
    for (auto& t : pool) {
        t.within_tolerance = off(t) <= kTolerance;
    }
    if (std::ranges::none_of(pool, &RoundTrip::within_tolerance)) {
        auto nearest = std::ranges::min_element(pool, {}, off);
        return {std::move(*nearest)};
    }
    std::erase_if(pool, [](const RoundTrip& t) {
        return !t.within_tolerance;
    });

    // 2–3. Tours outside the cost/overlap bands are only flagged, they are
    //      still used if there would be too few variants otherwise.
    const double best_cost = std::ranges::min(pool, {}, &RoundTrip::cost_per_km).cost_per_km;
    const double best_overlap = std::ranges::min(pool, {}, &RoundTrip::overlap_pct).overlap_pct;
    const auto in_bands = [&](const RoundTrip& t) {
        return t.cost_per_km <= kCostBand * best_cost && t.overlap_pct <= best_overlap + kOverlapBand;
    };

    // 4. Normalise the metrics to 0..1 and compare them with the sliders that
    //    are not neutral. Smallest distance first, ties by cost.
    const Axes bias = bias_of(s);
    std::vector<Axes> m;
    for (const auto& t : pool) {
        m.push_back({t.ascent_m / (t.length_m / 1000), t.bike_pct, t.nature_pct, t.unpaved_pct, t.curvy_deg_km});
    }
    normalize(m);
    const Axes goal{s.hilly / 100, s.bike / 100, s.nature / 100, s.trail / 100, s.curvy / 100};
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        double d = 0;
        for (int k = 0; k < 5; ++k) {
            if (bias[k] != 0) {
                d += (m[i][k] - goal[k]) * (m[i][k] - goal[k]);
            }
        }
        // Round to nine decimal places so numerical noise does not break ties.
        order.push_back({std::round(d * 1e9) / 1e9, i});
    }
    std::ranges::sort(order, [&](const auto& a, const auto& b) {
        return std::pair{a.first, pool[a.second].cost_per_km} < std::pair{b.first, pool[b.second].cost_per_km};
    });
    std::vector<RoundTrip> sorted;
    for (const auto& [d, i] : order) {
        sorted.push_back(std::move(pool[i]));
    }
    pool = std::move(sorted);

    // Select variants in two passes:
    // Pass 1: both quality bands and < 70 % shared distance.
    // Pass 2 if needed: matching length and < 85 % shared distance.
    std::vector<RoundTrip> picked;
    std::vector<std::uint8_t> taken(pool.size(), 0);
    const auto fill = [&](bool strict, double max_shared) {
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (static_cast<int>(picked.size()) >= variants) {
                return;
            }
            if (taken[i] || (strict && !in_bands(pool[i]))) {
                continue;
            }
            const bool different = std::ranges::all_of(picked, [&](const RoundTrip& p) {
                return shared(g, pool[i], p) < max_shared;
            });
            if (different) {
                picked.push_back(pool[i]);
                taken[i] = 1;
            }
        }
    };
    fill(true, kMaxShared);
    fill(false, kRelaxedShared);
    return picked;
}

} // namespace rp
