#pragma once

// Build a cycling graph from .osm.pbf and add SRTM elevations.

#include "rp/graph/graph.hpp"
#include "rp/import/srtm.hpp"

#include <filesystem>

namespace rp {

Graph build_graph(const std::filesystem::path& pbf, SrtmReader& srtm);

} // namespace rp
