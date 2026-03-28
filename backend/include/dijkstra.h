#pragma once
#include "graph.h"
#include <vector>

struct DijkstraResult {
    int distance;           // shortest distance in meters, -1 if unreachable
    std::vector<int> path;  // node IDs from source to target
};

class Dijkstra {
public:
    explicit Dijkstra(const Graph& graph);

    DijkstraResult computeShortestPath(int source, int target);

private:
    const Graph& graph;

    std::vector<int>  dist;     // dist[i]    = shortest known distance to node i
    std::vector<int>  prev;     // prev[i]    = predecessor of node i on shortest path
    std::vector<bool> visited;  // visited[i] = node i has been settled
};
