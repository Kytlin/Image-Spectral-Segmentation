#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>

#include "image_io.hpp"
#include "preprocess.hpp"

namespace {

// Day 5 turns these into command-line flags.
constexpr int kTargetWidth = 160;
constexpr double kSigma = 1.0;

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <image>\n";
        return 1;
    }

    try {
        auto start = Clock::now();
        const Image img = load_image(argv[1]);
        std::cout << argv[1] << ": " << img.width << " x " << img.height << " x "
                  << img.channels << "  (load " << ms_since(start) << " ms)\n";

        start = Clock::now();
        const int factor = std::max(1, img.width / kTargetWidth);
        const Image small = downsample(img, factor);
        std::cout << "downsample x" << factor << ": " << small.width << " x " << small.height
                  << "  (" << ms_since(start) << " ms)\n";

        start = Clock::now();
        const Image blurred = gaussian_blur(small, kSigma);
        std::cout << "blur sigma=" << kSigma << "  (" << ms_since(start) << " ms)\n";

        std::filesystem::create_directories("out");
        save_png("out/small.png", small);
        save_png("out/blurred.png", blurred);
        std::cout << "wrote out/small.png, out/blurred.png\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
