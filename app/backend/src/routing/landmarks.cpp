#include "rp/routing/landmarks.hpp"

#include "rp/routing/cost_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <numbers>
#include <print>
#include <queue>
#include <utility>

namespace rp {

namespace {

constexpr std::uint32_t kCount = 16;
constexpr std::uint32_t kActive = 8;
constexpr std::uint32_t kMagic = 0x314B4D4C;
constexpr std::uint32_t kVersion = 3;

// FNV-1a hash over the graph layout and the edge lower bounds. If either one
// changes, the cached landmarks are rebuilt; stale bounds would quietly give
// non-optimal paths.
std::uint64_t fingerprint(const Graph& g) {
    std::uint64_t h = 14695981039346656037ull;
    const auto mix = [&](std::uint64_t x) {
        h = (h ^ x) * 1099511628211ull;
    };
    for (const auto first : g.first_out) {
        mix(first);
    }
    for (std::uint32_t e = 0; e < g.num_edges(); ++e) {
        mix(g.head[e]);
        mix(std::bit_cast<std::uint32_t>(static_cast<float>(CostModel::lower_bound(g, e))));
    }
    return h;
}

struct Adj {
    std::vector<std::uint32_t> first, to;
    std::vector<float> w;
};

std::vector<float> all_distances(const Adj& a, std::uint32_t src) {
    const std::uint32_t n = static_cast<std::uint32_t>(a.first.size()) - 1;
    std::vector<float> d(n, std::numeric_limits<float>::infinity());
    using Entry = std::pair<float, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> q;
    d[src] = 0;
    q.push({0, src});
    while (!q.empty()) {
        const auto [dv, v] = q.top();
        q.pop();
        if (dv > d[v]) {
            continue;
        }
        for (std::uint32_t k = a.first[v]; k < a.first[v + 1]; ++k) {
            const float nd = dv + a.w[k];
            if (nd < d[a.to[k]]) {
                d[a.to[k]] = nd;
                q.push({nd, a.to[k]});
            }
        }
    }
    return d;
}

} // namespace

Landmarks Landmarks::load_or_build(const Graph& g, const std::filesystem::path& file) {
    Landmarks lm;
    lm.n_ = g.num_nodes();
    lm.count_ = kCount;
    const std::size_t size = static_cast<std::size_t>(lm.n_) * kCount;

    // 1. Reuse the cached tables if they were built for this graph.
    const std::uint64_t graph_hash = fingerprint(g);
    if (std::ifstream in{file, std::ios::binary}) {
        std::uint32_t head[4]{};
        std::uint64_t stored_hash = 0;
        in.read(reinterpret_cast<char*>(head), sizeof(head));
        in.read(reinterpret_cast<char*>(&stored_hash), sizeof(stored_hash));
        if (in && head[0] == kMagic && head[1] == kVersion && head[2] == lm.n_ && head[3] == kCount
            && stored_hash == graph_hash) {
            lm.from_.resize(size);
            lm.to_.resize(size);
            in.read(reinterpret_cast<char*>(lm.from_.data()), static_cast<std::streamsize>(size * sizeof(float)));
            in.read(reinterpret_cast<char*>(lm.to_.data()), static_cast<std::streamsize>(size * sizeof(float)));
            if (in) {
                std::println("  landmarks loaded from {}", file.string());
                return lm;
            }
        }
        std::println("  {}: outdated or damaged, rebuilding", file.string());
    }

    // 2. Forward and reverse adjacency with the lowest possible edge costs.
    const std::uint32_t n = lm.n_, m = g.num_edges();
    Adj fwd{g.first_out, g.head, std::vector<float>(m)};
    Adj bwd{std::vector<std::uint32_t>(n + 1, 0), std::vector<std::uint32_t>(m), std::vector<float>(m)};
    for (std::uint32_t e = 0; e < m; ++e) {
        fwd.w[e] = static_cast<float>(CostModel::lower_bound(g, e));
    }
    for (std::uint32_t e = 0; e < m; ++e) {
        ++bwd.first[g.head[e] + 1];
    }
    for (std::uint32_t v = 0; v < n; ++v) {
        bwd.first[v + 1] += bwd.first[v];
    }
    {
        std::vector<std::uint32_t> fill(bwd.first.begin(), bwd.first.end() - 1);
        for (std::uint32_t v = 0; v < n; ++v) {
            for (std::uint32_t e = g.first_out[v]; e < g.first_out[v + 1]; ++e) {
                const std::uint32_t k = fill[g.head[e]]++;
                bwd.to[k] = v;
                bwd.w[k] = fwd.w[e];
            }
        }
    }

    // 3. 16 landmarks on the boundary: in each angular sector around the centre
    //    the node farthest away by graph distance.
    LatLon mean{0, 0};
    for (const auto& p : g.node_pos) {
        mean.lat += p.lat;
        mean.lon += p.lon;
    }
    mean = {mean.lat / n, mean.lon / n};
    std::uint32_t center = 0;
    for (std::uint32_t v = 1; v < n; ++v) {
        if (haversine_m(g.node_pos[v], mean) < haversine_m(g.node_pos[center], mean)) {
            center = v;
        }
    }
    const auto dc = all_distances(fwd, center);
    std::vector<std::uint32_t> chosen(kCount, kNoEdge);
    for (std::uint32_t v = 0; v < n; ++v) {
        const LatLon p = g.node_pos[v];
        double angle = std::atan2(p.lat - mean.lat, (p.lon - mean.lon) * std::cos(deg_to_rad(mean.lat)));
        if (angle < 0) {
            angle += 2 * std::numbers::pi;
        }
        const auto sector = std::min(kCount - 1, static_cast<std::uint32_t>(angle / (2 * std::numbers::pi) * kCount));
        if (chosen[sector] == kNoEdge || dc[v] > dc[chosen[sector]]) {
            chosen[sector] = v;
        }
    }

    // 4. One Dijkstra run per landmark on each adjacency, then cache the tables.
    lm.from_.resize(size);
    lm.to_.resize(size);
    for (std::uint32_t l = 0; l < kCount; ++l) {
        const std::uint32_t src = chosen[l] == kNoEdge ? center : chosen[l];
        const auto f = all_distances(fwd, src);
        const auto b = all_distances(bwd, src);
        for (std::uint32_t v = 0; v < n; ++v) {
            lm.from_[lm.at(v, l)] = f[v];
            lm.to_[lm.at(v, l)] = b[v];
        }
    }

    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        const std::uint32_t head[4]{kMagic, kVersion, n, kCount};
        out.write(reinterpret_cast<const char*>(head), sizeof(head));
        out.write(reinterpret_cast<const char*>(&graph_hash), sizeof(graph_hash));
        out.write(reinterpret_cast<const char*>(lm.from_.data()), static_cast<std::streamsize>(size * sizeof(float)));
        out.write(reinterpret_cast<const char*>(lm.to_.data()), static_cast<std::streamsize>(size * sizeof(float)));
    }
    std::filesystem::rename(tmp, file);
    std::println("  landmarks built and saved to {}", file.string());
    return lm;
}

LandmarkQuery Landmarks::prepare(std::uint32_t s, std::uint32_t t) const {
    LandmarkQuery q;
    q.target = t;
    std::vector<std::pair<float, std::uint32_t>> score;
    for (std::uint32_t l = 0; l < count_; ++l) {
        score.push_back({std::max(from_[at(t, l)] - from_[at(s, l)], to_[at(s, l)] - to_[at(t, l)]), l});
    }
    std::ranges::sort(score, std::greater<>{});
    for (std::uint32_t i = 0; i < kActive && i < score.size(); ++i) {
        q.active[q.count++] = score[i].second;
    }
    return q;
}

double Landmarks::bound(const LandmarkQuery& q, std::uint32_t v) const {
    float best = 0;
    for (std::uint32_t i = 0; i < q.count; ++i) {
        const std::uint32_t l = q.active[i];
        best = std::max({best, from_[at(q.target, l)] - from_[at(v, l)], to_[at(v, l)] - to_[at(q.target, l)]});
    }
    // A bit lower, so float rounding never makes the bound too high.
    return best * 0.999;
}

} // namespace rp
