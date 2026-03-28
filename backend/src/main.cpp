#include "graph.h"
#include "dijkstra.h"
#include "nearest_neighbor.h"
#include "server.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::ios::sync_with_stdio(false);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <graph.fmi>" << std::endl;
        return 1;
    }
    std::string graphFile = argv[1];

    Graph graph;
    std::cout << "Loading graph: " << graphFile << std::endl;
    auto t1 = std::chrono::high_resolution_clock::now();
    graph.loadFromFile(graphFile);
    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "Graph loaded in " << std::chrono::duration<double>(t2 - t1).count() << "s" << std::endl;
    std::cout << "Nodes: " << graph.numNodes << ", Edges: " << graph.numEdges << std::endl;

    std::cout << "Building nearest-neighbor index..." << std::endl;
    auto t3 = std::chrono::high_resolution_clock::now();
    NearestNeighborSearch nns(graph);
    auto t4 = std::chrono::high_resolution_clock::now();
    std::cout << "Index built in " << std::chrono::duration<double>(t4 - t3).count() << "s" << std::endl;

    Dijkstra dijkstra(graph);

    std::filesystem::path exeDir = std::filesystem::path(argv[0]).parent_path();
    std::string frontendPath = (exeDir / "../../../frontend").string();

    Server server(graph, dijkstra, nns, frontendPath);
    server.start(8080);

    return 0;
}
