#include "graph.h"
#include <fstream>
#include <stdexcept>
#include <charconv>
#include <string>

static inline void parseInt(const char*& ptr, int& value) {
    auto [p, ec] = std::from_chars(ptr, ptr + 20, value);
    ptr = p + 1;
}

static inline void parseLongLong(const char*& ptr, long long& value) {
    auto [p, ec] = std::from_chars(ptr, ptr + 20, value);
    ptr = p + 1;
}

static inline void parseDouble(const char*& ptr, double& value) {
    value = std::strtod(ptr, const_cast<char**>(&ptr));
    ptr++; // skip space
}

void Graph::loadFromFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    static char buffer[1 << 20];
    file.rdbuf()->pubsetbuf(buffer, sizeof(buffer));

    std::string line;

    // skip comment lines
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#')
            break;
    }

    numNodes = std::stoi(line);

    std::getline(file, line);
    numEdges = std::stoi(line);

    nodes.resize(numNodes);

    int    skip_i;
    long long skip_ll;

    for (int i = 0; i < numNodes; i++) {
        std::getline(file, line);
        const char* ptr = line.c_str();

        parseInt(ptr, skip_i);       // nodeId (unused)
        parseLongLong(ptr, skip_ll); // osmId  (unused)
        parseDouble(ptr, nodes[i].latitude);
        parseDouble(ptr, nodes[i].longitude);
        parseInt(ptr, skip_i);       // elevation (unused)
    }

    edges.resize(numEdges);

    for (int i = 0; i < numEdges; i++) {
        std::getline(file, line);
        const char* ptr = line.c_str();

        parseInt(ptr, edges[i].source);
        parseInt(ptr, edges[i].target);
        parseInt(ptr, edges[i].weight);
        parseInt(ptr, edges[i].type);
        parseInt(ptr, edges[i].maxspeed);
    }

    // build offset array: offset[i] = index of first edge from node i
    offset.assign(numNodes + 1, 0);

    for (int i = 0; i < numEdges; i++) {
        offset[edges[i].source + 1]++;
    }

    for (int i = 0; i < numNodes; i++) {
        offset[i + 1] += offset[i];
    }
}
