#include "rp/routing/local_search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace rp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::size_t kMaxArchive = 24;

// Graph of the roads in the candidate pool (both directions where allowed).
// Nodes are renumbered 0..n-1, edges keep their global IDs.
struct LocalGraph {
    std::unordered_map<std::uint32_t, std::uint32_t> id;
    std::vector<std::uint32_t> first;
    std::vector<std::uint32_t> tail, head, global;
    std::vector<double> cost, length;

    std::uint32_t n() const {
        return static_cast<std::uint32_t>(first.size()) - 1;
    }
    std::uint32_t degree(std::uint32_t v) const {
        return first[v + 1] - first[v];
    }
};

LocalGraph build_local(const Graph& g, const std::vector<RoundTrip>& pool, const CostModel& cm) {
    std::unordered_set<std::uint32_t> edges;
    for (const auto& t : pool) {
        for (const auto e : t.edges) {
            edges.insert(e);
            if (g.twin[e] != kNoEdge) {
                edges.insert(g.twin[e]);
            }
        }
    }
    LocalGraph lg;
    const auto local = [&](std::uint32_t v) {
        const auto [it, fresh] = lg.id.try_emplace(v, static_cast<std::uint32_t>(lg.id.size()));
        return it->second;
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> list;
    for (const auto e : edges) {
        const auto it = std::upper_bound(g.first_out.begin(), g.first_out.end(), e);
        const auto tail = static_cast<std::uint32_t>(it - g.first_out.begin()) - 1;
        list.push_back({local(tail), e});
        local(g.head[e]);
    }
    std::ranges::sort(list);
    lg.first.assign(lg.id.size() + 1, 0);
    for (const auto& [u, e] : list) {
        ++lg.first[u + 1];
    }
    for (std::size_t v = 0; v < lg.id.size(); ++v) {
        lg.first[v + 1] += lg.first[v];
    }
    for (const auto& [u, e] : list) {
        lg.tail.push_back(u);
        lg.head.push_back(lg.id.at(g.head[e]));
        lg.global.push_back(e);
        lg.cost.push_back(cm.cost(e));
        lg.length.push_back(g.length_m[e]);
    }
    return lg;
}

// Result of local_dijkstra(): cost, length and predecessor edge per node. With
// the length most candidates can be rejected before building their path.
struct LocalSearchTree {
    std::vector<double> dist, length;
    std::vector<std::uint32_t> pred;
};

// Run Dijkstra from src on the local graph, skipping blocked (global) edges.

LocalSearchTree local_dijkstra(const LocalGraph& lg, std::uint32_t src,
                               const std::unordered_set<std::uint32_t>& blocked) {
    LocalSearchTree t{std::vector<double>(lg.n(), kInf), std::vector<double>(lg.n(), 0),
                      std::vector<std::uint32_t>(lg.n(), kNoEdge)};
    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> q;
    t.dist[src] = 0;
    q.push({0, src});
    while (!q.empty()) {
        const auto [d, v] = q.top();
        q.pop();
        if (d > t.dist[v]) {
            continue;
        }
        for (std::uint32_t k = lg.first[v]; k < lg.first[v + 1]; ++k) {
            if (blocked.count(lg.global[k])) {
                continue;
            }
            const std::uint32_t w = lg.head[k];
            if (d + lg.cost[k] < t.dist[w]) {
                t.dist[w] = d + lg.cost[k];
                t.length[w] = t.length[v] + lg.length[k];
                t.pred[w] = k;
                q.push({t.dist[w], w});
            }
        }
    }
    return t;
}

std::vector<std::uint32_t> path_to(const LocalGraph& lg, const LocalSearchTree& t, std::uint32_t src, std::uint32_t w) {
    std::vector<std::uint32_t> path;
    while (w != src) {
        const std::uint32_t k = t.pred[w];
        path.push_back(lg.global[k]);
        w = lg.tail[k];
    }
    std::ranges::reverse(path);
    return path;
}

// The three objectives, rounded: f1 length error (10 m), f2 overlap (1 %),
// f3 cost/km (10 perceived metres). Lower is better.
struct Goals {
    long f1, f2, f3;
};

Goals goals_of(const RoundTrip& t, double target_m) {
    return {std::lround(std::abs(target_m - t.length_m) / 10), std::lround(t.overlap_pct),
            std::lround(t.cost_per_km / 10)};
}

bool dominates(const Goals& a, const Goals& b) {
    return a.f1 <= b.f1 && a.f2 <= b.f2 && a.f3 <= b.f3 && (a.f1 < b.f1 || a.f2 < b.f2 || a.f3 < b.f3);
}
bool same(const Goals& a, const Goals& b) {
    return a.f1 == b.f1 && a.f2 == b.f2 && a.f3 == b.f3;
}

// Nondominated tours (Algorithm 3). visited is set once a tour's neighbourhood was searched.
struct Archive {
    struct Entry {
        RoundTrip trip;
        Goals goals;
        bool visited = false;
        bool from_pool = false;
    };
    std::vector<Entry> entries;

    bool add(RoundTrip t, double target_m, bool from_pool) {
        const Goals g = goals_of(t, target_m);
        for (const auto& e : entries) {
            if (dominates(e.goals, g) || same(e.goals, g)) {
                return false;
            }
        }
        std::erase_if(entries, [&](const Entry& e) {
            return dominates(g, e.goals);
        });
        if (entries.size() >= kMaxArchive) {
            return false;
        }
        entries.push_back({std::move(t), g, false, from_pool});
        return true;
    }
};

} // namespace

std::vector<RoundTrip> local_search(RoundTripPlanner& planner, std::uint32_t start, const std::vector<RoundTrip>& pool,
                                    double target_m, const Sliders& s, double budget_ms) {
    if (pool.empty()) {
        return {};
    }
    const Graph& g = planner.graph();
    const auto t0 = std::chrono::steady_clock::now();
    const auto out_of_time = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budget_ms;
    };

    const CostModel cm(g, s);
    const LocalGraph lg = build_local(g, pool, cm);
    const auto& pois = planner.pois();

    Archive archive;
    for (const auto& t : pool) {
        archive.add(t, target_m, true);
    }

    // A candidate only gets into the archive if it still visits all POIs and its
    // overlap is at most two percentage points above the current tour's.
    // Otherwise out-and-back detours would be a cheap way to gain length.
    double max_overlap = 0;
    bool found_new = false;
    const auto consider = [&](std::vector<std::uint32_t> edges) {
        for (const auto p : pois) {
            if (std::ranges::none_of(edges, [&](std::uint32_t e) {
                    return g.head[e] == p;
                })) {
                return;
            }
        }
        RoundTrip t = planner.evaluate(std::move(edges), s);
        if (t.overlap_pct > max_overlap) {
            return;
        }
        if (archive.add(std::move(t), target_m, false)) {
            found_new = true;
        }
    };

    // Algorithm 3: search the neighbourhood of every unvisited tour. If three
    // tours in a row bring nothing new, we stop early.
    constexpr int kMaxIdle = 3;
    int idle = 0;
    bool started = false;
    while (!out_of_time()) {
        if (started) {
            idle = found_new ? 0 : idle + 1;
        }
        if (idle >= kMaxIdle) {
            break;
        }
        started = true;
        found_new = false;
        const auto it = std::ranges::find_if(archive.entries, [](const auto& e) {
            return !e.visited;
        });
        if (it == archive.entries.end()) {
            break;
        }
        it->visited = true;
        const std::vector<std::uint32_t> S = it->trip.edges; // copy: archive changes next
        const double L = it->trip.length_m;
        // Neighbours may miss k by as much as S does, or by 5 % if S is closer.
        const double limit = std::max(std::abs(L - target_m), 0.05 * target_m);
        max_overlap = it->trip.overlap_pct + 2.0;

        // Build local node sequence u_0..u_l and prefix lengths;
        // edge S[k] leads from u_k to u_{k+1}.
        std::vector<std::uint32_t> u{lg.id.at(start)};
        std::vector<double> cum{0};
        for (const auto e : S) {
            u.push_back(lg.id.at(g.head[e]));
            cum.push_back(cum.back() + g.length_m[e]);
        }
        // The tour's roads are blocked in both directions, otherwise a
        // replacement path could just ride back along the tour.
        std::unordered_set<std::uint32_t> blocked;
        for (const auto e : S) {
            blocked.insert(e);
            blocked.insert(g.twin[e]); // kNoEdge for one-way roads, doesn't matter
        }

        for (std::size_t i = 0; i + 1 < u.size() && !out_of_time(); ++i) {
            if (lg.degree(u[i]) < 3) {
                continue;
            }
            const auto tree = local_dijkstra(lg, u[i], blocked);

            // Replace a section: path from u_i to a later tour node u_j. The
            // length check comes first, most candidates already fail there.
            for (std::size_t j = i + 1; j < u.size(); ++j) {
                if (u[j] == u[i] || tree.dist[u[j]] == kInf) {
                    continue;
                }
                const double len = L - (cum[j] - cum[i]) + tree.length[u[j]];
                if (std::abs(len - target_m) > limit) {
                    continue;
                }
                const auto path = path_to(lg, tree, u[i], u[j]);
                std::vector<std::uint32_t> cand(S.begin(), S.begin() + static_cast<std::ptrdiff_t>(i));
                cand.insert(cand.end(), path.begin(), path.end());
                cand.insert(cand.end(), S.begin() + static_cast<std::ptrdiff_t>(j), S.end());
                consider(std::move(cand));
            }

            // Insert a sub-loop: path from u_i to a node with an edge back to u_i.
            for (std::uint32_t k = 0; k < lg.head.size(); ++k) {
                const std::uint32_t tail = lg.tail[k];
                if (lg.head[k] != u[i] || blocked.count(lg.global[k]) || tail == u[i] || tree.dist[tail] == kInf) {
                    continue;
                }
                const double loop_m = tree.length[tail] + lg.length[k];
                if (std::abs(L + loop_m - target_m) > limit) {
                    continue;
                }
                auto loop = path_to(lg, tree, u[i], tail);
                loop.push_back(lg.global[k]);
                if (loop.size() == 2 && g.twin[loop[0]] == loop[1]) {
                    continue; // retracing the same edge is not a loop
                }
                std::vector<std::uint32_t> cand(S.begin(), S.begin() + static_cast<std::ptrdiff_t>(i));
                cand.insert(cand.end(), loop.begin(), loop.end());
                cand.insert(cand.end(), S.begin() + static_cast<std::ptrdiff_t>(i), S.end());
                consider(std::move(cand));
            }
        }
    }

    std::vector<RoundTrip> result;
    for (auto& e : archive.entries) {
        if (!e.from_pool) {
            result.push_back(std::move(e.trip));
        }
    }
    return result;
}

} // namespace rp
