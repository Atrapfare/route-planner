#include "nearest_neighbor.h"
#include <cmath>
#include <limits>
#include <algorithm>

NearestNeighborSearch::NearestNeighborSearch(const Graph& graph)
    : graph(graph),
      minLat(0), maxLat(0),
      minLon(0), maxLon(0),
      gridRows(0), gridCols(0),
      cellHeight(0), cellWidth(0)
{
    buildIndex();
}

void NearestNeighborSearch::buildIndex()
{
    minLat = std::numeric_limits<double>::max();
    maxLat = std::numeric_limits<double>::lowest();
    minLon = std::numeric_limits<double>::max();
    maxLon = std::numeric_limits<double>::lowest();

    for (int i = 0; i < graph.numNodes; i++) {
        double lat = graph.nodes[i].latitude;
        double lon = graph.nodes[i].longitude;
        if (lat < minLat) minLat = lat;
        if (lat > maxLat) maxLat = lat;
        if (lon < minLon) minLon = lon;
        if (lon > maxLon) maxLon = lon;
    }

    // sqrt(n) x sqrt(n) grid
    gridRows = (int)std::sqrt((double)graph.numNodes);
    gridCols = gridRows;

    cellHeight = (maxLat - minLat) / gridRows;
    cellWidth  = (maxLon - minLon) / gridCols;

    grid.assign(gridRows, std::vector<std::vector<int>>(gridCols));

    for (int i = 0; i < graph.numNodes; i++) {
        int row = getRow(graph.nodes[i].latitude);
        int col = getCol(graph.nodes[i].longitude);
        grid[row][col].push_back(i);
    }
}

NearestResult NearestNeighborSearch::findNearest(double lat, double lon) const
{
    int startRow = getRow(lat);
    int startCol = getCol(lon);

    double bestDist = std::numeric_limits<double>::max();
    int bestNode = -1;

    // expand outward in rings; stop once the next ring is guaranteed farther than bestDist
    for (int radius = 0; radius < std::max(gridRows, gridCols); radius++) {

        for (int dr = -radius; dr <= radius; dr++) {
            for (int dc = -radius; dc <= radius; dc++) {

                // only process the outer shell of this ring
                if (std::abs(dr) != radius && std::abs(dc) != radius)
                    continue;

                int r = startRow + dr;
                int c = startCol + dc;

                if (r < 0 || r >= gridRows || c < 0 || c >= gridCols)
                    continue;

                for (int nodeId : grid[r][c]) {
                    double d = distance(lat, lon,
                                        graph.nodes[nodeId].latitude,
                                        graph.nodes[nodeId].longitude);
                    if (d < bestDist) {
                        bestDist = d;
                        bestNode = nodeId;
                    }
                }
            }
        }

        if (bestNode != -1) {
            double minDistNextRing = (radius) * std::min(cellHeight, cellWidth);
            if (minDistNextRing > bestDist)
                break;
        }
    }

    return { bestNode, graph.nodes[bestNode].latitude, graph.nodes[bestNode].longitude };
}

bool NearestNeighborSearch::isInBounds(double lat, double lon) const
{
    const double buffer = 0.5;
    return lat >= minLat - buffer && lat <= maxLat + buffer &&
           lon >= minLon - buffer && lon <= maxLon + buffer;
}

int NearestNeighborSearch::getRow(double lat) const
{
    int row = (int)((lat - minLat) / cellHeight);
    return std::clamp(row, 0, gridRows - 1);
}

int NearestNeighborSearch::getCol(double lon) const
{
    int col = (int)((lon - minLon) / cellWidth);
    return std::clamp(col, 0, gridCols - 1);
}

double NearestNeighborSearch::distance(double lat1, double lon1, double lat2, double lon2) const
{
    double dlat = lat1 - lat2;
    double dlon = lon1 - lon2;
    return std::sqrt(dlat * dlat + dlon * dlon);
}
