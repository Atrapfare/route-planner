// A PBF contains nodes, then ways, then relations, but we need them the other
// way round: the relations say which ways are cycle routes, the ways say which
// nodes to keep. So the file is read three times, once per type.

#include "rp/import/osm_import.hpp"
#include "rp/import/nature_map.hpp"
#include "rp/routing/spatial_index.hpp"

#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>
#include <optional>
#include <print>
#include <stdexcept>
#include <set>
#include <string>
#include <string_view>

namespace rp {

namespace {

using osmium::object_id_type;

bool is_one_of(const char* value, std::initializer_list<std::string_view> list) {
    return value && std::ranges::find(list, std::string_view{value}) != list.end();
}

// Access for bikes, most specific tag first: bicycle, vehicle, access.
// motorroad=yes forbids cycling unless bicycle says otherwise. motor_vehicle
// is ignored, it does not apply to bikes.
bool bicycle_forbidden(const osmium::TagList& tags) {
    const char* bicycle = tags["bicycle"];
    const char* vehicle = tags["vehicle"];
    const bool motorroad = is_one_of(tags["motorroad"], {"yes"});
    const char* rule = bicycle ? bicycle : motorroad ? "no" : vehicle ? vehicle : tags["access"];
    return is_one_of(rule, {"no", "private", "use_sidepath", "delivery", "emergency"});
}

std::optional<RoadClass> bicycle_road_class(const osmium::TagList& tags) {
    const char* highway = tags["highway"];
    if (!highway || bicycle_forbidden(tags)) {
        return std::nullopt;
    }
    const bool bike_yes = is_one_of(tags["bicycle"], {"yes", "designated", "permissive"});
    if (is_one_of(tags["area"], {"yes"})) {
        return std::nullopt;
    }

    const std::string_view h{highway};
    if (h == "cycleway") {
        return RoadClass::Cycleway;
    }
    if (h == "path") {
        return RoadClass::Path;
    }
    if (h == "track") {
        return RoadClass::Track;
    }
    if (h == "living_street") {
        return RoadClass::LivingStreet;
    }
    if (h == "residential") {
        return RoadClass::Residential;
    }
    if (h == "service") {
        return RoadClass::Service;
    }
    if (h == "unclassified" || h == "road") {
        return RoadClass::Unclassified;
    }
    if (h.starts_with("tertiary")) {
        return RoadClass::Tertiary;
    }
    if (h.starts_with("secondary")) {
        return RoadClass::Secondary;
    }
    if (h.starts_with("primary")) {
        return RoadClass::Primary;
    }
    if (h == "steps") {
        return RoadClass::Steps;
    }
    // Footways and motorroads require explicit bicycle permission.
    if ((h == "footway" || h == "pedestrian" || h == "bridleway") && bike_yes) {
        return RoadClass::Footway;
    }
    if (h.starts_with("trunk") && bike_yes) {
        return RoadClass::Trunk;
    }
    return std::nullopt;
}

Surface parse_surface(const osmium::TagList& tags) {
    const char* s = tags["surface"];
    if (is_one_of(s, {"asphalt", "concrete", "paved"})) {
        return Surface::Paved;
    }
    if (is_one_of(s, {"paving_stones", "compacted", "fine_gravel"})) {
        return Surface::Paving;
    }
    if (is_one_of(s, {"sett", "cobblestone", "unhewn_cobblestone"})) {
        return Surface::Cobble;
    }
    if (is_one_of(s, {"gravel", "pebblestone"})) {
        return Surface::Gravel;
    }
    if (is_one_of(s, {"unpaved", "ground", "dirt", "earth", "grass", "sand", "mud"})) {
        return Surface::Unpaved;
    }
    // Tracks without surface tag fall back to tracktype (grade1 = solid,
    // grade5 = soft).
    const char* grade = tags["tracktype"];
    if (is_one_of(grade, {"grade1"})) {
        return Surface::Paved;
    }
    if (is_one_of(grade, {"grade2"})) {
        return Surface::Paving;
    }
    if (is_one_of(grade, {"grade3"})) {
        return Surface::Gravel;
    }
    if (is_one_of(grade, {"grade4", "grade5"})) {
        return Surface::Unpaved;
    }
    return Surface::Unknown;
}

// Directions bikes may ride, {forward, backward} relative to the way's node order.
std::pair<bool, bool> bicycle_directions(const osmium::TagList& tags) {
    const char* oneway = tags["oneway"];
    bool fwd = true, bwd = true;
    // Roundabouts are one-way; oneway=-1 reverses the drawn direction.
    if (is_one_of(oneway, {"yes", "1", "true"}) || is_one_of(tags["junction"], {"roundabout", "circular"})) {
        bwd = false;
    } else if (is_one_of(oneway, {"-1", "reverse"})) {
        fwd = false;
    }

    // Cycling against the one-way direction if it is explicitly allowed.
    const std::initializer_list<std::string_view> opposite{"opposite", "opposite_lane", "opposite_track"};
    if (is_one_of(tags["oneway:bicycle"], {"no"}) || is_one_of(tags["cycleway"], opposite)
        || is_one_of(tags["cycleway:left"], opposite) || is_one_of(tags["cycleway:right"], opposite)) {
        fwd = bwd = true;
    } else if (is_one_of(tags["oneway:bicycle"], {"yes", "1"})) {
        bwd = false;
    }
    return {fwd, bwd};
}

std::uint8_t edge_flags(const osmium::TagList& tags) {
    std::uint8_t f = 0;
    if (tags["bridge"] && std::strcmp(tags["bridge"], "no") != 0) {
        f |= kBridge;
    }
    if (tags["tunnel"] && std::strcmp(tags["tunnel"], "no") != 0) {
        f |= kTunnel;
    }
    for (const char* key : {"cycleway", "cycleway:both", "cycleway:left", "cycleway:right"}) {
        // "separate" means the cycleway is mapped separately, not on this road.
        if (is_one_of(tags[key], {"lane", "track", "shared_lane", "opposite_lane", "opposite_track"})) {
            f |= kCycleInfra;
        }
    }
    return f;
}

bool is_nature(const osmium::TagList& tags) {
    return is_one_of(tags["landuse"], {"forest", "meadow", "grass", "orchard"})
        || is_one_of(tags["natural"], {"wood", "water", "scrub", "grassland", "heath", "wetland"})
        || is_one_of(tags["leisure"], {"park", "garden", "nature_reserve"});
}

// Category of a named POI is the value of its OSM tag, nullptr if the object
// is no POI.
const char* poi_category(const osmium::TagList& tags) {
    if (!tags["name"]) {
        return nullptr;
    }
    for (const char* key : {"amenity", "shop", "tourism", "historic"}) {
        if (const char* v = tags[key]) {
            return v;
        }
    }
    if (const char* v = tags["natural"]; is_one_of(v, {"peak", "waterfall"})) {
        return v;
    }
    if (is_one_of(tags["railway"], {"station", "halt"}) || is_one_of(tags["public_transport"], {"station"})) {
        return "station";
    }
    return nullptr;
}

// POI from a node or a closed way. For areas, the refs are boundary nodes
// and pos becomes their centroid once the node pass is done.
struct RawPoi {
    std::string name;
    std::string category;
    LatLon pos;
    std::size_t first_ref = 0;  // area boundary in WayCollector::poi_refs
    std::uint32_t num_refs = 0; // 0 for point POIs
};

// First pass, relations only. Collects the member ways of cycle routes
// (route=bicycle) and the boundary ways of nature multipolygons, with one shape
// ID per area so its rings can be filled together later.
struct RelationCollector : osmium::handler::Handler {
    std::vector<object_id_type> bike_ways;
    std::vector<std::pair<object_id_type, std::uint32_t>> nature_members;
    std::uint32_t nature_shapes = 0;

    void relation(const osmium::Relation& r) {
        const bool bike = is_one_of(r.tags()["route"], {"bicycle"});
        const bool nature = is_one_of(r.tags()["type"], {"multipolygon"}) && is_nature(r.tags());
        if (!bike && !nature) {
            return;
        }
        for (const auto& m : r.members()) {
            if (m.type() != osmium::item_type::way) {
                continue;
            }
            if (bike) {
                bike_ways.push_back(m.ref());
            }
            if (nature) {
                nature_members.push_back({m.ref(), nature_shapes});
            }
        }
        if (nature) {
            ++nature_shapes;
        }
    }
};

struct WayInfo {
    std::size_t first_ref = 0;
    std::uint32_t num_refs = 0;
    RoadClass road_class{};
    Surface surface{};
    std::uint8_t flags = 0;
    bool fwd = true, bwd = true;
};

struct NatureWay {
    std::uint32_t shape;
    std::size_t first_ref;
    std::uint32_t num_refs;
};

// Second pass: keep cyclable ways with their tags, nature-area boundaries
// (closed ways and multipolygon members) and named area POIs.
struct WayCollector : osmium::handler::Handler {
    std::vector<object_id_type> bike_ways; // sorted cycle-route way IDs from the relation pass
    std::vector<std::pair<object_id_type, std::uint32_t>> nature_members;
    std::uint32_t nature_shapes = 0;
    std::vector<object_id_type> refs; // node IDs of retained ways, concatenated
    std::vector<WayInfo> ways;
    std::vector<object_id_type> nature_refs;
    std::vector<NatureWay> nature_ways;
    std::vector<RawPoi> pois;
    std::vector<object_id_type> poi_refs;

    void add_nature(const osmium::Way& way, std::uint32_t shape) {
        nature_ways.push_back({shape, nature_refs.size(), static_cast<std::uint32_t>(way.nodes().size())});
        for (const auto& nr : way.nodes()) {
            nature_refs.push_back(nr.ref());
        }
    }

    void way(const osmium::Way& way) {
        const auto& tags = way.tags();
        if (way.nodes().size() >= 4 && way.is_closed() && is_nature(tags)) {
            add_nature(way, nature_shapes++);
        }
        auto it = std::ranges::lower_bound(nature_members, std::pair{way.id(), 0u});
        for (; it != nature_members.end() && it->first == way.id(); ++it) {
            add_nature(way, it->second);
        }
        if (way.is_closed()) {
            if (const char* cat = poi_category(tags)) {
                pois.push_back(
                    {tags["name"], cat, {}, poi_refs.size(), static_cast<std::uint32_t>(way.nodes().size())});
                for (const auto& nr : way.nodes()) {
                    poi_refs.push_back(nr.ref());
                }
            }
        }

        const auto cls = bicycle_road_class(tags);
        const auto [fwd, bwd] = bicycle_directions(tags);
        if (!cls || (!fwd && !bwd)) {
            return;
        }

        WayInfo w{refs.size(), 0, *cls, parse_surface(tags), edge_flags(tags), fwd, bwd};
        if (std::ranges::binary_search(bike_ways, way.id())) {
            w.flags |= kBikeRoute;
        }
        for (const auto& nr : way.nodes()) {
            if (w.num_refs > 0 && refs.back() == nr.ref()) {
                continue; // duplicate node in source data
            }
            refs.push_back(nr.ref());
            ++w.num_refs;
        }
        if (w.num_refs < 2) {
            refs.resize(w.first_ref);
            return;
        }
        ways.push_back(w);
    }
};

// Third pass: keeps the coordinates of all nodes in ids (sorted, binary search)
// and collects named point POIs.
struct NodeCollector : osmium::handler::Handler {
    const std::vector<object_id_type>& ids;
    std::vector<LatLon>& pos;
    std::vector<std::uint8_t>& found;
    std::vector<RawPoi>& pois;

    NodeCollector(const std::vector<object_id_type>& i, std::vector<LatLon>& p, std::vector<std::uint8_t>& f,
                  std::vector<RawPoi>& po)
        : ids(i),
          pos(p),
          found(f),
          pois(po) {
    }

    void node(const osmium::Node& n) {
        if (n.location().valid() && !n.tags().empty()) {
            if (const char* cat = poi_category(n.tags())) {
                pois.push_back({n.tags()["name"], cat, {n.location().lat(), n.location().lon()}, 0, 0});
            }
        }
        const auto it = std::ranges::lower_bound(ids, n.id());
        if (it == ids.end() || *it != n.id() || !n.location().valid()) {
            return;
        }
        const auto i = static_cast<std::size_t>(it - ids.begin());
        pos[i] = {n.location().lat(), n.location().lon()};
        found[i] = 1;
    }
};

struct Segment {
    std::uint32_t a, b;
    std::uint32_t way;
    std::size_t geo_begin, geo_end;
};

struct DirEdge {
    std::uint32_t tail, head;
    std::uint32_t seg;
    bool reversed;
};

// BFS from start, 1 = reached. With backward the edges are followed in reverse,
// which gives the nodes that can reach start.
std::vector<std::uint8_t> reachable(std::uint32_t n, const std::vector<DirEdge>& edges, std::uint32_t start,
                                    bool backward) {
    std::vector<std::uint32_t> first(n + 1, 0), to(edges.size());
    for (const auto& e : edges) {
        ++first[(backward ? e.head : e.tail) + 1];
    }
    std::partial_sum(first.begin(), first.end(), first.begin());
    std::vector<std::uint32_t> fill(first.begin(), first.end() - 1);
    for (const auto& e : edges) {
        to[fill[backward ? e.head : e.tail]++] = backward ? e.tail : e.head;
    }

    std::vector<std::uint8_t> seen(n, 0);
    std::vector<std::uint32_t> queue{start};
    seen[start] = 1;
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const std::uint32_t v = queue[q];
        for (std::uint32_t k = first[v]; k < first[v + 1]; ++k) {
            if (!seen[to[k]]) {
                seen[to[k]] = 1;
                queue.push_back(to[k]);
            }
        }
    }
    return seen;
}

struct Profile {
    double length = 0, ascent = 0, descent = 0;
    double curvy_deg_km = 0;
};

// Elevation changes under 2 m are ignored, otherwise SRTM noise adds up to
// ascent that isn't there.
constexpr double kMinClimbM = 2.0;
// Sample SRTM at least every 100 m, also between distant way points.
constexpr double kMaxGapM = 100.0;

// Curviness in degrees/km. Edges under 100 m count as straight, otherwise bends
// at junctions would dominate. Capped at 2000°/km, which also fits a uint16.
constexpr double kCurvyMinLengthM = 100.0;
constexpr double kCurvyMaxDegPerKm = 2000.0;
// Smooth drawing errors < 3 m before measuring curvature.
constexpr double kCurvySmoothM = 3.0;

double heading_deg(LatLon a, LatLon b) {
    const double dx = (b.lon - a.lon) * std::cos(deg_to_rad(a.lat));
    return std::atan2(b.lat - a.lat, dx) * 180.0 / std::numbers::pi;
}

double distance_to_segment_m(LatLon p, LatLon a, LatLon b) {
    const double cos_lat = std::cos(deg_to_rad(a.lat));
    const double bx = kEarthRadiusM * deg_to_rad(b.lon - a.lon) * cos_lat;
    const double by = kEarthRadiusM * deg_to_rad(b.lat - a.lat);
    const double px = kEarthRadiusM * deg_to_rad(p.lon - a.lon) * cos_lat;
    const double py = kEarthRadiusM * deg_to_rad(p.lat - a.lat);
    const double len2 = bx * bx + by * by;
    const double t = len2 > 0 ? std::clamp((px * bx + py * by) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(px - t * bx, py - t * by);
}

// Douglas-Peucker: drops points closer than tolerance_m to the line between
// their kept neighbours. Both endpoints stay.
std::vector<LatLon> simplify(const std::vector<LatLon>& line, double tolerance_m) {
    std::vector<std::uint8_t> keep(line.size(), 0);
    keep.front() = keep.back() = 1;
    // Ranges still to check. The point farthest from the chord is kept if it is
    // further away than the tolerance.
    std::vector<std::pair<std::size_t, std::size_t>> todo{{0, line.size() - 1}};
    while (!todo.empty()) {
        const auto [from, to] = todo.back();
        todo.pop_back();
        std::size_t farthest = from;
        double far_m = tolerance_m;
        for (std::size_t i = from + 1; i < to; ++i) {
            const double d = distance_to_segment_m(line[i], line[from], line[to]);
            if (d > far_m) {
                far_m = d;
                farthest = i;
            }
        }
        if (farthest != from) {
            keep[farthest] = 1;
            todo.push_back({from, farthest});
            todo.push_back({farthest, to});
        }
    }
    std::vector<LatLon> smooth;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (keep[i]) {
            smooth.push_back(line[i]);
        }
    }
    return smooth;
}

Profile measure(const std::vector<LatLon>& line, bool bridge_or_tunnel, SrtmReader& srtm) {
    Profile p;
    for (std::size_t i = 1; i < line.size(); ++i) {
        p.length += haversine_m(line[i - 1], line[i]);
    }

    // Sum of bearing changes along the smoothed line, so sloppily drawn roads
    // don't count as curvy.
    const std::vector<LatLon> smooth = simplify(line, kCurvySmoothM);
    double turn = 0;
    for (std::size_t i = 2; i < smooth.size(); ++i) {
        double d = std::abs(heading_deg(smooth[i - 1], smooth[i]) - heading_deg(smooth[i - 2], smooth[i - 1]));
        if (d > 180) {
            d = 360 - d;
        }
        turn += d;
    }
    if (p.length >= kCurvyMinLengthM) {
        p.curvy_deg_km = std::min(turn / (p.length / 1000.0), kCurvyMaxDegPerKm);
    }

    const double h_start = srtm.elevation(line.front()).value_or(0.0);
    const double h_end = srtm.elevation(line.back()).value_or(h_start);

    // On bridges and in tunnels SRTM measures the valley or hill, not the
    // road, so only the difference between the end nodes counts there.
    if (!bridge_or_tunnel) {
        // ref = last counted elevation
        double ref = h_start;
        const auto visit = [&](LatLon q) {
            const double h = srtm.elevation(q).value_or(ref);
            if (h - ref >= kMinClimbM) {
                p.ascent += h - ref;
                ref = h;
            } else if (ref - h >= kMinClimbM) {
                p.descent += ref - h;
                ref = h;
            }
        };
        for (std::size_t i = 1; i < line.size(); ++i) {
            const int extra = static_cast<int>(haversine_m(line[i - 1], line[i]) / kMaxGapM);
            for (int k = 1; k <= extra; ++k) {
                visit(lerp(line[i - 1], line[i], k / (extra + 1.0)));
            }
            visit(line[i]);
        }
        // Add the rest at the end so that ascent - descent equals the net change.
        if (h_end > ref) {
            p.ascent += h_end - ref;
        } else {
            p.descent += ref - h_end;
        }
    } else {
        if (h_end > h_start) {
            p.ascent = h_end - h_start;
        } else {
            p.descent = h_start - h_end;
        }
    }
    return p;
}

} // namespace

Graph build_graph(const std::filesystem::path& pbf, SrtmReader& srtm) {
    // 1. Read relations, then ways (see the top of this file).
    WayCollector wc;
    {
        RelationCollector rc;
        osmium::io::Reader reader{pbf.string(), osmium::osm_entity_bits::relation};
        osmium::apply(reader, rc);
        reader.close();
        wc.bike_ways = std::move(rc.bike_ways);
        wc.nature_members = std::move(rc.nature_members);
        wc.nature_shapes = rc.nature_shapes;
        std::ranges::sort(wc.bike_ways);
        std::ranges::sort(wc.nature_members);
    }

    {
        osmium::io::Reader reader{pbf.string(), osmium::osm_entity_bits::way};
        osmium::apply(reader, wc);
        reader.close();
    }
    std::println("  bicycle ways: {}", wc.ways.size());

    // 2. Coordinates of all nodes we need (ways, nature boundaries, area POIs).
    //    ids is sorted and deduplicated, index_of() finds a node in it.
    std::vector<object_id_type> ids = wc.refs;
    ids.insert(ids.end(), wc.nature_refs.begin(), wc.nature_refs.end());
    ids.insert(ids.end(), wc.poi_refs.begin(), wc.poi_refs.end());
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const auto index_of = [&](object_id_type id) {
        return static_cast<std::size_t>(std::ranges::lower_bound(ids, id) - ids.begin());
    };

    // Nodes used more than once and way endpoints are intersections
    // (use_count = 2, no need to count higher).
    std::vector<std::uint8_t> use_count(ids.size(), 0);
    for (const auto& w : wc.ways) {
        for (std::uint32_t k = 0; k < w.num_refs; ++k) {
            auto& c = use_count[index_of(wc.refs[w.first_ref + k])];
            if (c < 2) {
                ++c;
            }
        }
        use_count[index_of(wc.refs[w.first_ref])] = 2;
        use_count[index_of(wc.refs[w.first_ref + w.num_refs - 1])] = 2;
    }

    std::vector<LatLon> pos(ids.size());
    std::vector<std::uint8_t> found(ids.size(), 0);
    {
        osmium::io::Reader reader{pbf.string(), osmium::osm_entity_bits::node};
        NodeCollector nc{ids, pos, found, wc.pois};
        osmium::apply(reader, nc);
        reader.close();
    }

    LatLon lo{90, 180}, hi{-90, -180};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!found[i]) {
            continue;
        }
        lo = {std::min(lo.lat, pos[i].lat), std::min(lo.lon, pos[i].lon)};
        hi = {std::max(hi.lat, pos[i].lat), std::max(hi.lon, pos[i].lon)};
    }
    NatureMap nature(lo, hi);
    {
        // Group segments by area so all rings are filled together.
        std::ranges::stable_sort(wc.nature_ways, {}, &NatureWay::shape);
        std::vector<std::pair<LatLon, LatLon>> border;
        for (std::size_t k = 0; k < wc.nature_ways.size(); ++k) {
            const NatureWay& nw = wc.nature_ways[k];
            for (std::uint32_t j = 1; j < nw.num_refs; ++j) {
                const std::size_t a = index_of(wc.nature_refs[nw.first_ref + j - 1]);
                const std::size_t b = index_of(wc.nature_refs[nw.first_ref + j]);
                if (found[a] && found[b]) {
                    border.push_back({pos[a], pos[b]});
                }
            }
            if (k + 1 == wc.nature_ways.size() || wc.nature_ways[k + 1].shape != nw.shape) {
                nature.fill(border);
                border.clear();
            }
        }
    }
    std::println("  nature areas: {}", wc.nature_shapes);

    // Area POIs get the mean of their boundary nodes as position.
    for (auto& poi : wc.pois) {
        if (poi.num_refs == 0) {
            continue;
        }
        double lat = 0, lon = 0;
        int n = 0;
        for (std::uint32_t j = 0; j < poi.num_refs; ++j) {
            const std::size_t i = index_of(wc.poi_refs[poi.first_ref + j]);
            if (!found[i]) {
                continue;
            }
            lat += pos[i].lat;
            lon += pos[i].lon;
            ++n;
        }
        poi.pos = n > 0 ? LatLon{lat / n, lon / n} : LatLon{0, 0};
    }

    // 3. Split the ways into segments at intersections. The points in between
    //    go into geo_pool.
    std::vector<LatLon> raw_pos;
    std::vector<std::uint32_t> node_of(ids.size(), kNoEdge);
    std::vector<LatLon> geo_pool;
    std::vector<Segment> segs;

    const auto node_for = [&](std::size_t i) {
        if (node_of[i] == kNoEdge) {
            node_of[i] = static_cast<std::uint32_t>(raw_pos.size());
            raw_pos.push_back(pos[i]);
        }
        return node_of[i];
    };
    // Segment a -> b. A closed loop (a == b) is split in the middle so there are
    // no self-loops.
    const auto add_segment = [&](std::uint32_t a, std::uint32_t b, std::uint32_t way, std::size_t gb, std::size_t ge) {
        if (a != b) {
            segs.push_back({a, b, way, gb, ge});
            return;
        }
        if (gb == ge) {
            return;
        }
        const std::size_t mid = gb + (ge - gb) / 2;
        const auto m = static_cast<std::uint32_t>(raw_pos.size());
        raw_pos.push_back(geo_pool[mid]);
        segs.push_back({a, m, way, gb, mid});
        segs.push_back({m, b, way, mid + 1, ge});
    };

    for (std::uint32_t wi = 0; wi < wc.ways.size(); ++wi) {
        const WayInfo& w = wc.ways[wi];
        std::optional<std::uint32_t> start;
        std::size_t geo_begin = geo_pool.size();
        for (std::uint32_t k = 0; k < w.num_refs; ++k) {
            const std::size_t i = index_of(wc.refs[w.first_ref + k]);
            if (!found[i]) {
                continue;
            }
            if (!start) {
                start = node_for(i);
                geo_begin = geo_pool.size();
            } else if (use_count[i] >= 2 || k + 1 == w.num_refs) {
                const std::uint32_t end = node_for(i);
                add_segment(*start, end, wi, geo_begin, geo_pool.size());
                start = end;
                geo_begin = geo_pool.size();
            } else {
                geo_pool.push_back(pos[i]);
            }
        }
    }

    std::vector<DirEdge> edges;
    for (std::uint32_t s = 0; s < segs.size(); ++s) {
        const WayInfo& w = wc.ways[segs[s].way];
        if (w.fwd) {
            edges.push_back({segs[s].a, segs[s].b, s, false});
        }
        if (w.bwd) {
            edges.push_back({segs[s].b, segs[s].a, s, true});
        }
    }
    const auto n_raw = static_cast<std::uint32_t>(raw_pos.size());
    std::println("  junction nodes: {}, directed edges: {}", n_raw, edges.size());

    // 4. Keep only the main strongly connected network. Isolated tracks and
    //    one-way dead ends are useless for round trips. The component of a seed
    //    is everything reachable both from and to it; if that covers more than
    //    half of the nodes it is the main network, otherwise try another seed.
    std::vector<std::uint8_t> in_main(n_raw, 0);
    std::vector<std::uint8_t> tried(n_raw, 0);
    bool found_main = false;
    for (std::uint32_t start = 0; start < n_raw && !found_main; ++start) {
        if (tried[start]) {
            continue;
        }
        const auto fwd = reachable(n_raw, edges, start, false);
        const auto bwd = reachable(n_raw, edges, start, true);
        std::uint32_t size = 0;
        for (std::uint32_t v = 0; v < n_raw; ++v) {
            in_main[v] = fwd[v] && bwd[v];
            if (in_main[v]) {
                ++size;
                tried[v] = 1;
            }
        }
        found_main = size > n_raw / 2;
    }
    if (!found_main) {
        throw std::runtime_error("no connected main network found");
    }

    // 5. Build the CSR graph with geometry, length, climb, curviness and
    //    nature flag per edge, then pair opposite edges as twins.
    Graph g;
    std::vector<std::uint32_t> new_id(n_raw, kNoEdge);
    for (std::uint32_t v = 0; v < n_raw; ++v) {
        if (in_main[v]) {
            new_id[v] = g.num_nodes();
            g.node_pos.push_back(raw_pos[v]);
        }
    }
    std::erase_if(edges, [&](const DirEdge& e) {
        return new_id[e.tail] == kNoEdge || new_id[e.head] == kNoEdge;
    });
    for (auto& e : edges) {
        e.tail = new_id[e.tail];
        e.head = new_id[e.head];
    }
    std::println("  main network: {} of {} nodes", g.num_nodes(), n_raw);

    std::ranges::stable_sort(edges, {}, &DirEdge::tail);
    const std::uint32_t n = g.num_nodes();
    const auto m = static_cast<std::uint32_t>(edges.size());
    g.first_out.assign(n + 1, 0);
    for (const auto& e : edges) {
        ++g.first_out[e.tail + 1];
    }
    std::partial_sum(g.first_out.begin(), g.first_out.end(), g.first_out.begin());

    g.node_elevation_m.resize(n);
    for (std::uint32_t v = 0; v < n; ++v) {
        g.node_elevation_m[v] = static_cast<float>(srtm.elevation(g.node_pos[v]).value_or(0.0));
    }

    g.head.resize(m);
    g.twin.assign(m, kNoEdge);
    g.length_m.resize(m);
    g.ascent_m.resize(m);
    g.descent_m.resize(m);
    g.curvy_deg_km.resize(m);
    g.road_class.resize(m);
    g.surface.resize(m);
    g.flags.resize(m);
    g.geo_first.resize(m + 1);

    std::vector<std::uint32_t> fwd_edge(segs.size(), kNoEdge), bwd_edge(segs.size(), kNoEdge);
    std::vector<LatLon> line;

    for (std::uint32_t e = 0; e < m; ++e) {
        const DirEdge& d = edges[e];
        const Segment& s = segs[d.seg];
        const WayInfo& w = wc.ways[s.way];
        (d.reversed ? bwd_edge : fwd_edge)[d.seg] = e;

        g.head[e] = d.head;
        g.road_class[e] = w.road_class;
        g.surface[e] = w.surface;
        g.flags[e] = w.flags;

        g.geo_first[e] = static_cast<std::uint32_t>(g.geo_points.size());
        const auto first = geo_pool.begin() + static_cast<std::ptrdiff_t>(s.geo_begin);
        const auto last = geo_pool.begin() + static_cast<std::ptrdiff_t>(s.geo_end);
        if (d.reversed) {
            g.geo_points.insert(g.geo_points.end(), std::make_reverse_iterator(last),
                                std::make_reverse_iterator(first));
        } else {
            g.geo_points.insert(g.geo_points.end(), first, last);
        }

        line.assign(1, g.node_pos[d.tail]);
        line.insert(line.end(), g.geo_points.begin() + g.geo_first[e], g.geo_points.end());
        line.push_back(g.node_pos[d.head]);
        const bool bridge_or_tunnel = (w.flags & (kBridge | kTunnel)) != 0;
        const Profile p = measure(line, bridge_or_tunnel, srtm);
        // Store intermediate elevations for profiles and GPX. On bridges,
        // in tunnels and where SRTM is missing, interpolate endpoint heights.
        const double h_tail = g.node_elevation_m[d.tail], h_head = g.node_elevation_m[d.head];
        double along = 0;
        for (std::size_t i = 1; i + 1 < line.size(); ++i) {
            along += haversine_m(line[i - 1], line[i]);
            const double flat = h_tail + (h_head - h_tail) * (p.length > 0 ? along / p.length : 0);
            const double h = bridge_or_tunnel ? flat : srtm.elevation(line[i]).value_or(flat);
            g.geo_elevation_m.push_back(static_cast<float>(h));
        }
        g.length_m[e] = static_cast<float>(p.length);
        g.ascent_m[e] = static_cast<float>(p.ascent);
        g.descent_m[e] = static_cast<float>(p.descent);
        g.curvy_deg_km[e] = static_cast<std::uint16_t>(p.curvy_deg_km);
        const LatLon mid = line.size() > 2 ? line[line.size() / 2] : lerp(line.front(), line.back(), 0.5);
        if (nature.contains(mid)) {
            g.flags[e] |= kNature;
        }
    }
    g.geo_first[m] = static_cast<std::uint32_t>(g.geo_points.size());

    std::uint32_t oneway = 0;
    for (std::size_t s = 0; s < segs.size(); ++s) {
        if (fwd_edge[s] != kNoEdge && bwd_edge[s] != kNoEdge) {
            g.twin[fwd_edge[s]] = bwd_edge[s];
            g.twin[bwd_edge[s]] = fwd_edge[s];
        } else if (fwd_edge[s] != kNoEdge || bwd_edge[s] != kNoEdge) {
            g.flags[fwd_edge[s] != kNoEdge ? fwd_edge[s] : bwd_edge[s]] |= kOneway;
            ++oneway;
        }
    }
    std::uint32_t bike = 0, curvy = 0, nat = 0;
    for (const auto f : g.flags) {
        bike += (f & kBikeRoute) != 0;
        nat += (f & kNature) != 0;
    }
    for (const auto c : g.curvy_deg_km) {
        curvy += c >= 500;
    }
    std::println("  graph: {} nodes, {} edges ({} one-way, {} bike route, {} curvy, {} nature)", n, m, oneway, bike,
                 curvy, nat);

    // 6. Snap POIs to the network. POIs more than 300 m away are dropped, and so
    //    are duplicates with the same name on one node.
    constexpr double kMaxPoiSnapM = 300.0;
    const SpatialIndex index(g);
    std::set<std::pair<std::uint32_t, std::string>> seen_pois;
    g.poi_name_first.push_back(0);
    g.poi_category_first.push_back(0);
    for (const auto& poi : wc.pois) {
        const std::uint32_t v = index.nearest(poi.pos);
        if (v == kNoEdge || haversine_m(poi.pos, g.node_pos[v]) > kMaxPoiSnapM) {
            continue;
        }
        if (!seen_pois.insert({v, poi.name}).second) {
            continue;
        }
        g.poi_node.push_back(v);
        g.poi_names.insert(g.poi_names.end(), poi.name.begin(), poi.name.end());
        g.poi_name_first.push_back(static_cast<std::uint32_t>(g.poi_names.size()));
        g.poi_categories.insert(g.poi_categories.end(), poi.category.begin(), poi.category.end());
        g.poi_category_first.push_back(static_cast<std::uint32_t>(g.poi_categories.size()));
    }
    std::println("  POIs: {} of {} found", g.num_pois(), wc.pois.size());
    return g;
}

} // namespace rp
