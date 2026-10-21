#pragma once

#include <vector>

#include <Eigen/Dense>

#include "types.hpp"

// Spectral embedding (Ng, Jordan & Weiss). `eigenvectors` is n x k, one eigenvector per column
// (see eigenvector_matrix). Returns a copy with each row scaled to unit length, so row i is
// pixel i's point on the unit sphere in R^k. All-zero rows (isolated pixels) stay zero.
Eigen::MatrixXd spectral_embedding(const Eigen::MatrixXd& eigenvectors);

// k-means on the rows of `points`: k-means++ seeding, then Lloyd's algorithm (assign each point
// to its nearest centre, move each centre to the mean of its points, until no label changes or
// max_iterations). Runs `restarts` times and keeps the run with the lowest inertia.
// Returns one label in [0, k) per row of `points`. If `iterations` isn't null, it receives the
// number of Lloyd iterations of the kept run.
std::vector<int> kmeans(const Eigen::MatrixXd& points, int k, unsigned seed, int max_iterations,
                        int restarts = 5, int* iterations = nullptr);

// Paints every pixel with the mean colour of its cluster. `labels` has one entry per pixel.
Image render_segments(const std::vector<int>& labels, const Image& image);
