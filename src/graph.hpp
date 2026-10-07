#pragma once

#include <string>

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include "types.hpp"

// Column-major (CSC) sparse matrix of doubles. W is symmetric, so CSC vs. CSR doesn't matter here.
using SparseMatrix = Eigen::SparseMatrix<double>;

// The pixel affinity matrix W, of size n x n where n = width * height.
// Node i is the pixel at (x, y) with i = y * width + x.
// Each pixel is connected to its 8 neighbours with weight
//     w_ij = exp(-||c_i - c_j||^2 / sigma_color^2),
// where c is the pixel's RGB colour scaled to [0, 1]. Weights below `threshold` are dropped.
// Every other entry is 0, so W has at most 8n nonzeros.
SparseMatrix build_affinity(const Image& image, double sigma_color, double threshold);

// Degree vector d: d_i is the sum of row i of W.
Eigen::VectorXd degrees(const SparseMatrix& affinity);

// M = D^{-1/2} W D^{-1/2}, where D = diag(d). Its eigenvalues lie in [-1, 1].
// Nodes with degree 0 (no edges survived the threshold) get a zero row and column.
SparseMatrix normalized_affinity(const SparseMatrix& affinity, const Eigen::VectorXd& degree);

// Writes the matrix in Matrix Market (.mtx) format, readable by scipy.io.mmread.
void save_matrix_market(const std::string& path, const SparseMatrix& matrix);
