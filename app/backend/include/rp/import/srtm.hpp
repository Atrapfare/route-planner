#pragma once

// Reads elevations from SRTM3 .hgt tiles. A tile covers 1° x 1° and is named
// after its south-west corner. It holds 1201 x 1201 big-endian int16 values,
// rows from north to south, -32768 where data is missing.

#include "rp/graph/geo.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rp {

class SrtmReader {
public:
    explicit SrtmReader(std::filesystem::path directory)
        : directory_(std::move(directory)) {
    }

    // Elevation at p in metres, bilinear over the four surrounding samples.
    // nullopt if the tile is missing or all four samples are void.
    std::optional<double> elevation(LatLon p);

    // Tiles that were needed but are missing or unreadable.
    std::vector<std::string> missing_tiles() const;

private:
    // Loaded on first use, empty if the file does not exist.
    const std::vector<std::int16_t>& tile(int lat_floor, int lon_floor);

    std::filesystem::path directory_;
    std::map<std::pair<int, int>, std::vector<std::int16_t>> tiles_; // cached samples by (lat, lon)
};

} // namespace rp
