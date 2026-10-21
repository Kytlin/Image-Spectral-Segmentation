#include "eigensolver.hpp"

#include <random>
#include <stdexcept>

#include <Eigen/SparseLU>

namespace {

// Removes from `vector` its components along the columns of `basis` (assumed orthonormal):
//     v <- v - Q (Q^T v).
// Done twice ("twice is enough"): one pass loses orthogonality to rounding error when v
// is nearly inside span(Q), which is exactly what happens after an inverse-iteration solve.
void project_out(Eigen::VectorXd& vector, const Eigen::MatrixXd& basis) {
    if (basis.cols() == 0) {
        return;
    }
    for (int pass = 0; pass < 2; ++pass) {
        vector -= basis * (basis.transpose() * vector);
    }
}

// Rayleigh quotient r(v) = v^T M v / v^T v. Here v always has unit length, so it is v^T M v.
double rayleigh_quotient(const SparseMatrix& matrix, const Eigen::VectorXd& unit_vector) {
    return unit_vector.dot(matrix * unit_vector);
}

double residual_norm(const SparseMatrix& matrix, const Eigen::VectorXd& unit_vector, double value) {
    return (matrix * unit_vector - value * unit_vector).norm();
}

// Wraps a sparse LU solver for (M - shift * I). The nonzero pattern of M - shift * I
// is the same for every shift: M's pattern plus the whole diagonal. So the expensive
// symbolic step (choosing a fill-reducing column ordering) runs once, and each new shift
// only needs the numeric factorization.
class ShiftedSolver {
public:
    explicit ShiftedSolver(const SparseMatrix& matrix) : matrix_(matrix) {
        identity_.resize(matrix.rows(), matrix.cols());
        identity_.setIdentity();
        // Any shift works for the pattern, as long as it's nonzero, so no diagonal entry cancels.
        shifted_ = matrix_ - 1.0 * identity_;
        shifted_.makeCompressed();
        solver_.analyzePattern(shifted_);
    }

    // Factorizes M - shift * I. Returns false if LU found it (numerically) singular,
    // which means the shift is an eigenvalue to working precision.
    bool factorize(double shift) {
        shifted_ = matrix_ - shift * identity_;
        shifted_.makeCompressed();
        solver_.factorize(shifted_);
        return solver_.info() == Eigen::Success;
    }

    Eigen::VectorXd solve(const Eigen::VectorXd& right_hand_side) {
        return solver_.solve(right_hand_side);
    }

private:
    const SparseMatrix& matrix_;
    SparseMatrix identity_;
    SparseMatrix shifted_;
    Eigen::SparseLU<SparseMatrix, Eigen::COLAMDOrdering<int>> solver_;
};

}  // namespace

EigenpairResult rayleigh_quotient_iteration(
    const SparseMatrix& matrix,
    Eigen::VectorXd start,
    const Eigen::MatrixXd& found_vectors,
    const RqiOptions& options) {
    if (matrix.rows() != matrix.cols() || start.size() != matrix.rows()) {
        throw std::invalid_argument("rayleigh_quotient_iteration: size mismatch");
    }

    EigenpairResult result;
    ShiftedSolver solver(matrix);

    Eigen::VectorXd vector = std::move(start);
    project_out(vector, found_vectors);
    vector.normalize();

    // ---- Step 1: inverse iteration with a fixed shift ----
    if (options.warmup_steps > 0) {
        solver.factorize(options.warmup_shift);
        ++result.factorizations;
        for (int step = 0; step < options.warmup_steps; ++step) {
            vector = solver.solve(vector);
            project_out(vector, found_vectors);
            vector.normalize();
        }
    }

    // ---- Step 2: Rayleigh quotient iteration ----
    double value = rayleigh_quotient(matrix, vector);
    result.residual_history.push_back(residual_norm(matrix, vector, value));

    for (int step = 0; step < options.max_iterations; ++step) {
        if (result.residual_history.back() < options.tolerance) {
            result.converged = true;
            break;
        }

        const bool nonsingular = solver.factorize(value);
        ++result.factorizations;
        ++result.iterations;
        if (!nonsingular) {
            result.converged = true;
            break;
        }

        // Rounding error in this ill-conditioned solve is almost entirely in 
        // the direction of the desired eigenvector, so normalizing w is enough.
        vector = solver.solve(vector);
        project_out(vector, found_vectors);
        vector.normalize();

        value = rayleigh_quotient(matrix, vector);
        result.residual_history.push_back(residual_norm(matrix, vector, value));
    }
    if (result.residual_history.back() < options.tolerance) {
        result.converged = true;
    }

    result.value = value;
    result.vector = std::move(vector);
    return result;
}

std::vector<EigenpairResult> top_eigenpairs_rqi(
    const SparseMatrix& matrix,
    int count,
    const RqiOptions& options,
    unsigned seed) {
    const Eigen::Index size = matrix.rows();
    std::mt19937 generator(seed);
    std::normal_distribution<double> gaussian(0.0, 1.0);

    std::vector<EigenpairResult> results;
    // Columns are the eigenvectors found so far. They are orthonormal (up to the tolerance), because each of them written in terms of previously computed vectors
    Eigen::MatrixXd found_vectors(size, 0);

    for (int index = 0; index < count; ++index) {
        // A random Gaussian vector has a nonzero component along every eigenvector
        // with probability 1, so we have convergence.
        Eigen::VectorXd start(size);
        for (Eigen::Index i = 0; i < size; ++i) {
            start[i] = gaussian(generator);
        }

        EigenpairResult pair = rayleigh_quotient_iteration(matrix, start, found_vectors, options);

        found_vectors.conservativeResize(Eigen::NoChange, found_vectors.cols() + 1);
        found_vectors.col(found_vectors.cols() - 1) = pair.vector;
        results.push_back(std::move(pair));
    }
    return results;
}

Eigen::MatrixXd eigenvector_matrix(const std::vector<EigenpairResult>& eigenpairs) {
    if (eigenpairs.empty()) {
        throw std::invalid_argument("eigenvector_matrix: no eigenpairs");
    }
    const Eigen::Index num_rows = eigenpairs.front().vector.size();
    const Eigen::Index num_columns = static_cast<Eigen::Index>(eigenpairs.size());

    Eigen::MatrixXd columns(num_rows, num_columns);
    for (Eigen::Index column = 0; column < num_columns; ++column) {
        columns.col(column) = eigenpairs[column].vector;
    }
    return columns;
}
