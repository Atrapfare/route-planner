#include "server.h"
#include "httplib.h"
#include <sstream>
#include <iostream>
#include <cmath>

static double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371000.0;
    constexpr double DEG = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * DEG;
    double dlon = (lon2 - lon1) * DEG;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2)
             + std::cos(lat1 * DEG) * std::cos(lat2 * DEG)
             * std::sin(dlon / 2) * std::sin(dlon / 2);
    return R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

static double pathDistanceMeters(const Graph& graph, const std::vector<int>& path) {
    double total = 0.0;
    for (size_t i = 1; i < path.size(); i++) {
        const auto& a = graph.nodes[path[i - 1]];
        const auto& b = graph.nodes[path[i]];
        total += haversineMeters(a.latitude, a.longitude, b.latitude, b.longitude);
    }
    return total;
}

Server::Server(Graph& graph, Dijkstra& dijkstra, NearestNeighborSearch& nns, const std::string& frontendPath)
    : graph(graph), dijkstra(dijkstra), nns(nns), frontendPath(frontendPath) {}

void Server::start(int port) {
    httplib::Server svr;

    svr.set_mount_point("/", frontendPath.c_str());

    svr.Get("/api/nearest", [this](const httplib::Request& req, httplib::Response& res) {
        double lat, lon;
        try {
            lat = std::stod(req.get_param_value("lat"));
            lon = std::stod(req.get_param_value("lon"));
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid or missing parameters\"}", "application/json");
            return;
        }

        if (!nns.isInBounds(lat, lon)) {
            res.set_content(
                "{\"error\":\"Coordinate is outside the graph bounds\","
                "\"minLat\":" + std::to_string(nns.getMinLat()) +
                ",\"maxLat\":" + std::to_string(nns.getMaxLat()) +
                ",\"minLon\":" + std::to_string(nns.getMinLon()) +
                ",\"maxLon\":" + std::to_string(nns.getMaxLon()) + "}",
                "application/json"
            );
            return;
        }

        auto result = nns.findNearest(lat, lon);

        std::ostringstream json;
        json << "{\"id\":" << result.nodeId
             << ",\"lat\":" << result.latitude
             << ",\"lon\":" << result.longitude << "}";

        res.set_content(json.str(), "application/json");
    });

    svr.Get("/api/route", [this](const httplib::Request& req, httplib::Response& res) {
        int src, trg;
        try {
            src = std::stoi(req.get_param_value("src"));
            trg = std::stoi(req.get_param_value("trg"));
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid or missing parameters\"}", "application/json");
            return;
        }

        auto result = dijkstra.computeShortestPath(src, trg);

        std::ostringstream json;

        if (result.distance < 0) {
            res.set_content("{\"distanceMeters\":-1,\"path\":[]}", "application/json");
            return;
        }

        double distanceMeters = pathDistanceMeters(graph, result.path);
        json << "{\"distanceMeters\":" << distanceMeters << ",\"path\":[";

        for (size_t i = 0; i < result.path.size(); i++) {
            int nodeId = result.path[i];
            if (i > 0) json << ",";
            // Leaflet expects [lat, lon]
            json << "[" << graph.nodes[nodeId].latitude
                 << "," << graph.nodes[nodeId].longitude << "]";
        }

        json << "]}";

        res.set_content(json.str(), "application/json");
    });

    std::cout << "Server listening on http://localhost:" << port << std::endl;
    svr.listen("0.0.0.0", port);
}
