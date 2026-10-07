#include "graph.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <unsupported/Eigen/SparseExtra>  // Eigen::saveMarket

namespace {

// Squared Euclidean distance between the RGB colours of pixels a and b, with each
// channel scaled from [0, 255] to [0, 1]. The result is in [0, 3].
double squared_color_distance(const Image& image, int a, int b) {
    double sum = 0.0;
    for (int channel = 0; channel < image.channels; ++channel) {
        const double value_a = image.pixels[static_cast<std::size_t>(a) * image.channels + channel];
        const double value_b = image.pixels[static_cast<std::size_t>(b) * image.channels + channel];
        const double difference = (value_a - value_b) / 255.0;
        sum += difference * difference;
    }
    return sum;
}

}  // namespace

SparseMatrix build_affinity(const Image& image, double sigma_color, double threshold) {
    if (sigma_color <= 0.0) {
        throw std::invalid_argument("build_affinity: sigma_color must be > 0");
    }

    const int width = image.width;
    const int height = image.height;
    const int num_nodes = width * height;

    // A triplet is one nonzero entry (row, column, value). We collect them all,
    // then Eigen sorts them into compressed column storage in one go.
    std::vector<Eigen::Triplet<double>> entries;
    entries.reserve(static_cast<std::size_t>(num_nodes) * 8);  // at most 8 neighbours per node

    // The 4 "forward" neighbours (dx, dy): right, and the three below. The other 4 neighbours
    // (left, and the three above) are covered when that neighbour visits us, so each pair
    // {i, j} is seen exactly once, and we push both (i, j) and (j, i) to keep W symmetric.
    constexpr int forward_offsets[4][2] = {{1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    const double inverse_sigma_squared = 1.0 / (sigma_color * sigma_color);

    // y in the outer loop, so we walk the pixel buffer in memory order (row by row).
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int node = y * width + x;

            for (const auto& offset : forward_offsets) {
                const int neighbor_x = x + offset[0];
                const int neighbor_y = y + offset[1];
                const bool inside_image = neighbor_x >= 0 && neighbor_x < width &&
                                          neighbor_y >= 0 && neighbor_y < height;
                if (!inside_image) {
                    continue;
                }

                const int neighbor = neighbor_y * width + neighbor_x;
                const double distance_squared = squared_color_distance(image, node, neighbor);
                const double weight = std::exp(-distance_squared * inverse_sigma_squared);
                if (weight < threshold) {
                    continue;
                }

                // Triplets are (row, column, value) in node indices, not pixel coordinates.
                entries.emplace_back(node, neighbor, weight);
                entries.emplace_back(neighbor, node, weight);
            }
        }
    }

    SparseMatrix affinity(num_nodes, num_nodes);
    affinity.setFromTriplets(entries.begin(), entries.end());

    return affinity;
}

Eigen::VectorXd degrees(const SparseMatrix& affinity) {
    // Multiplying by the all-ones vector sums each row.
    return affinity * Eigen::VectorXd::Ones(affinity.cols());
}

SparseMatrix normalized_affinity(const SparseMatrix& affinity, const Eigen::VectorXd& degree) {
    // d_i^{-1/2}, with 0 for isolated nodes instead of dividing by zero.
    Eigen::VectorXd inverse_sqrt_degree(degree.size());
    for (Eigen::Index i = 0; i < degree.size(); ++i) {
        inverse_sqrt_degree[i] = degree[i] > 0.0 ? 1.0 / std::sqrt(degree[i]) : 0.0;
    }

    // Scaling by a diagonal matrix on the left scales rows, and on the right scales columns,
    // so entry (i, j) becomes w_ij / sqrt(d_i * d_j). The result stays sparse.
    const auto scale = inverse_sqrt_degree.asDiagonal();
    return scale * affinity * scale;
}

void save_matrix_market(const std::string& path, const SparseMatrix& matrix) {
    if (!Eigen::saveMarket(matrix, path)) {
        throw std::runtime_error("failed to write " + path);
    }
}
