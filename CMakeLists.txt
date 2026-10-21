cmake_minimum_required(VERSION 3.28)
project(spectral_segmentation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build; Eigen is very slow without -O2.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# The numerical core, shared by the `segment` program and the `rqi_bench` experiments.
add_library(segcore STATIC
  src/image_io.cpp src/preprocess.cpp src/graph.cpp src/eigensolver.cpp src/clustering.cpp)
target_compile_options(segcore PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(segcore PUBLIC src)
# SYSTEM silences warnings from third-party headers we don't own.
target_include_directories(segcore SYSTEM PRIVATE third_party)
target_link_libraries(segcore PUBLIC Eigen3::Eigen)

add_executable(segment src/main.cpp)
target_compile_options(segment PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(segment PRIVATE segcore)

add_executable(rqi_bench src/rqi_experiments.cpp)
target_compile_options(rqi_bench PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rqi_bench PRIVATE segcore)

# HTTP server for the web frontend. Needs libpq (apt: libpq-dev). Skipped if libpq isn't found,
# so the CLI and the experiments still build without it.
find_package(PostgreSQL)
find_package(Threads REQUIRED)
if(PostgreSQL_FOUND)
  add_executable(seg_server
    src/server/main.cpp src/server/base64.cpp src/server/database.cpp src/server/run_cache.cpp)
  target_compile_options(seg_server PRIVATE -Wall -Wextra -Wpedantic)
  target_include_directories(seg_server SYSTEM PRIVATE third_party)
  target_link_libraries(seg_server PRIVATE segcore PostgreSQL::PostgreSQL Threads::Threads)
else()
  message(STATUS "libpq not found: skipping seg_server")
endif()
