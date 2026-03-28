#pragma once
#include "graph.h"
#include <vector>

struct NearestResult {
    int nodeId;
    double latitude;
    double longitude;
};

// Grid-based nearest-node lookup.
// Partitions the coordinate space into a sqrt(n) x sqrt(n) grid and
// searches outward in rings until no closer node can exist in the next ring.
class NearestNeighborSearch {
public:
    explicit NearestNeighborSearch(const Graph& graph);

    NearestResult findNearest(double lat, double lon) const;

    // 0.5 degree buffer around the graph bounds
    bool isInBounds(double lat, double lon) const;

    double getMinLat() const { return minLat; }
    double getMaxLat() const { return maxLat; }
    double getMinLon() const { return minLon; }
    double getMaxLon() const { return maxLon; }

private:
    const Graph& graph;

    double minLat, maxLat;
    double minLon, maxLon;

    int gridRows, gridCols;
    double cellHeight, cellWidth;

    // grid[row][col] = list of node IDs in that cell
    std::vector<std::vector<std::vector<int>>> grid;

    void   buildIndex();
    int    getRow(double lat) const;
    int    getCol(double lon) const;
    double distance(double lat1, double lon1, double lat2, double lon2) const;
};
