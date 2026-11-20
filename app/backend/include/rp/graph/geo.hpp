#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rp {

// WGS84 coordinates in degrees.
struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

inline constexpr double kEarthRadiusM = 6371008.8;

inline double deg_to_rad(double deg) {
    return deg * std::numbers::pi / 180.0;
}

// Great-circle distance in metres (haversine).
inline double haversine_m(LatLon a, LatLon b) {
    const double dlat = deg_to_rad(b.lat - a.lat);
    const double dlon = deg_to_rad(b.lon - a.lon);
    const double s =
        std::sin(dlat / 2) * std::sin(dlat / 2)
        + std::cos(deg_to_rad(a.lat)) * std::cos(deg_to_rad(b.lat)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    // Rounding can push s slightly above 1, and asin would return NaN.
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(std::min(1.0, s)));
}

// Cheaper version of haversine_m for many points close together. The caller
// computes cos(lat) once, so there is no trig per call. Good over a few km,
// off by about 1 % at 100 km.
inline double flat_m(LatLon a, LatLon b, double cos_lat) {
    const double north = deg_to_rad(a.lat - b.lat);
    const double east = deg_to_rad(a.lon - b.lon) * cos_lat;
    return kEarthRadiusM * std::sqrt(north * north + east * east);
}

// Linear interpolation in degrees (t = 0 gives a, t = 1 gives b). Good enough
// for short segments.
inline LatLon lerp(LatLon a, LatLon b, double t) {
    return {a.lat + (b.lat - a.lat) * t, a.lon + (b.lon - a.lon) * t};
}

// Point dist metres away from p in direction bearing_deg (0° = north,
// 90° = east), using a flat approximation.
inline LatLon move(LatLon p, double bearing_deg, double dist) {
    constexpr double kMetersPerDeg = 111320.0;
    const double b = deg_to_rad(bearing_deg);
    return {p.lat + dist * std::cos(b) / kMetersPerDeg,
            p.lon + dist * std::sin(b) / (kMetersPerDeg * std::cos(deg_to_rad(p.lat)))};
}

} // namespace rp
