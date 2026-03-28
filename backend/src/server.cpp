#include "server.h"
#include "httplib.h"
#include <sstream>
#include <iostream>

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
        json << "{\"distance\":" << result.distance << ",\"path\":[";

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
