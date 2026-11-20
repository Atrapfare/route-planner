// Loads or builds the graph and the ALT landmarks, then runs the HTTP server,
// the measurement series (--measure) or the ALT benchmark (--bench-alt).

#include "rp/eval/measure.hpp"
#include "rp/graph/graph.hpp"
#include "rp/graph/graph_io.hpp"
#include "rp/routing/landmarks.hpp"
#include "rp/server/server.hpp"
#include "rp/import/osm_import.hpp"
#include "rp/import/srtm.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Relative to app/backend/, where node app/start runs the backend. --pbf picks
// one of the extracts in here.
const fs::path kGraphDir = "data/osm/graphs";
const fs::path kDefaultSrtmDir = "data/srtm";
// Per-region caches: <region>.graph.bin and <region>.landmarks.bin.
const fs::path kCacheDir = "data";

const char* const kUsage = R"(Usage: route_planner --pbf NAME [options]
  --pbf NAME         OSM extract in data/osm/graphs/ to build the graph from, e.g. baden-wuerttemberg-261115
                     (.osm.pbf may be left out; required)
  --srtm DIR         folder with the SRTM .hgt tiles (default: data/srtm)
  --rebuild          build the graph again even if its cache exists
  --threads N        threads for the round-trip methods (default: number of cores, at most 8)
  --port N           port of the HTTP server (default: 8080)
  --no-alt           route without ALT landmarks
  --measure DIR      run the slider measurement series, write CSVs to DIR and exit (instead of the server)
  --starts N         random start points for --measure in addition to the 5 named places (default: 100)
  --bench-alt        compare Dijkstra against ALT and exit
  --ready-file FILE  write this file as soon as the server listens (used by app/start)
  --help             show this help)";

// Local only, the frontend goes through the Vite proxy.
const std::string kHost = "127.0.0.1";
constexpr int kDefaultPort = 8080;

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void print_stats(const rp::Graph& g) {
    double total_km = 0, ascent_km = 0;
    std::uint32_t oneway = 0;
    for (std::uint32_t e = 0; e < g.num_edges(); ++e) {
        total_km += g.length_m[e] / 1000.0;
        ascent_km += g.ascent_m[e] / 1000.0;
        if (g.flags[e] & rp::kOneway) {
            ++oneway;
        }
    }
    std::println("Graph: {} nodes, {} edges ({} one-way), {} geometry points", g.num_nodes(), g.num_edges(), oneway,
                 g.geo_points.size());
    std::println("       {:.0f} km of directed edges, {:.0f} km total ascent", total_km, ascent_km);
}

// Cache name = PBF file name without .osm.pbf. A new file under an old name
// therefore needs --rebuild.
std::string region_name(const fs::path& pbf) {
    std::string name = pbf.filename().string();
    for (const std::string_view ext : {".osm.pbf", ".pbf"}) {
        if (name.ends_with(ext)) {
            name.resize(name.size() - ext.size());
            break;
        }
    }
    return name;
}

// Looks up the --pbf name (with or without .osm.pbf) in data/osm/graphs/.
// Paths are not accepted.
std::optional<fs::path> pbf_in_graph_dir(std::string_view name) {
    if (name.empty() || name.find_first_of("/\\") != std::string_view::npos) {
        return std::nullopt;
    }
    std::string file{name};
    if (!file.ends_with(".pbf")) {
        file += ".osm.pbf";
    }
    return (kGraphDir / file).make_preferred();
}

void print_available_graphs() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(kGraphDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".pbf") {
            names.push_back(region_name(entry.path()));
        }
    }
    std::ranges::sort(names);
    if (names.empty()) {
        std::println(stderr, "No OSM extracts in {} (see README: download data/).", kGraphDir.string());
        return;
    }
    std::println(stderr, "Available in {}:", kGraphDir.string());
    for (const auto& n : names) {
        std::println(stderr, "  {}", n);
    }
}

} // namespace

int main(int argc, char** argv) {
    fs::path pbf_file;
    fs::path srtm_dir = kDefaultSrtmDir;
    bool rebuild = false;
    fs::path ready_file;
    fs::path measure_dir;
    std::size_t starts = 100;
    bool use_alt = true;
    bool bench_alt = false;
    std::size_t threads = 0;
    int port = kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        const auto value = [&]() -> const char* {
            return i + 1 < argc ? argv[++i] : nullptr;
        };
        const char* v = nullptr;
        if (arg == "--help" || arg == "-h") {
            std::println("{}", kUsage);
            return 0;
        } else if (arg == "--rebuild") {
            rebuild = true;
        } else if (arg == "--no-alt") {
            use_alt = false;
        } else if (arg == "--bench-alt") {
            bench_alt = true;
        } else if (arg == "--pbf" && (v = value())) {
            const auto file = pbf_in_graph_dir(v);
            if (!file) {
                std::println(stderr, "Error: --pbf takes only the name of an extract in {}, got '{}'\n{}",
                             kGraphDir.string(), v, kUsage);
                print_available_graphs();
                return 2;
            }
            pbf_file = *file;
        } else if (arg == "--srtm" && (v = value())) {
            srtm_dir = v;
        } else if (arg == "--ready-file" && (v = value())) {
            ready_file = v;
        } else if (arg == "--measure" && (v = value())) {
            measure_dir = v;
        } else if (arg == "--starts" && (v = value())) {
            char* end = nullptr;
            const long n = std::strtol(v, &end, 10);
            if (*v == '\0' || *end != '\0' || n < 0) {
                std::println(stderr, "Error: --starts needs a whole number >= 0, got '{}'\n{}", v, kUsage);
                return 2;
            }
            starts = static_cast<std::size_t>(n);
        } else if (arg == "--threads" && (v = value())) {
            char* end = nullptr;
            const long n = std::strtol(v, &end, 10);
            if (*v == '\0' || *end != '\0' || n < 0) {
                std::println(stderr, "Error: --threads needs a whole number >= 0, got '{}'\n{}", v, kUsage);
                return 2;
            }
            threads = static_cast<std::size_t>(n);
        } else if (arg == "--port" && (v = value())) {
            char* end = nullptr;
            const long n = std::strtol(v, &end, 10);
            if (*v == '\0' || *end != '\0' || n < 1 || n > 65535) {
                std::println(stderr, "Error: --port needs a number from 1 to 65535, got '{}'\n{}", v, kUsage);
                return 2;
            }
            port = static_cast<int>(n);
        } else {
            std::println(stderr, "Error: unknown option or missing value: {}\n{}", arg, kUsage);
            return 2;
        }
    }
    if (pbf_file.empty()) {
        std::println(stderr, "Error: --pbf is required\n{}", kUsage);
        print_available_graphs();
        return 2;
    }
    const std::string region = region_name(pbf_file);
    const fs::path graph_file = kCacheDir / (region + ".graph.bin");
    const fs::path landmark_file = kCacheDir / (region + ".landmarks.bin");

    try {
        auto t0 = std::chrono::steady_clock::now();
        std::optional<rp::Graph> graph;
        if (!rebuild) {
            std::println("Loading {} ...", graph_file.string());
            graph = rp::load_graph(graph_file);
            if (graph) {
                std::println("  loaded in {:.1f} s", seconds_since(t0));
            }
        }

        if (!graph) {
            if (!fs::is_regular_file(pbf_file)) {
                std::println(stderr, "{} not found.", pbf_file.string());
                print_available_graphs();
                return 1;
            }
            std::println("Building graph from {} ...", pbf_file.string());

            t0 = std::chrono::steady_clock::now();
            if (!fs::is_directory(srtm_dir)) {
                std::println(stderr, "Warning: SRTM folder {} not found, all elevations will be 0.", srtm_dir.string());
            }
            rp::SrtmReader srtm(srtm_dir);
            rp::Graph g = rp::build_graph(pbf_file, srtm);
            std::println("  built in {:.1f} s", seconds_since(t0));
            if (const auto missing = srtm.missing_tiles(); !missing.empty()) {
                std::string names;
                for (const auto& m : missing) {
                    names += " " + m;
                }
                std::println(stderr, "Warning: {} SRTM tiles missing in {}, elevation 0 there:{}", missing.size(),
                             srtm_dir.string(), names);
            }

            const auto t2 = std::chrono::steady_clock::now();
            fs::create_directories(kCacheDir);
            rp::save_graph(g, graph_file);
            std::println("  saved {} in {:.1f} s", graph_file.string(), seconds_since(t2));

            graph = std::move(g);
        }

        print_stats(*graph);

        std::optional<rp::Landmarks> landmarks;
        if (use_alt || bench_alt) {
            const auto t3 = std::chrono::steady_clock::now();
            std::println("Landmarks ...");
            landmarks = rp::Landmarks::load_or_build(*graph, landmark_file);
            std::println("  ready in {:.1f} s", seconds_since(t3));
        }
        const rp::Landmarks* lm = use_alt && landmarks ? &*landmarks : nullptr;

        if (bench_alt) {
            rp::run_alt_benchmark(*graph, *landmarks);
            return 0;
        }

        if (!measure_dir.empty()) {
            std::println("Measuring -> {} (ALT {}) ...", measure_dir.string(), lm ? "on" : "off");
            rp::MeasureOptions opt;
            opt.out_dir = measure_dir;
            opt.threads = threads;
            opt.random_starts = starts;
            opt.graph_name = region;
            rp::run_measurement(*graph, lm, opt);
            return 0;
        }

        // app/start waits for the ready file before it starts the frontend.
        const auto on_ready = [&] {
            if (!ready_file.empty()) {
                std::ofstream{ready_file} << "ready\n";
            }
        };
        if (!rp::run_server(*graph, lm, threads, kHost, port, on_ready)) {
            std::println(stderr, "Error: cannot open port {} (already in use?)", port);
            return 1;
        }
    } catch (const std::exception& ex) {
        std::println(stderr, "Error: {}", ex.what());
        return 1;
    }
    return 0;
}
