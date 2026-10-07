#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>

#include "graph.hpp"
#include "image_io.hpp"
#include "preprocess.hpp"

namespace {

// Day 5 turns these into command-line flags.
constexpr int kTargetWidth = 160;
constexpr double kSigma = 1.0;
// Colour scale for edge weights, on RGB scaled to [0, 1]. Pixels whose colours differ by
// much more than this get a near-zero weight.
constexpr double kSigmaColor = 0.1;
// Edges lighter than this are dropped to keep W sparse.
constexpr double kWeightThreshold = 1e-4;

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

        start = Clock::now();
        const SparseMatrix affinity = build_affinity(blurred, kSigmaColor, kWeightThreshold);
        const Eigen::VectorXd degree = degrees(affinity);
        const SparseMatrix normalized = normalized_affinity(affinity, degree);
        const double nodes = static_cast<double>(affinity.rows());
        std::cout << "graph: n=" << affinity.rows() << ", nnz=" << affinity.nonZeros()
                  << " (" << affinity.nonZeros() / nodes << " per node)  (" << ms_since(start)
                  << " ms)\n";

        // Day 3 sanity checks. These move into unit tests at Checkpoint v1.
        // ||W - W^T||: should be exactly 0.
        const SparseMatrix transposed = affinity.transpose();
        std::cout << "  ||W - W^T|| = " << (affinity - transposed).norm() << "\n";
        // Column sums of W: should equal the row sums d, since W is symmetric.
        const Eigen::VectorXd column_sums = Eigen::RowVectorXd::Ones(affinity.rows()) * affinity;
        std::cout << "  max |colsum - d| = " << (column_sums - degree).cwiseAbs().maxCoeff()
                  << "\n";
        std::cout << "  isolated nodes (d = 0): " << (degree.array() == 0.0).count() << "\n";
        if (normalized.nonZeros() > 0) {
            std::cout << "  max |M_ij| = " << normalized.coeffs().cwiseAbs().maxCoeff() << "\n";
        }

        save_matrix_market("out/W.mtx", affinity);
        std::cout << "wrote out/W.mtx\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
