#pragma once

#include "rp/graph/graph.hpp"

#include <filesystem>
#include <optional>

namespace rp {

// Writes to a .tmp file and renames it at the end, so an interrupted write
// never leaves a broken graph.bin behind.
void save_graph(const Graph& g, const std::filesystem::path& file);

// nullopt (with the reason logged) if the file is missing, outdated or
// corrupt. main() then rebuilds the graph.
std::optional<Graph> load_graph(const std::filesystem::path& file);

} // namespace rp
