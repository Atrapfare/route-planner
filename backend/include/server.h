#pragma once
#include "graph.h"
#include "dijkstra.h"
#include "nearest_neighbor.h"
#include <string>

class Server {
public:
    Server(Graph& graph, Dijkstra& dijkstra, NearestNeighborSearch& nns, const std::string& frontendPath);

    void start(int port = 8080);

private:
    Graph& graph;
    Dijkstra& dijkstra;
    NearestNeighborSearch& nns;
    std::string frontendPath;
};
