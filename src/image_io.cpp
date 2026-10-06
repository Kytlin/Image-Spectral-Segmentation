#include "image_io.hpp"

#include <memory>
#include <stdexcept>

// The stb headers contain both declarations and definitions. These macros switch the
// definitions on, and must appear in exactly one .cpp file (the one-definition rule).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Image load_image(const std::string& path) {
    constexpr int kChannels = 3;
    int width = 0, height = 0, channels_in_file = 0;

    // stb allocates with malloc, so the unique_ptr must free with stbi_image_free, not delete.
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
        stbi_load(path.c_str(), &width, &height, &channels_in_file, kChannels), stbi_image_free);
    if (data == nullptr) {
        throw std::runtime_error("failed to load " + path + ": " + stbi_failure_reason());
    }

    // Widen before multiplying so the byte count can't overflow int.
    const std::size_t num_bytes = static_cast<std::size_t>(width) * height * kChannels;
    return Image{width, height, kChannels,
                 std::vector<unsigned char>(data.get(), data.get() + num_bytes)};
}

void save_png(const std::string& path, const Image& img) {
    const int stride_bytes = img.width * img.channels;
    if (stbi_write_png(path.c_str(), img.width, img.height, img.channels, img.pixels.data(),
                       stride_bytes) == 0) {
        throw std::runtime_error("failed to write " + path);
    }
}
