#pragma once

#include <vector>

// An 8-bit image that owns its pixels. Copying an Image copies the pixels;
// the vector frees them automatically when the Image goes out of scope.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Row-major, channels interleaved: pixel (x, y), channel c is at (y*width + x)*channels + c.
    std::vector<unsigned char> pixels;
};
