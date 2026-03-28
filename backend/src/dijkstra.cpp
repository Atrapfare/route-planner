#include "dijkstra.h"
#include <queue>
#include <limits>
#include <algorithm>

Dijkstra::Dijkstra(const Graph& graph)
    : graph(graph),
      dist(graph.numNodes),
      prev(graph.numNodes),
      visited(graph.numNodes)
{
}

DijkstraResult Dijkstra::computeShortestPath(int source, int target)
{
    std::fill(dist.begin(), dist.end(), std::numeric_limits<int>::max());
    std::fill(prev.begin(), prev.end(), -1);
    std::fill(visited.begin(), visited.end(), false);

    dist[source] = 0;

    using PQEntry = std::pair<int, int>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
    pq.push({0, source});

    while (!pq.empty())
    {
        auto [d, u] = pq.top();
        pq.pop();

        // skip stale entries (node was already settled with a shorter distance)
        if (visited[u])
            continue;

        visited[u] = true;

        if (u == target)
            break;

        for (int i = graph.edgesBegin(u); i < graph.edgesEnd(u); i++)
        {
            int v = graph.edges[i].target;
            int w = graph.edges[i].weight;

            if (dist[u] + w < dist[v])
            {
                dist[v] = dist[u] + w;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    if (dist[target] == std::numeric_limits<int>::max())
        return {-1, {}};

    std::vector<int> path;
    for (int node = target; node != -1; node = prev[node])
        path.push_back(node);

    std::reverse(path.begin(), path.end());

    return {dist[target], path};
}
