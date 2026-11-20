#include "rp/graph/graph_io.hpp"

#include <cstdint>
#include <fstream>
#include <print>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace rp {

namespace {

constexpr std::uint32_t kMagic = 0x48505247; // "GRPH" (little-endian)
// Bump this whenever the format changes, old caches are then rebuilt.
constexpr std::uint32_t kFormatVersion = 9;

template <typename T> void write_pod(std::ofstream& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T> void write_vec(std::ofstream& out, const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    write_pod(out, static_cast<std::uint64_t>(v.size()));
    out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
}

template <typename T> bool read_pod(std::ifstream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}

// Counterpart of write_vec. n is capped so a corrupt file cannot cause a huge
// allocation.
template <typename T> bool read_vec(std::ifstream& in, std::vector<T>& v) {
    std::uint64_t n = 0;
    if (!read_pod(in, n) || n > (1ull << 34)) {
        return false;
    }
    v.resize(n);
    in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T)));
    return static_cast<bool>(in);
}

// The one list of all graph arrays, used for writing and reading so the order
// cannot get out of sync. Stops as soon as fn returns false.
template <typename G, typename F> bool for_each_array(G& g, F&& fn) {
    return fn(g.node_pos) && fn(g.node_elevation_m) && fn(g.first_out) && fn(g.head) && fn(g.twin) && fn(g.length_m)
        && fn(g.ascent_m) && fn(g.descent_m) && fn(g.curvy_deg_km) && fn(g.road_class) && fn(g.surface) && fn(g.flags)
        && fn(g.geo_first) && fn(g.geo_points) && fn(g.geo_elevation_m) && fn(g.poi_node) && fn(g.poi_name_first)
        && fn(g.poi_names) && fn(g.poi_category_first) && fn(g.poi_categories);
}

} // namespace

void save_graph(const Graph& g, const std::filesystem::path& file) {
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write " + tmp.string());
        }
        write_pod(out, kMagic);
        write_pod(out, kFormatVersion);
        for_each_array(g, [&](const auto& v) {
            write_vec(out, v);
            return true;
        });
        if (!out) {
            throw std::runtime_error("error while writing " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, file);
}

std::optional<Graph> load_graph(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::println("  {} not found", file.string());
        return std::nullopt;
    }

    std::uint32_t magic = 0, format = 0;
    if (!read_pod(in, magic) || magic != kMagic || !read_pod(in, format) || format != kFormatVersion) {
        std::println("  {}: unknown or outdated file format", file.string());
        return std::nullopt;
    }

    Graph g;
    if (!for_each_array(g, [&](auto& v) {
            return read_vec(in, v);
        })) {
        std::println("  {}: damaged data", file.string());
        return std::nullopt;
    }
    if (g.first_out.size() != g.node_pos.size() + 1 || g.geo_first.size() != g.head.size() + 1) {
        std::println("  {}: inconsistent data", file.string());
        return std::nullopt;
    }
    return g;
}

} // namespace rp
