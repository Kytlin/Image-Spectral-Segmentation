#pragma once

#include <vector>

#include <Eigen/Dense>

#include "graph.hpp"

// Settings for Rayleigh quotient iteration (Trefethen & Bau, Lecture 27).
struct RqiOptions {
    // Upper bound on RQI steps per eigenpair (warm-up steps not counted).
    int max_iterations = 30;
    // Stop once the residual ||M v - mu v|| drops below this (v has unit length).
    double tolerance = 1e-10;
    // Steps of plain inverse iteration with the fixed shift `warmup_shift` before RQI starts.
    // 0 means "pure RQI from a random vector". A shift just above the top of the spectrum
    // steers the vector towards the LARGEST remaining eigenvalue, which is what clustering needs.
    int warmup_steps = 0;
    double warmup_shift = 1.001;
};

struct EigenpairResult {
    double value = 0.0;          // the eigenvalue estimate (final Rayleigh quotient)
    Eigen::VectorXd vector;      // unit-length eigenvector estimate
    int iterations = 0;          // RQI steps taken (not counting warm-up)
    int factorizations = 0;      // sparse LU factorizations done (warm-up shares one)
    bool converged = false;
    // Residual ||M v - mu v|| after the warm-up and after every RQI step.
    std::vector<double> residual_history;
};

// Finds one eigenpair of the symmetric matrix `matrix` by Rayleigh quotient iteration,
// starting from `start`. Every iterate is kept orthogonal to the columns of `found_vectors`
// (orthonormal eigenvectors found earlier), so the same eigenpair isn't found twice.
// Pass a matrix with 0 columns to skip that.
EigenpairResult rayleigh_quotient_iteration(
    const SparseMatrix& matrix,
    Eigen::VectorXd start,
    const Eigen::MatrixXd& found_vectors,
    const RqiOptions& options);

// Finds `count` eigenpairs one after another, each from a random start vector and deflated
// against the ones before it. With warmup_steps > 0 these should be the top `count`
// eigenpairs. Without warm-up there is no such guarantee (that's the point of the experiments).
std::vector<EigenpairResult> top_eigenpairs_rqi(
    const SparseMatrix& matrix,
    int count,
    const RqiOptions& options,
    unsigned seed);

// The eigenvectors side by side: an n x k matrix whose column j is eigenpairs[j].vector.
Eigen::MatrixXd eigenvector_matrix(const std::vector<EigenpairResult>& eigenpairs);
