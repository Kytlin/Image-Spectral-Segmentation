#pragma once

#include <vector>

#include "types.hpp"

// Shrinks img by an integer factor, averaging each factor * factor block into one pixel.
// Leftover rows/columns that don't fill a whole block are omitted.
Image downsample(const Image& img, int factor);

// Normalized 1-D Gaussian weights for offsets [-r, r], where r = ceil(3*sigma).
// sigma <= 0 gives {1.0}, i.e. no blur.
std::vector<double> gaussian_kernel(double sigma);

// Separable Gaussian blur: a horizontal pass then a vertical pass, clamping at the edges.
Image gaussian_blur(const Image& img, double sigma);
