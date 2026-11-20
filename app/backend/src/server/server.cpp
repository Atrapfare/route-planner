#include "rp/server/server.hpp"

#include "rp/routing/cost_model.hpp"
#include "rp/routing/dijkstra.hpp"
#include "rp/routing/isochrone_method.hpp"
#include "rp/routing/local_search.hpp"
#include "rp/routing/polygon_method.hpp"
#include "rp/routing/round_trip.hpp"
#include "rp/routing/spatial_index.hpp"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <string>

namespace rp {

namespace {

// Clicks more than 1 km from the nearest node count as outside the map.
constexpr double kMaxSnapM = 1000.0;

// Numeric URL parameter, nullopt if missing or invalid. stod accepts nan and
// inf, so those are rejected too.
std::optional<double> number(const httplib::Request& req, const char* name) {
    if (!req.has_param(name)) {
        return std::nullopt;
    }
    try {
        const double v = std::stod(req.get_param_value(name));
        return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

void error(httplib::Response& res, int status, const std::string& msg) {
    res.status = status;
    res.set_content(std::format(R"({{"error":"{}"}})", msg), "application/json");
}

// Lowercase and ä/ö/ü/ß -> a/o/u/ss for the search. In UTF-8 these are two
// bytes, 0xC3 and a second one.
std::string normalize(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            switch (static_cast<unsigned char>(s[++i])) {
                case 0xA4:
                case 0x84:
                    out += 'a';
                    break; // ä Ä
                case 0xB6:
                case 0x96:
                    out += 'o';
                    break; // ö Ö
                case 0xBC:
                case 0x9C:
                    out += 'u';
                    break; // ü Ü
                case 0x9F:
                    out += "ss";
                    break; // ß
                default:
                    out += s[i - 1];
                    out += s[i];
            }
        } else {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

std::string json_escape(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out;
}

std::string point_json(LatLon p) {
    return std::format("[{:.6f},{:.6f}]", p.lat, p.lon);
}

std::string path_point_json(LatLon p, float elevation_m) {
    return std::format("[{:.6f},{:.6f},{:.1f}]", p.lat, p.lon, elevation_m);
}

// Points for map, profile and GPX: start node, then per edge its geo points and end node.
std::string path_json(const Graph& g, std::uint32_t start, const std::vector<std::uint32_t>& edges) {
    std::string coords = path_point_json(g.node_pos[start], g.node_elevation_m[start]);
    for (const auto e : edges) {
        for (std::uint32_t i = g.geo_first[e]; i < g.geo_first[e + 1]; ++i) {
            coords += "," + path_point_json(g.geo_points[i], g.geo_elevation_m[i]);
        }
        coords += "," + path_point_json(g.node_pos[g.head[e]], g.node_elevation_m[g.head[e]]);
    }
    return "[" + coords + "]";
}

// Sliders from the URL, clamped to 0..100, 50 if missing.
Sliders read_sliders(const httplib::Request& req) {
    Sliders s;
    const auto slider = [&](const char* name, double& value) {
        if (const auto v = number(req, name)) {
            value = std::clamp(*v, 0.0, 100.0);
        }
    };
    slider("hilly", s.hilly);
    slider("bike", s.bike);
    slider("nature", s.nature);
    slider("trail", s.trail);
    slider("curvy", s.curvy);
    return s;
}

} // namespace

bool run_server(const Graph& g, const Landmarks* landmarks, std::size_t threads, const std::string& host, int port,
                const std::function<void()>& on_ready) {
    const SpatialIndex index(g);
    // workers.main() does everything single-threaded: A-to-B routes, the
    // isochrone and the local search.
    Workers workers(g, index, landmarks, threads);
    RoundTripPlanner& planner = workers.main();
    Dijkstra& dijkstra = planner.dijkstra();
    std::println("Using {} threads", workers.planners.size());
    // One request at a time: the Dijkstras have state, and one request already uses all cores.
    std::mutex search_mutex;
    std::vector<std::string> poi_search_names(g.num_pois());
    for (std::uint32_t i = 0; i < g.num_pois(); ++i) {
        poi_search_names[i] = normalize(g.poi_name(i));
    }

    httplib::Server server;
#ifndef _WIN32
    // httplib sets SO_REUSEPORT on Linux/macOS, so a second backend could quietly
    // share the port. With only SO_REUSEADDR an occupied port fails at startup.
    server.set_socket_options([](socket_t sock) {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    });
#endif

    const auto snap = [&](LatLon p, httplib::Response& res) -> std::optional<std::uint32_t> {
        const std::uint32_t v = index.nearest(p);
        if (v == kNoEdge || haversine_m(p, g.node_pos[v]) > kMaxSnapM) {
            error(res, 400, "point is outside the map");
            return std::nullopt;
        }
        return v;
    };

    server.Get("/api/nearest", [&](const httplib::Request& req, httplib::Response& res) {
        const auto lat = number(req, "lat"), lon = number(req, "lon");
        if (!lat || !lon) {
            return error(res, 400, "lat and lon required");
        }
        const auto v = snap({*lat, *lon}, res);
        if (!v) {
            return;
        }
        res.set_content(std::format(R"({{"node":{},"pos":{}}})", *v, point_json(g.node_pos[*v])), "application/json");
    });

    LatLon lo{0, 0}, hi{0, 0};
    if (g.num_nodes() > 0) {
        lo = hi = g.node_pos.front();
    }
    for (const auto& p : g.node_pos) {
        lo = {std::min(lo.lat, p.lat), std::min(lo.lon, p.lon)};
        hi = {std::max(hi.lat, p.lat), std::max(hi.lon, p.lon)};
    }
    const std::string bounds_json = std::format(R"({{"min":{},"max":{}}})", point_json(lo), point_json(hi));
    server.Get("/api/bounds", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(bounds_json, "application/json");
    });

    server.Get("/api/route", [&](const httplib::Request& req, httplib::Response& res) {
        const auto flat = number(req, "from_lat"), flon = number(req, "from_lon");
        const auto tlat = number(req, "to_lat"), tlon = number(req, "to_lon");
        if (!flat || !flon || !tlat || !tlon) {
            return error(res, 400, "from_lat, from_lon, to_lat, to_lon required");
        }

        const Sliders s = read_sliders(req);

        const auto src = snap({*flat, *flon}, res);
        if (!src) {
            return;
        }
        const auto trg = snap({*tlat, *tlon}, res);
        if (!trg) {
            return;
        }

        const auto t0 = std::chrono::steady_clock::now();
        std::optional<Path> path;
        {
            const std::lock_guard lock(search_mutex);
            path = dijkstra.shortest_path(*src, *trg, CostModel(g, s));
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!path) {
            return error(res, 422, "target not reachable");
        }

        // Same 5 m threshold as for round trips.
        const Climb climb = climb_along(g, *src, path->edges);
        res.set_content(
            std::format(R"({{"length_m":{:.1f},"ascent_m":{:.1f},"descent_m":{:.1f},"time_ms":{:.1f},"path":{}}})",
                        path->length_m, climb.ascent_m, climb.descent_m, ms, path_json(g, *src, path->edges)),
            "application/json");
    });

    server.Get("/api/round-route", [&](const httplib::Request& req, httplib::Response& res) {
        const auto lat = number(req, "lat"), lon = number(req, "lon"), km = number(req, "length_km");
        if (!lat || !lon || !km) {
            return error(res, 400, "lat, lon, length_km required");
        }
        if (*km < 1 || *km > 200) {
            return error(res, 400, "length_km must be between 1 and 200");
        }
        const int variants = static_cast<int>(std::clamp(number(req, "variants").value_or(3), 1.0, 12.0));
        const std::string method = req.has_param("method") ? req.get_param_value("method") : "isochrone";
        if (method != "polygon" && method != "isochrone") {
            return error(res, 400, "method must be polygon or isochrone");
        }
        const Sliders s = read_sliders(req);
        const auto start = snap({*lat, *lon}, res);
        if (!start) {
            return;
        }

        // Required POIs: comma-separated IDs from /api/pois, at most 10.
        std::vector<std::uint32_t> poi_nodes;
        if (req.has_param("poi") && !req.get_param_value("poi").empty()) {
            std::string list = req.get_param_value("poi") + ",";
            for (std::size_t a = 0, b; (b = list.find(',', a)) != std::string::npos; a = b + 1) {
                std::uint32_t id = 0;
                try {
                    id = static_cast<std::uint32_t>(std::stoul(list.substr(a, b - a)));
                } catch (...) {
                    return error(res, 400, "invalid poi id");
                }
                if (id >= g.num_pois()) {
                    return error(res, 400, "invalid poi id");
                }
                const std::uint32_t v = g.poi_node[id];
                if (v != *start && std::ranges::find(poi_nodes, v) == poi_nodes.end()) {
                    poi_nodes.push_back(v);
                }
            }
        }
        // Required map points: lat,lon pairs separated by ';', snapped to the network.
        if (req.has_param("via") && !req.get_param_value("via").empty()) {
            std::string list = req.get_param_value("via") + ";";
            for (std::size_t a = 0, b; (b = list.find(';', a)) != std::string::npos; a = b + 1) {
                const std::string pair = list.substr(a, b - a);
                const std::size_t comma = pair.find(',');
                double vlat = 0, vlon = 0;
                try {
                    vlat = std::stod(pair.substr(0, comma));
                    vlon = std::stod(pair.substr(comma + 1));
                } catch (...) {
                    return error(res, 400, "invalid via point");
                }
                if (comma == std::string::npos || !std::isfinite(vlat) || !std::isfinite(vlon)) {
                    return error(res, 400, "invalid via point");
                }
                const std::uint32_t v = index.nearest({vlat, vlon});
                if (v == kNoEdge || haversine_m({vlat, vlon}, g.node_pos[v]) > kMaxSnapM) {
                    return error(res, 400, "via point is outside the map");
                }
                if (v != *start && std::ranges::find(poi_nodes, v) == poi_nodes.end()) {
                    poi_nodes.push_back(v);
                }
            }
        }
        if (poi_nodes.size() > 10) {
            return error(res, 400, "at most 10 pois and via points");
        }
        const double target = *km * 1000;

        const auto t0 = std::chrono::steady_clock::now();
        std::vector<RoundTrip> trips;
        bool poi_tour = false;
        const bool use_ls = !req.has_param("ls") || req.get_param_value("ls") != "0";
        std::size_t archive_size = 0;
        {
            const std::lock_guard lock(search_mutex);
            workers.set_pois(poi_nodes);
            // Direct tour start -> POIs -> start without extra corners. If it is
            // already more than 5 % too long, it is returned alone. The stop
            // order is only a heuristic, so this is no real lower bound.
            std::optional<RoundTrip> direct;
            if (!poi_nodes.empty()) {
                direct = planner.build(*start, {}, s);
                poi_tour = direct && direct->length_m > 1.05 * target;
            }
            if (!poi_tour) {
                if (method == "polygon") {
                    trips = polygon_method(workers.planners, *start, target, variants, s);
                } else {
                    trips = isochrone_method(workers.planners, *start, target, variants, s);
                }
            }
            // The direct tour is always a candidate. If its length already fits,
            // more corners would only make it longer.
            if (direct) {
                trips.push_back(std::move(*direct));
            }
            // Local search runs before the POIs are cleared, it has to keep the stops.
            if (!poi_tour && use_ls) {
                auto better = local_search(planner, *start, trips, target, s);
                archive_size = better.size();
                for (auto& t : better) {
                    trips.push_back(std::move(t));
                }
            }
            workers.set_pois({});
        }
        const std::size_t pool_size = trips.size();
        trips = choose(g, std::move(trips), *km * 1000, variants, s);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (trips.empty()) {
            return error(res, 422, "no round trip found");
        }

        std::string routes;
        for (const auto& t : trips) {
            if (!routes.empty()) {
                routes += ",";
            }
            routes += std::format(
                R"({{"length_m":{:.1f},"ascent_m":{:.1f},"overlap_pct":{:.1f},"bike_pct":{:.1f},"nature_pct":{:.1f},"unpaved_pct":{:.1f},"curvy_deg_km":{:.0f},"cost_per_km":{:.1f},"within_tolerance":{},"path":{}}})",
                t.length_m, t.ascent_m, t.overlap_pct, t.bike_pct, t.nature_pct, t.unpaved_pct, t.curvy_deg_km,
                t.cost_per_km, t.within_tolerance, path_json(g, *start, t.edges));
        }
        res.set_content(
            std::format(R"({{"time_ms":{:.1f},"pool_size":{},"archive_size":{},"poi_tour":{},"routes":[{}]}})", ms,
                        pool_size, archive_size, poi_tour, routes),
            "application/json");
    });

    server.Get("/api/pois", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string q = normalize(req.has_param("q") ? req.get_param_value("q") : "");
        if (q.empty()) {
            return error(res, 400, "q required");
        }
        const auto lat = number(req, "lat"), lon = number(req, "lon");
        const auto limit = static_cast<std::size_t>(std::clamp(number(req, "limit").value_or(10), 1.0, 50.0));

        // Rank: exact, prefix, word prefix, then substring.
        struct Hit {
            int rank;
            double dist;
            std::uint32_t poi;
        };
        std::vector<Hit> hits;
        for (std::uint32_t i = 0; i < g.num_pois(); ++i) {
            const std::string& name = poi_search_names[i];
            const auto at = name.find(q);
            if (at == std::string::npos) {
                continue;
            }
            int rank = 3;
            if (name == q) {
                rank = 0;
            } else if (at == 0) {
                rank = 1;
            } else if (name.find(" " + q) != std::string::npos || name.find("-" + q) != std::string::npos) {
                rank = 2;
            }
            const double dist = lat && lon ? haversine_m({*lat, *lon}, g.node_pos[g.poi_node[i]]) : 0;
            hits.push_back({rank, dist, i});
        }
        std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
            return a.rank != b.rank ? a.rank < b.rank : a.dist < b.dist;
        });
        if (hits.size() > limit) {
            hits.resize(limit);
        }

        std::string out;
        for (const auto& h : hits) {
            if (!out.empty()) {
                out += ",";
            }
            out += std::format(R"({{"id":{},"name":"{}","category":"{}","pos":{}}})", h.poi,
                               json_escape(g.poi_name(h.poi)), json_escape(g.poi_category(h.poi)),
                               point_json(g.node_pos[g.poi_node[h.poi]]));
        }
        res.set_content("[" + out + "]", "application/json");
    });

    if (!server.bind_to_port(host, port)) {
        return false;
    }
    std::println("Server listening on http://{}:{}", host, port);
    // stdout is buffered when redirected, flush so the line shows up right away.
    std::fflush(stdout);
    on_ready();
    return server.listen_after_bind();
}

} // namespace rp
