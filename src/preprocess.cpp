#include "preprocess.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

std::size_t pixel_index(
    int x,
    int y,
    int channel,
    int width,
    int channels) {
    return (static_cast<std::size_t>(y) * width + x) * channels + channel;
}

unsigned char to_byte(double value) {
    value = std::clamp(value, 0.0, 255.0);
    return static_cast<unsigned char>(std::lround(value));
}

} // namespace

Image downsample(const Image& image, int factor) {
    if (factor < 1) {
        throw std::invalid_argument("downsample: factor must be >= 1");
    }

    if (factor > image.width || factor > image.height) {
        throw std::invalid_argument(
            "downsample: factor is larger than image dimensions");
    }

    const int output_width = image.width / factor;
    const int output_height = image.height / factor;

    Image result{
        output_width,
        output_height,
        image.channels,
        {}
    };

    result.pixels.resize(
        static_cast<std::size_t>(output_width)
        * output_height
        * image.channels
    );

    const double pixels_per_block = static_cast<double>(factor * factor);

    for (int output_y = 0; output_y < output_height; ++output_y) {
        for (int output_x = 0; output_x < output_width; ++output_x) {

            // Every color channel is averaged independently.
            for (int channel = 0; channel < image.channels; ++channel) {
                double block_sum = 0.0;
                for (int block_y = 0; block_y < factor; ++block_y) {
                    for (int block_x = 0; block_x < factor; ++block_x) {
                        const int source_x =
                            output_x * factor + block_x;
                        const int source_y =
                            output_y * factor + block_y;
                        block_sum += image.pixels[
                            pixel_index(
                                source_x,
                                source_y,
                                channel,
                                image.width,
                                image.channels
                            )
                        ];
                    }
                }

                const double block_average = block_sum / pixels_per_block;
                result.pixels[
                    pixel_index(
                        output_x,
                        output_y,
                        channel,
                        result.width,
                        result.channels
                    )
                ] = to_byte(block_average);
            }
        }
    }

    return result;
}

std::vector<double> gaussian_kernel(double sigma) {
    if (sigma <= 0.0) {
        return {1.0};
    }

    const int radius = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<double> weights(2 * radius + 1);
    double weight_sum = 0.0;

    for (int offset = -radius; offset <= radius; ++offset) {
        const double weight = std::exp(-(static_cast<double>(offset * offset))
            / (2.0 * sigma * sigma));

        weights[offset + radius] = weight;
        weight_sum += weight;
    }

    for (double& weight : weights) {
        weight /= weight_sum;
    }

    return weights;
}

Image gaussian_blur(const Image& image, double sigma) {
    const std::vector<double> kernel =
        gaussian_kernel(sigma);

    const int radius =
        static_cast<int>(kernel.size() / 2);

    const int width = image.width;
    const int height = image.height;
    const int channels = image.channels;

    /*
     * PASS 1: horizontal blur
     *
     * Store floating-point values rather than unsigned chars.
     * Otherwise we would round once here and then round again
     * after the vertical pass.
     */
    std::vector<double> horizontally_blurred(
        image.pixels.size(),
        0.0
    );

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int channel = 0; channel < channels; ++channel) {

                double weighted_sum = 0.0;

                for (int x_offset = -radius;
                     x_offset <= radius;
                     ++x_offset) {

                    // "Clamp at the edges":
                    // x = -1 becomes x = 0,
                    // x = width becomes x = width - 1.
                    const int neighboring_x =
                        std::clamp(
                            x + x_offset,
                            0,
                            width - 1
                        );

                    const double weight =
                        kernel[x_offset + radius];

                    const unsigned char source_value =
                        image.pixels[
                            pixel_index(
                                neighboring_x,
                                y,
                                channel,
                                width,
                                channels
                            )
                        ];

                    weighted_sum +=
                        weight * source_value;
                }

                horizontally_blurred[
                    pixel_index(
                        x,
                        y,
                        channel,
                        width,
                        channels
                    )
                ] = weighted_sum;
            }
        }
    }


    /*
     * PASS 2: vertical blur
     *
     * Now apply exactly the same 1-D kernel vertically,
     * but to the horizontally blurred image.
     */
    Image result{
        width,
        height,
        channels,
        std::vector<unsigned char>(image.pixels.size())
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int channel = 0; channel < channels; ++channel) {

                double weighted_sum = 0.0;

                for (int y_offset = -radius;
                     y_offset <= radius;
                     ++y_offset) {

                    const int neighboring_y =
                        std::clamp(
                            y + y_offset,
                            0,
                            height - 1
                        );

                    const double weight =
                        kernel[y_offset + radius];

                    const double horizontally_blurred_value =
                        horizontally_blurred[
                            pixel_index(
                                x,
                                neighboring_y,
                                channel,
                                width,
                                channels
                            )
                        ];

                    weighted_sum +=
                        weight * horizontally_blurred_value;
                }

                result.pixels[
                    pixel_index(
                        x,
                        y,
                        channel,
                        width,
                        channels
                    )
                ] = to_byte(weighted_sum);
            }
        }
    }

    return result;
}
