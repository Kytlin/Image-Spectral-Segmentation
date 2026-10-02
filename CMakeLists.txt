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

add_executable(segment src/main.cpp)
target_compile_options(segment PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(segment PRIVATE Eigen3::Eigen)
