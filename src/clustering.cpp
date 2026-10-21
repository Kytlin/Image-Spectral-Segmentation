#include "clustering.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>

Eigen::MatrixXd spectral_embedding(const Eigen::MatrixXd& eigenvectors) {
    // Row i holds pixel i's entry in each eigenvector. Scale every row to unit length.
    Eigen::MatrixXd embedding = eigenvectors;
    for (Eigen::Index row_idx = 0; row_idx < embedding.rows(); ++row_idx) {
        const double length = embedding.row(row_idx).norm();
        if (length > 0.0) {
            embedding.row(row_idx) /= length;
        }
    }
    return embedding;
}

namespace {

double squared_distance_to_nearest(const Eigen::RowVectorXd& point, const Eigen::MatrixXd& centres,
                                   int num_centres, int& nearest_cluster) {
    double nearest_distance = std::numeric_limits<double>::infinity();
    nearest_cluster = 0;
    for (int cluster = 0; cluster < num_centres; ++cluster) {
        const double distance = (point - centres.row(cluster)).squaredNorm();
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_cluster = cluster;
        }
    }
    return nearest_distance;
}

// k-means++ seeding: the first centre is a random point; each next centre is a random point
// picked with probability proportional to its squared distance to the nearest centre so far.
Eigen::MatrixXd kmeans_plus_plus_centres(const Eigen::MatrixXd& points, int k,
                                         std::mt19937& generator) {
    const Eigen::Index num_points = points.rows();
    Eigen::MatrixXd centres(k, points.cols());

    std::uniform_int_distribution<Eigen::Index> any_point(0, num_points - 1);
    centres.row(0) = points.row(any_point(generator));

    std::vector<double> squared_distances(static_cast<std::size_t>(num_points));
    for (int chosen = 1; chosen < k; ++chosen) {
        double total = 0.0;
        int unused_cluster = 0;
        for (Eigen::Index point = 0; point < num_points; ++point) {
            squared_distances[point] =
                squared_distance_to_nearest(points.row(point), centres, chosen, unused_cluster);
            total += squared_distances[point];
        }
        // All points already coincide with a centre: fall back to a uniform pick.
        if (total == 0.0) {
            centres.row(chosen) = points.row(any_point(generator));
            continue;
        }
        std::discrete_distribution<Eigen::Index> weighted_point(squared_distances.begin(),
                                                                squared_distances.end());
        centres.row(chosen) = points.row(weighted_point(generator));
    }
    return centres;
}

// Lloyd's algorithm from the given centres. Fills `labels` and `iterations_used`, and returns
// the inertia: the sum of squared distances from each point to its centre (lower is better).
double lloyd(const Eigen::MatrixXd& points, Eigen::MatrixXd centres, int max_iterations,
             std::vector<int>& labels, int& iterations_used) {
    const Eigen::Index num_points = points.rows();
    const int k = static_cast<int>(centres.rows());
    labels.assign(static_cast<std::size_t>(num_points), -1);
    iterations_used = 0;

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        ++iterations_used;
        // ---- Step 1: assign each point to its nearest centre. ----
        bool any_label_changed = false;
        for (Eigen::Index point = 0; point < num_points; ++point) {
            int nearest_cluster = 0;
            squared_distance_to_nearest(points.row(point), centres, k, nearest_cluster);
            if (labels[point] != nearest_cluster) {
                labels[point] = nearest_cluster;
                any_label_changed = true;
            }
        }
        if (!any_label_changed) {
            break;  // converged: step 2 would leave the centres unchanged
        }

        // ---- Step 2: move each centre to the mean of its points. ----
        Eigen::MatrixXd sums = Eigen::MatrixXd::Zero(k, points.cols());
        std::vector<int> counts(static_cast<std::size_t>(k), 0);
        for (Eigen::Index point = 0; point < num_points; ++point) {
            sums.row(labels[point]) += points.row(point);
            ++counts[labels[point]];
        }
        for (int cluster = 0; cluster < k; ++cluster) {
            // An empty cluster keeps its old centre.
            if (counts[cluster] > 0) {
                centres.row(cluster) = sums.row(cluster) / counts[cluster];
            }
        }
    }

    double inertia = 0.0;
    for (Eigen::Index point = 0; point < num_points; ++point) {
        inertia += (points.row(point) - centres.row(labels[point])).squaredNorm();
    }
    return inertia;
}

}  // namespace

std::vector<int> kmeans(const Eigen::MatrixXd& points, int k, unsigned seed, int max_iterations,
                        int restarts, int* iterations) {
    if (k < 1 || k > points.rows()) {
        throw std::invalid_argument("kmeans: need 1 <= k <= number of points");
    }
    if (restarts < 1) {
        throw std::invalid_argument("kmeans: need restarts >= 1");
    }

    std::mt19937 generator(seed);
    std::vector<int> best_labels;
    double best_inertia = std::numeric_limits<double>::infinity();
    std::vector<int> labels;
    int iterations_used = 0;

    for (int attempt = 0; attempt < restarts; ++attempt) {
        const Eigen::MatrixXd centres = kmeans_plus_plus_centres(points, k, generator);
        const double inertia = lloyd(points, centres, max_iterations, labels, iterations_used);
        if (inertia < best_inertia) {
            best_inertia = inertia;
            best_labels = labels;
            if (iterations != nullptr) {
                *iterations = iterations_used;
            }
        }
    }
    return best_labels;
}

Image render_segments(const std::vector<int>& labels, const Image& image) {
    const std::size_t num_pixels = static_cast<std::size_t>(image.width) * image.height;
    if (labels.size() != num_pixels) {
        throw std::invalid_argument("render_segments: one label per pixel expected");
    }

    // Labels are 0 .. max_label. Sum up each cluster's colours.
    int max_label = 0;
    for (int label : labels) {
        max_label = std::max(max_label, label);
    }
    const int num_clusters = max_label + 1;
    std::vector<double> colour_sums(static_cast<std::size_t>(num_clusters) * image.channels, 0.0);
    std::vector<int> counts(static_cast<std::size_t>(num_clusters), 0);

    for (std::size_t pixel = 0; pixel < num_pixels; ++pixel) {
        const int label = labels[pixel];
        ++counts[label];
        for (int channel = 0; channel < image.channels; ++channel) {
            colour_sums[static_cast<std::size_t>(label) * image.channels + channel] +=
                image.pixels[pixel * image.channels + channel];
        }
    }

    // Write the mean colour of each pixel's cluster.
    Image segments = image;
    for (std::size_t pixel = 0; pixel < num_pixels; ++pixel) {
        const int label = labels[pixel];
        for (int channel = 0; channel < image.channels; ++channel) {
            const double mean =
                colour_sums[static_cast<std::size_t>(label) * image.channels + channel] / counts[label];
            segments.pixels[pixel * image.channels + channel] =
                static_cast<unsigned char>(std::lround(mean));
        }
    }
    return segments;
}
