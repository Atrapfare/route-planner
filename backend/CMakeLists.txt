cmake_minimum_required(VERSION 3.16)
project(routeplanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(routeplanner
    src/main.cpp
    src/graph.cpp
    src/dijkstra.cpp
    src/nearest_neighbor.cpp
    src/server.cpp
)

# Header-Verzeichnisse bekannt machen
target_include_directories(routeplanner PRIVATE
    include
    lib
)

# httplib braucht Threads
find_package(Threads REQUIRED)
target_link_libraries(routeplanner Threads::Threads)
