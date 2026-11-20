# Bicycle round-trip planner

Generates cycling loops of a given length from a start point, tuned by five preference sliders (hills, bike infrastructure, nature, unpaved, curvy). POIs or map waypoints can be included. C++23 backend on OpenStreetMap + SRTM data, Vue/TypeScript frontend with alternative routes, elevation profiles and GPX export.

## Requirements

- CMake 3.28+, Ninja, zlib, C++23 compiler with `<print>` (GCC 14+ / Clang 18+; Windows: MSYS2 UCRT64). The first configure downloads libosmium, protozero and cpp-httplib.
- Node.js 22.18+ or 24.12+
- An OSM `.osm.pbf` extract in `app/backend/data/osm/graphs/` (not included; e.g. from [Geofabrik](https://download.geofabrik.de/europe/germany/) or the [Baden-Württemberg from Google Drive in osm/ folder](https://drive.google.com/drive/folders/1oJ1c8PuYdnWFJInf03jBGaJt-2nqBwWa?usp=sharing))
- SRTM tiles for Germany and surroundings are included in `app/backend/data/srtm/`; for other regions add tiles or use `--srtm DIR`.

## Run

```bash
node app/start --pbf baden-wuerttemberg-261115
```

`--pbf` takes the filename without `.osm.pbf`. Builds the backend, loads or builds the graph and ALT landmarks, and opens the UI at <http://localhost:5173>; backend on `127.0.0.1:8080` (change with `--port N`).

```bash
node app/start build          # build only
node app/start clean          # remove builds, caches, node_modules
node app/start help           # all commands and options
```

Graph and landmarks are cached as `app/backend/data/<region>.graph.bin` / `.landmarks.bin`. Use `--rebuild` after replacing an OSM file under the same name.

## License

The code is released under the [MIT License](LICENSE). The data it works with is not covered by it:

- OpenStreetMap data and map tiles: © OpenStreetMap contributors, available under the [Open Database License](https://www.openstreetmap.org/copyright).
- SRTM tiles in `app/backend/data/srtm/`: CGIAR-CSI SRTM 90m v4.1, subject to the CGIAR-CSI terms of use. Jarvis, A., H.I. Reuter, A. Nelson, E. Guevara (2008): Hole-filled seamless SRTM data V4, International Centre for Tropical Agriculture (CIAT), <https://srtm.csi.cgiar.org>.
