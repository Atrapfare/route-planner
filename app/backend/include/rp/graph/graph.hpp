#pragma once

#include "rp/graph/geo.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rp {

// Marks a missing edge, mostly the twin of a one-way edge.
inline constexpr std::uint32_t kNoEdge = 0xFFFFFFFFu;

// Simplified OSM highway tag.
enum class RoadClass : std::uint8_t {
    Cycleway,
    Path,
    Track,
    Footway,
    LivingStreet,
    Residential,
    Service,
    Unclassified,
    Tertiary,
    Secondary,
    Primary,
    Trunk,
    Steps,
    Other
};

// Simplified OSM surface tag.
enum class Surface : std::uint8_t {
    Unknown,
    Paved,
    Paving,
    Cobble,
    Gravel,
    Unpaved,
};

enum EdgeFlags : std::uint8_t {
    kBridge = 1 << 0,
    kTunnel = 1 << 1,
    kCycleInfra = 1 << 2,
    kOneway = 1 << 3,
    // From cycle route relations and nature areas, used by the sliders.
    kBikeRoute = 1 << 4,
    kNature = 1 << 5,
};

// Directed graph in CSR format. Every node and edge property is its own array,
// indexed by ID. The outgoing edges of node v are first_out[v] to
// first_out[v+1] - 1.
struct Graph {
    std::vector<LatLon> node_pos;
    std::vector<float> node_elevation_m;
    std::vector<std::uint32_t> first_out; // first outgoing edge; length = node count + 1

    std::vector<std::uint32_t> head;
    std::vector<std::uint32_t> twin;         // same road in the other direction, or kNoEdge
    std::vector<float> length_m;
    std::vector<float> ascent_m;
    std::vector<float> descent_m;
    std::vector<std::uint16_t> curvy_deg_km; // sum of direction changes in degrees per km
    std::vector<RoadClass> road_class;
    std::vector<Surface> surface;
    std::vector<std::uint8_t> flags;

    // Intermediate points of edge e (excluding its endpoint nodes):
    //   geo_points[geo_first[e]] ... geo_points[geo_first[e+1] - 1]
    std::vector<std::uint32_t> geo_first; // length = edge count + 1
    std::vector<LatLon> geo_points;
    std::vector<float> geo_elevation_m; // elevation of each geo point

    // POIs snapped to graph nodes. Name of POI i:
    //   poi_names[poi_name_first[i] .. poi_name_first[i+1]-1]
    std::vector<std::uint32_t> poi_node;
    std::vector<std::uint32_t> poi_name_first; // length = POI count + 1
    std::vector<char> poi_names;
    // The category (value of the OSM tag) is stored the same way.
    std::vector<std::uint32_t> poi_category_first;
    std::vector<char> poi_categories;

    std::uint32_t num_pois() const {
        return static_cast<std::uint32_t>(poi_node.size());
    }
    std::string_view poi_name(std::uint32_t i) const {
        return {poi_names.data() + poi_name_first[i], poi_name_first[i + 1] - poi_name_first[i]};
    }

    std::string_view poi_category(std::uint32_t i) const {
        return {poi_categories.data() + poi_category_first[i], poi_category_first[i + 1] - poi_category_first[i]};
    }

    std::uint32_t num_nodes() const {
        return static_cast<std::uint32_t>(node_pos.size());
    }
    std::uint32_t num_edges() const {
        return static_cast<std::uint32_t>(head.size());
    }
};

} // namespace rp
