#include <exception>
#include <filesystem>
#include <iostream>

#include "image_io.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <image>\n";
        return 1;
    }

    try {
        const Image img = load_image(argv[1]);
        std::cout << argv[1] << ": " << img.width << " x " << img.height << " x "
                  << img.channels << "\n";

        std::filesystem::create_directories("out");
        save_png("out/copy.png", img);
        std::cout << "wrote out/copy.png\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
