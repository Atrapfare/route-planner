#include "rp/eval/measure.hpp"

#include "rp/routing/dijkstra.hpp"
#include "rp/routing/isochrone_method.hpp"
#include "rp/routing/local_search.hpp"
#include "rp/routing/polygon_method.hpp"
#include "rp/routing/round_trip.hpp"
#include "rp/routing/spatial_index.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace rp {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

constexpr int kVariants = 3;
constexpr double kLengthsKm[] = {10, 25, 50};
constexpr double kDetailKm = 25;
constexpr double kSweepLevels[] = {0, 25, 75, 100}; // neutral 50 is tested once per group
constexpr double kMaxPlaceSnapM = 1000.0;
constexpr double kMaxRandomSnapM = 200.0;

// Five fixed places in Baden-Württemberg, skipped if not in the loaded graph.
struct NamedPlace {
    const char* name;
    LatLon pos;
};
constexpr NamedPlace kPlaces[] = {{"Stuttgart", {48.7784, 9.18}},
                                  {"Tuebingen", {48.52, 9.056}},
                                  {"Freiburg", {47.995, 7.85}},
                                  {"Konstanz", {47.66, 9.175}},
                                  {"Titisee", {47.90, 8.15}}};

struct Start {
    std::string name;
    LatLon pos;
    std::uint32_t node;
    double snap_m;
};

struct Profile {
    std::string name;
    Sliders s;
    bool key;
};

// Slider mechanisms toggled for ablation:
//   Cost model: slider-dependent edge costs (routing/cost_model)
//   Corner bias: move method corners to better nodes (pick_corners)
//   Selection: choose() ranks routes by slider-metric fit
enum class Mechanism {
    Full,
    NoCorner,
    NoChoose,
    CostOnly,
    ChooseOnly,
};

const char* name_of(Mechanism m) {
    switch (m) {
        case Mechanism::Full:
            return "full";
        case Mechanism::NoCorner:
            return "no_corner";
        case Mechanism::NoChoose:
            return "no_choose";
        case Mechanism::CostOnly:
            return "cost_only";
        case Mechanism::ChooseOnly:
            return "choose_only";
    }
    return "?";
}

// Group by method and local-search setting; run neutral reference first,
// then profiles with different enabled mechanisms.
struct Group {
    bool polygon;
    bool ls;
    struct Part {
        const char* phase;
        Mechanism mech;
        bool all_profiles;
    };
    std::vector<Part> parts;
};

std::vector<Profile> make_profiles() {
    std::vector<Profile> p{{"neutral", Sliders{}, true}};
    const std::pair<const char*, double Sliders::*> axes[] = {{"hilly", &Sliders::hilly},
                                                              {"bike", &Sliders::bike},
                                                              {"nature", &Sliders::nature},
                                                              {"trail", &Sliders::trail},
                                                              {"curvy", &Sliders::curvy}};
    for (const auto& [name, member] : axes) {
        for (const double v : kSweepLevels) {
            Sliders s;
            s.*member = v;
            p.push_back({std::format("{}={:.0f}", name, v), s, v == 0 || v == 100});
        }
    }
    p.push_back({"flat+bike", Sliders{.hilly = 0, .bike = 100}, true});
    p.push_back({"nature+trail", Sliders{.nature = 100, .trail = 100}, true});
    p.push_back({"hilly+curvy", Sliders{.hilly = 100, .curvy = 100}, true});
    return p;
}

std::vector<Group> groups_for(double km) {
    std::vector<Group> groups{{false, true, {{"sweep", Mechanism::Full, true}}}};
    if (km == kDetailKm) {
        for (const auto m : {Mechanism::NoCorner, Mechanism::NoChoose, Mechanism::CostOnly, Mechanism::ChooseOnly}) {
            groups[0].parts.push_back({"ablation", m, false});
        }
        groups.push_back({true, true, {{"methods", Mechanism::Full, false}}});
        groups.push_back({false, false, {{"methods", Mechanism::Full, false}}});
        groups.push_back({true, false, {{"methods", Mechanism::Full, false}}});
    }
    return groups;
}

// Fixed places plus seeded random points in the graph bounds. Points too far
// from a road are drawn again.
std::vector<Start> make_starts(const Graph& g, const SpatialIndex& index, const MeasureOptions& opt) {
    std::vector<Start> starts;
    std::unordered_set<std::uint32_t> used;
    for (const auto& p : kPlaces) {
        const std::uint32_t v = index.nearest(p.pos);
        const double d = v == kNoEdge ? 1e18 : haversine_m(p.pos, g.node_pos[v]);
        if (d > kMaxPlaceSnapM) {
            std::println(stderr, "  skipped {}: not on this graph", p.name);
            continue;
        }
        starts.push_back({p.name, p.pos, v, d});
        used.insert(v);
    }

    LatLon lo = g.node_pos[0], hi = g.node_pos[0];
    for (const auto& q : g.node_pos) {
        lo = {std::min(lo.lat, q.lat), std::min(lo.lon, q.lon)};
        hi = {std::max(hi.lat, q.lat), std::max(hi.lon, q.lon)};
    }
    std::uint64_t seed = opt.seed;
    const auto uniform = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(seed >> 11) / static_cast<double>(1ull << 53);
    };
    std::size_t found = 0;
    for (std::size_t tries = 0; found < opt.random_starts && tries < 1000 * (opt.random_starts + 1); ++tries) {
        const LatLon q{lo.lat + uniform() * (hi.lat - lo.lat), lo.lon + uniform() * (hi.lon - lo.lon)};
        const std::uint32_t v = index.nearest(q);
        if (v == kNoEdge || used.count(v)) {
            continue;
        }
        const double d = haversine_m(q, g.node_pos[v]);
        if (d > kMaxRandomSnapM) {
            continue;
        }
        used.insert(v);
        starts.push_back({std::format("r{:02}", ++found), q, v, d});
    }
    if (found < opt.random_starts) {
        std::println(stderr, "  only {} of {} random start points found", found, opt.random_starts);
    }
    return starts;
}

struct Outcome {
    std::vector<RoundTrip> trips;
    std::size_t pool = 0, archive = 0;
    double method_ms = 0, ls_ms = 0, choose_ms = 0;
};

// Same as a server request (without POIs), but with switchable mechanisms.
Outcome run_request(Workers& workers, const Graph& g, std::uint32_t start, double km, bool polygon, bool ls,
                    Mechanism mech, const Sliders& s) {
    const Sliders neutral;
    const bool corner_bias = mech != Mechanism::NoCorner && mech != Mechanism::CostOnly;
    const Sliders& route_s = mech == Mechanism::ChooseOnly ? neutral : s;
    const Sliders& choose_s = mech == Mechanism::NoChoose || mech == Mechanism::CostOnly ? neutral : s;
    const double target = km * 1000;
    RoundTripPlanner& planner = workers.main();

    Outcome o;
    workers.set_corner_bias(corner_bias);
    auto t0 = Clock::now();
    auto pool = polygon ? polygon_method(workers.planners, start, target, kVariants, route_s)
                        : isochrone_method(workers.planners, start, target, kVariants, route_s);
    o.method_ms = ms_since(t0);
    if (ls) {
        t0 = Clock::now();
        auto better = local_search(planner, start, pool, target, route_s);
        o.ls_ms = ms_since(t0);
        o.archive = better.size();
        for (auto& t : better) {
            pool.push_back(std::move(t));
        }
    }
    workers.set_corner_bias(true);

    t0 = Clock::now();
    if (mech == Mechanism::ChooseOnly) {
        // Recompute cost/km of the neutral candidates with the profile's
        // sliders, so the selection sees the same values as in the app.
        for (auto& t : pool) {
            t = planner.evaluate(std::move(t.edges), s);
        }
    }
    o.pool = pool.size();
    o.trips = choose(g, std::move(pool), target, kVariants, choose_s);
    o.choose_ms = ms_since(t0);
    return o;
}

std::string json_escape(const std::string& in) {
    std::string out;
    for (const char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

std::string compiler_name() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return std::format("msvc {}", _MSC_VER);
#else
    return "unknown";
#endif
}

std::string utc_now() {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}",
                       std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

} // namespace

void run_measurement(const Graph& g, const Landmarks* landmarks, const MeasureOptions& opt) {
    std::filesystem::create_directories(opt.out_dir);
    const auto open = [&](const char* file) {
        std::ofstream f(opt.out_dir / file);
        if (!f) {
            throw std::runtime_error("cannot write " + (opt.out_dir / file).string());
        }
        return f;
    };
    std::ofstream requests = open("requests.csv"), routes = open("routes.csv");
    requests << "req,phase,start,start_name,lat,lon,snap_m,km,method,ls,mechanism,profile,"
                "hilly,bike,nature,trail,curvy,pool,archive,routes,missed,max_pair_shared_pct,"
                "method_ms,ls_ms,choose_ms,total_ms\n";
    routes << "req,rank,length_m,length_err_pct,within_tol,ascent_m,ascent_per_km,bike_pct,nature_pct,"
              "unpaved_pct,curvy_deg_km,overlap_pct,cost_per_km,shared_neutral_pct\n";

    const SpatialIndex index(g);
    Workers workers(g, index, landmarks, opt.threads);
    const auto profiles = make_profiles();
    const auto starts = make_starts(g, index, opt);
    if (starts.empty()) {
        throw std::runtime_error("no start point lies on this graph (see eval/measure.cpp)");
    }
    std::println("  {} threads, {} start points, {} profiles", workers.planners.size(), starts.size(), profiles.size());

    const std::string started = utc_now();
    const auto t_all = Clock::now();
    std::size_t req = 0;
    for (std::size_t si = 0; si < starts.size(); ++si) {
        const Start& st = starts[si];
        for (const double km : kLengthsKm) {
            for (const Group& grp : groups_for(km)) {
                std::vector<RoundTrip> neutral;

                const auto measure = [&](const char* phase, Mechanism mech, const Profile& prof) {
                    const Outcome o = run_request(workers, g, st.node, km, grp.polygon, grp.ls, mech, prof.s);
                    ++req;
                    double max_pair = 0;
                    for (std::size_t a = 0; a < o.trips.size(); ++a) {
                        for (std::size_t b = a + 1; b < o.trips.size(); ++b) {
                            max_pair = std::max(max_pair, shared(g, o.trips[a], o.trips[b]));
                        }
                    }
                    const auto missed = std::ranges::count_if(o.trips, [](const RoundTrip& t) {
                        return !t.within_tolerance;
                    });
                    const Sliders& s = prof.s;
                    requests << std::format(
                        "{},{},{},{},{:.6f},{:.6f},{:.0f},{},{},{},{},{},{:.0f},{:.0f},{:.0f},{:.0f},"
                        "{:.0f},{},{},{},{},{},{:.1f},{:.1f},{:.1f},{:.1f}\n",
                        req, phase, si, st.name, st.pos.lat, st.pos.lon, st.snap_m, km,
                        grp.polygon ? "polygon" : "isochrone", grp.ls ? 1 : 0, name_of(mech), prof.name, s.hilly,
                        s.bike, s.nature, s.trail, s.curvy, o.pool, o.archive, o.trips.size(), missed,
                        o.trips.size() > 1 ? std::format("{:.1f}", 100 * max_pair) : "", o.method_ms, o.ls_ms,
                        o.choose_ms, o.method_ms + o.ls_ms + o.choose_ms);
                    for (std::size_t r = 0; r < o.trips.size(); ++r) {
                        const RoundTrip& t = o.trips[r];
                        std::string vs_neutral;
                        if (&prof != &profiles[0] && !neutral.empty()) {
                            double best = 0;
                            for (const auto& n : neutral) {
                                best = std::max(best, shared(g, t, n));
                            }
                            vs_neutral = std::format("{:.1f}", 100 * best);
                        }
                        const double target = km * 1000;
                        routes << std::format(
                            "{},{},{:.0f},{:.2f},{},{:.0f},{:.2f},{:.1f},{:.1f},{:.1f},{:.1f},{:.2f},"
                            "{:.2f},{}\n",
                            req, r, t.length_m, 100 * (t.length_m - target) / target, t.within_tolerance ? 1 : 0,
                            t.ascent_m, t.length_m > 0 ? t.ascent_m / (t.length_m / 1000) : 0.0, t.bike_pct,
                            t.nature_pct, t.unpaved_pct, t.curvy_deg_km, t.overlap_pct, t.cost_per_km, vs_neutral);
                    }
                    return o.trips;
                };

                // Neutral only once per group, the mechanisms change nothing there.
                neutral = measure(grp.parts[0].phase, Mechanism::Full, profiles[0]);
                for (const auto& part : grp.parts) {
                    for (std::size_t p = 1; p < profiles.size(); ++p) {
                        if (part.all_profiles || profiles[p].key) {
                            measure(part.phase, part.mech, profiles[p]);
                        }
                    }
                }
            }
        }
        requests.flush();
        routes.flush();
        const double elapsed = ms_since(t_all) / 1000;
        const double eta = elapsed / static_cast<double>(si + 1) * static_cast<double>(starts.size() - si - 1);
        std::println("  [{}/{}] {} done: {} requests, {:.0f} s elapsed, about {:.0f} min left", si + 1, starts.size(),
                     st.name, req, elapsed, eta / 60);
        std::fflush(stdout); // flush progress when output is redirected
    }

    std::ofstream meta = open("meta.json");
    std::string lengths;
    for (const double km : kLengthsKm) {
        lengths += (lengths.empty() ? "" : ", ") + std::format("{:.0f}", km);
    }
#ifdef NDEBUG
    const char* build = "release";
#else
    const char* build = "debug";
#endif
    meta << std::format(R"({{
  "graph": "{}",
  "nodes": {},
  "edges": {},
  "alt": {},
  "threads": {},
  "hardware_threads": {},
  "local_search_budget_ms": {:.0f},
  "variants": {},
  "lengths_km": [{}],
  "detail_km": {:.0f},
  "seed": {},
  "random_starts_requested": {},
  "start_points": {},
  "requests": {},
  "compiler": "{}",
  "build": "{}",
  "started_utc": "{}",
  "finished_utc": "{}",
  "duration_s": {:.0f}
}}
)",
                        json_escape(opt.graph_name), g.num_nodes(), g.num_edges(), landmarks ? "true" : "false",
                        workers.planners.size(), std::thread::hardware_concurrency(), kLocalSearchBudgetMs, kVariants,
                        lengths, kDetailKm, opt.seed, opt.random_starts, starts.size(), req,
                        json_escape(compiler_name()), build, started, utc_now(), ms_since(t_all) / 1000);
    std::println("  wrote {} requests to {}", req, opt.out_dir.string());
}

void run_alt_benchmark(const Graph& g, Landmarks& landmarks) {
    Dijkstra plain(g), alt(g);
    alt.set_landmarks(&landmarks);

    std::uint64_t seed = 12345;
    const auto rnd = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return seed >> 33;
    };
    const Sliders profiles[] = {Sliders{}, Sliders{0, 50, 50, 50, 50}, Sliders{100, 100, 100, 100, 100},
                                Sliders{50, 0, 0, 0, 0}};

    for (const double max_km : {5.0, 1000.0}) { // short route legs and unrestricted pairs
        double t_plain = 0, t_alt = 0;
        std::uint64_t s_plain = 0, s_alt = 0;
        int pairs = 0;
        while (pairs < 200) {
            const auto s = static_cast<std::uint32_t>(rnd() % g.num_nodes());
            const auto t = static_cast<std::uint32_t>(rnd() % g.num_nodes());
            if (s == t || haversine_m(g.node_pos[s], g.node_pos[t]) > max_km * 1000) {
                continue;
            }
            const CostModel cost(g, profiles[pairs % 4]);

            auto t0 = std::chrono::steady_clock::now();
            const auto a = plain.shortest_path(s, t, cost);
            t_plain += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            s_plain += plain.settled();

            t0 = std::chrono::steady_clock::now();
            const auto b = alt.shortest_path(s, t, cost);
            t_alt += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            s_alt += alt.settled();

            // ALT must return exactly the same cost.
            if (a.has_value() != b.has_value() || (a && std::abs(a->cost - b->cost) > 1e-6 * a->cost + 1e-3)) {
                throw std::runtime_error(std::format("ALT differs from Dijkstra: {} -> {} ({} vs {})", s, t,
                                                     a ? a->cost : -1, b ? b->cost : -1));
            }
            ++pairs;
        }
        std::println("  {} pairs (<= {} km): Dijkstra {:.1f} ms / {} settled, ALT {:.1f} ms / {} settled (x{:.1f})",
                     pairs, max_km, t_plain / pairs, s_plain / pairs, t_alt / pairs, s_alt / pairs,
                     t_plain / std::max(t_alt, 1e-9));
    }
    std::println("  ALT costs identical to Dijkstra for all pairs");
}

} // namespace rp
