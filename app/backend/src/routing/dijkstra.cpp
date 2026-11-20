#include "rp/routing/dijkstra.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>

namespace rp {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
} // namespace

Dijkstra::Dijkstra(const Graph& g)
    : g_(g),
      dist_(g.num_nodes(), kInf),
      pred_(g.num_nodes(), kNoEdge) {
}

void Dijkstra::reset() {
    for (const std::uint32_t v : touched_) {
        dist_[v] = kInf;
        pred_[v] = kNoEdge;
    }
    touched_.clear();
}

std::optional<Path> Dijkstra::shortest_path(std::uint32_t source, std::uint32_t target, const CostModel& cost) {
    reset();
    settled_ = 0;

    // A*: the queue is ordered by d + π(v), with π(v) the ALT bound on the
    // remaining cost. Without landmarks π = 0, which is plain Dijkstra.
    const LandmarkQuery lq = landmarks_ ? landmarks_->prepare(source, target) : LandmarkQuery{};
    const auto pi = [&](std::uint32_t v) {
        return landmarks_ ? landmarks_->bound(lq, v) : 0.0;
    };

    using Entry = std::tuple<double, double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    dist_[source] = 0;
    touched_.push_back(source);
    queue.push({pi(source), 0, source});

    while (!queue.empty()) {
        const auto [key, d, v] = queue.top();
        queue.pop();
        if (d > dist_[v]) {
            continue;
        }
        ++settled_;
        if (v == target) {
            break;
        }

        for (std::uint32_t e = g_.first_out[v]; e < g_.first_out[v + 1]; ++e) {
            const std::uint32_t w = g_.head[e];
            const double nd = d + cost.cost(e);
            if (nd < dist_[w]) {
                if (dist_[w] == kInf) {
                    touched_.push_back(w);
                }
                dist_[w] = nd;
                pred_[w] = e;
                queue.push({nd + pi(w), nd, w});
            }
        }
    }

    if (dist_[target] == kInf) {
        return std::nullopt;
    }

    Path path;
    path.cost = dist_[target];
    for (std::uint32_t v = target; v != source;) {
        const std::uint32_t e = pred_[v];
        path.edges.push_back(e);
        path.length_m += g_.length_m[e];
        // Tail of e: the u with first_out[u] <= e < first_out[u+1].
        const auto it = std::upper_bound(g_.first_out.begin(), g_.first_out.end(), e);
        v = static_cast<std::uint32_t>(it - g_.first_out.begin()) - 1;
    }
    std::ranges::reverse(path.edges);
    return path;
}

std::vector<std::pair<std::uint32_t, double>> Dijkstra::within(std::uint32_t source, double max_m) {
    reset();
    settled_ = 0;

    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    std::vector<std::pair<std::uint32_t, double>> reached;
    dist_[source] = 0;
    touched_.push_back(source);
    queue.push({0, source});

    while (!queue.empty()) {
        const auto [d, v] = queue.top();
        queue.pop();
        if (d > dist_[v]) {
            continue;
        }
        ++settled_;
        reached.push_back({v, d});
        for (std::uint32_t e = g_.first_out[v]; e < g_.first_out[v + 1]; ++e) {
            const std::uint32_t w = g_.head[e];
            const double nd = d + g_.length_m[e];
            if (nd <= max_m && nd < dist_[w]) {
                if (dist_[w] == kInf) {
                    touched_.push_back(w);
                }
                dist_[w] = nd;
                pred_[w] = e;
                queue.push({nd, w});
            }
        }
    }
    return reached;
}

} // namespace rp
