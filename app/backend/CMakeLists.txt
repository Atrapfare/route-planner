cmake_minimum_required(VERSION 3.28)

project(route_planner
    VERSION 0.1.0
    DESCRIPTION "Route planner backend"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Dependencies ------------------------------------------------------------
# libosmium and protozero are header-only. We only download the sources and use
# their include directories (SOURCE_SUBDIR points to a non-existing folder so
# that their own CMake projects are not configured).
include(FetchContent)

FetchContent_Declare(protozero
    URL https://github.com/mapbox/protozero/archive/refs/tags/v1.8.0.tar.gz
    SOURCE_SUBDIR do-not-configure
)
FetchContent_Declare(libosmium
    URL https://github.com/osmcode/libosmium/archive/refs/tags/v2.22.0.tar.gz
    SOURCE_SUBDIR do-not-configure
)
# cpp-httplib: single-header HTTP server (httplib.h).
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    SOURCE_SUBDIR do-not-configure
)
FetchContent_MakeAvailable(protozero libosmium httplib)

# With MSYS2/MinGW, zlib and the other libraries live next to the compiler.
# A CMake installed outside of MSYS2 does not search there by default, so we
# add the compiler's prefix to the search path.
if(MINGW)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _compiler_bin_dir)
    cmake_path(GET _compiler_bin_dir PARENT_PATH _compiler_prefix)
    list(APPEND CMAKE_PREFIX_PATH "${_compiler_prefix}")
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# --- Program -----------------------------------------------------------------
add_executable(route_planner
    src/main.cpp
    src/graph/graph_io.cpp
    src/import/osm_import.cpp
    src/import/nature_map.cpp
    src/import/srtm.cpp
    src/routing/cost_model.cpp
    src/routing/dijkstra.cpp
    src/routing/spatial_index.cpp
    src/routing/round_trip.cpp
    src/routing/polygon_method.cpp
    src/routing/isochrone_method.cpp
    src/routing/local_search.cpp
    src/routing/landmarks.cpp
    src/server/server.cpp
    src/eval/measure.cpp
)

target_include_directories(route_planner PRIVATE include)
target_include_directories(route_planner SYSTEM PRIVATE
    ${protozero_SOURCE_DIR}/include
    ${libosmium_SOURCE_DIR}/include
    ${httplib_SOURCE_DIR}
)
target_link_libraries(route_planner PRIVATE ZLIB::ZLIB Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_executable(routing_smoke
        tests/routing_smoke.cpp
        src/routing/cost_model.cpp
        src/routing/dijkstra.cpp
        src/routing/landmarks.cpp
        src/routing/spatial_index.cpp
        src/routing/round_trip.cpp
    )
    target_include_directories(routing_smoke PRIVATE include)
    target_link_libraries(routing_smoke PRIVATE Threads::Threads)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(routing_smoke PRIVATE stdc++exp)
    endif()
    add_test(NAME routing_smoke COMMAND routing_smoke)
endif()

# cpp-httplib needs Windows socket libraries.
if(WIN32)
    target_link_libraries(route_planner PRIVATE ws2_32 crypt32)
endif()

if(MSVC)
    target_compile_options(route_planner PRIVATE /W4 /permissive-)
else()
    target_compile_options(route_planner PRIVATE -Wall -Wextra -Wpedantic)
endif()

# libstdc++ on MinGW ships the Windows terminal support for std::print in a separate library
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(route_planner PRIVATE stdc++exp)
endif()

# MinGW uses a 32-bit st_size in fstat/stat by default. For .osm.pbf files over
# 4 GB libosmium then fails with "Could not get file size".
if(MINGW)
    target_compile_definitions(route_planner PRIVATE _FILE_OFFSET_BITS=64)
endif()
