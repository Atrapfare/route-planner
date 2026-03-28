#pragma once
#include <vector>
#include <string>

struct Node
{
    double latitude;
    double longitude;
};

struct Edge
{
    int source;
    int target;
    int weight;
    int type;
    int maxspeed;
};

class Graph
{
public:
    int numNodes;
    int numEdges;

    std::vector<Node> nodes;
    std::vector<Edge> edges;

    // offset array: edges from node i are at edges[offset[i] .. offset[i+1])
    std::vector<int> offset;

    void loadFromFile(const std::string &filename);

    int edgesBegin(int nodeId) const { return offset[nodeId]; }
    int edgesEnd(int nodeId)   const { return offset[nodeId + 1]; }
};
