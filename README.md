# Route Planner

Shortest-path routing on real road networks, using OpenStreetMap data.
Click a source and a target on the map — the route gets computed and drawn instantly.

## Requirements

- CMake 3.16+
- A C++20 compiler (MSVC, GCC, Clang)
- A browser

## Build

**Linux / Mac:**
```bash
cd backend
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Windows:**
```bash
cd backend
cmake -B build
cmake --build build --config Release
```

## Run

Pass the path to an FMI graph file as the only argument:

**Linux / Mac:**
```bash
./build/routeplanner ./graphs/germany.fmi
```

**Windows:**
```bash
.\build\Release\routeplanner.exe ..\graphs\germany.fmi
```

Graph files in FMI format can be downloaded from https://fmi.uni-stuttgart.de/alg/research/stuff/

Once the server is running, open your browser at:

```
http://localhost:8080
```

## Usage

1. Click anywhere on the map to set the **source** (green marker).
2. Click again to set the **target** (red marker) — the route is computed automatically.
3. Click **Reset** to start over.

If you click outside the loaded graph region, an error message will tell you the valid bounds.
