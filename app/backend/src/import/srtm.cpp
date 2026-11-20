#include "rp/import/srtm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <format>
#include <fstream>

namespace rp {

namespace {
constexpr int kSize = 1201;            // samples per row and column
constexpr std::int16_t kVoid = -32768; // missing elevation sample

// File name of the tile with south-west corner (lat_floor, lon_floor).
std::string tile_name(int lat_floor, int lon_floor) {
    return std::format("{}{:02}{}{:03}.hgt", lat_floor < 0 ? 'S' : 'N', std::abs(lat_floor), lon_floor < 0 ? 'W' : 'E',
                       std::abs(lon_floor));
}
} // namespace

const std::vector<std::int16_t>& SrtmReader::tile(int lat_floor, int lon_floor) {
    const auto key = std::pair{lat_floor, lon_floor};
    if (auto it = tiles_.find(key); it != tiles_.end()) {
        return it->second;
    }

    const auto path = directory_ / tile_name(lat_floor, lon_floor);
    std::vector<std::int16_t> samples;
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> raw(2 * kSize * kSize);
    if (in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()))) {
        samples.resize(kSize * kSize);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<std::int16_t>((raw[2 * i] << 8) | raw[2 * i + 1]);
        }
    }
    // Missing tiles are cached too (empty), so we only look for them once.
    return tiles_.emplace(key, std::move(samples)).first->second;
}

std::vector<std::string> SrtmReader::missing_tiles() const {
    std::vector<std::string> names;
    for (const auto& [key, samples] : tiles_) {
        if (samples.empty()) {
            names.push_back(tile_name(key.first, key.second));
        }
    }
    return names;
}

std::optional<double> SrtmReader::elevation(LatLon p) {
    const int lat_floor = static_cast<int>(std::floor(p.lat));
    const int lon_floor = static_cast<int>(std::floor(p.lon));
    const auto& t = tile(lat_floor, lon_floor);
    if (t.empty()) {
        return std::nullopt;
    }

    // Fractional grid position: x eastward, y southward (row 0 at north edge).
    const double x = (p.lon - lon_floor) * (kSize - 1);
    const double y = (lat_floor + 1 - p.lat) * (kSize - 1);
    const int x0 = std::clamp(static_cast<int>(x), 0, kSize - 2);
    const int y0 = std::clamp(static_cast<int>(y), 0, kSize - 2);
    const double fx = x - x0;
    const double fy = y - y0;

    // Bilinear weights. Void samples are skipped and the other weights scaled
    // up to make up for it.
    const int corner[4][2] = {{y0, x0}, {y0, x0 + 1}, {y0 + 1, x0}, {y0 + 1, x0 + 1}};
    const double weight[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    double sum = 0, wsum = 0;
    for (int i = 0; i < 4; ++i) {
        const std::int16_t h = t[static_cast<std::size_t>(corner[i][0]) * kSize + corner[i][1]];
        if (h == kVoid) {
            continue;
        }
        sum += weight[i] * h;
        wsum += weight[i];
    }
    if (wsum <= 1e-9) {
        return std::nullopt; // all four samples are void
    }
    return sum / wsum;
}

} // namespace rp
