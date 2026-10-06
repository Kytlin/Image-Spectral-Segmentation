#pragma once

#include <string>

#include "types.hpp"

// Loads a JPEG/PNG/BMP as 3-channel RGB (any alpha channel is dropped).
// Throws std::runtime_error if the file can't be read or decoded.
Image load_image(const std::string& path);

// Writes img as a PNG. Throws std::runtime_error on failure.
void save_png(const std::string& path, const Image& img);
