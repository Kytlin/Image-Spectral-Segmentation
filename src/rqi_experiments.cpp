// rqi_bench: where does Rayleigh quotient iteration work for spectral clustering, and where not?
//
// Every test image goes through the same pipeline as `segment` (blur -> affinity W -> M), then
// the top-k eigenvectors of M are computed two ways:
//   plain  : pure RQI from a random start vector (warm-up 0)
//   warm   : a few steps of fixed-shift inverse iteration (shift just above 1), then RQI
// The images are small enough for a dense eigensolver, which gives the exact spectrum to
// compare against.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "clustering.hpp"
#include "eigensolver.hpp"
#include "graph.hpp"
#include "image_io.hpp"
#include "preprocess.hpp"

namespace {

// Same values as src/main.cpp.
constexpr double kBlurSigma = 1.0;
constexpr double kSigmaColor = 0.1;
constexpr double kWeightThreshold = 1e-4;
constexpr int kWarmupSteps = 5;

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct TestCase {
    std::string name;
    Image image;
    std::vector<int> truth;  // ground-truth label per pixel; empty if there is none
    int k = 0;               // number of segments / eigenvectors
};

// ---------------------------------------------------------------------------------------------
// Synthetic images
// ---------------------------------------------------------------------------------------------

struct Colour {
    double r, g, b;  // in [0, 1]
};

unsigned char to_byte(double value) {
    return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

// Four quadrants, quadrant q painted with colours[q], plus Gaussian noise of standard
// deviation `noise` (in [0, 1] units) on every channel.
TestCase make_quadrants(const std::string& name, int width, int height,
                        const std::vector<Colour>& colours, double noise, unsigned seed) {
    TestCase test{name, Image{width, height, 3, {}}, {}, 4};
    test.image.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    test.truth.resize(static_cast<std::size_t>(width) * height);

    std::mt19937 generator(seed);
    std::normal_distribution<double> gaussian(0.0, noise > 0.0 ? noise : 1.0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int quadrant = (x < width / 2 ? 0 : 1) + (y < height / 2 ? 0 : 2);
            const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
            test.truth[pixel] = quadrant;
            const double base[3] = {colours[quadrant].r, colours[quadrant].g, colours[quadrant].b};
            for (int channel = 0; channel < 3; ++channel) {
                const double jitter = noise > 0.0 ? gaussian(generator) : 0.0;
                test.image.pixels[pixel * 3 + channel] = to_byte(base[channel] + jitter);
            }
        }
    }
    return test;
}

// A left-to-right grey ramp from 0.2 to 0.8. No true segments.
TestCase make_gradient(int width, int height) {
    TestCase test{"gradient (no clusters)", Image{width, height, 3, {}}, {}, 4};
    test.image.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double value = 0.2 + 0.6 * x / (width - 1.0);
            for (int channel = 0; channel < 3; ++channel) {
                test.image.pixels[(static_cast<std::size_t>(y) * width + x) * 3 + channel] =
                    to_byte(value);
            }
        }
    }
    return test;
}

// The sample image, downsampled. Ground truth by colour: red disc = 1, yellow square = 2,
// gradient background = 0.
TestCase make_shapes(const std::string& path, int factor) {
    TestCase test{"shapes.png", downsample(load_image(path), factor), {}, 3};
    const std::size_t num_pixels = static_cast<std::size_t>(test.image.width) * test.image.height;
    test.truth.resize(num_pixels);
    for (std::size_t pixel = 0; pixel < num_pixels; ++pixel) {
        const int r = test.image.pixels[pixel * 3 + 0];
        const int g = test.image.pixels[pixel * 3 + 1];
        const int b = test.image.pixels[pixel * 3 + 2];
        int label = 0;
        if (r > 150 && g < 100 && b < 100) {
            label = 1;
        } else if (r > 150 && g > 150 && b < 120) {
            label = 2;
        }
        test.truth[pixel] = label;
    }
    return test;
}

// ---------------------------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------------------------

// Fraction of pixels labelled correctly, under the best matching of cluster ids to true ids
// (k! permutations; fine for k <= 6).
double clustering_accuracy(const std::vector<int>& labels, const std::vector<int>& truth, int k) {
    std::vector<std::vector<int>> confusion(k, std::vector<int>(k, 0));
    for (std::size_t i = 0; i < labels.size(); ++i) {
        ++confusion[labels[i]][truth[i]];
    }
    std::vector<int> permutation(k);
    for (int i = 0; i < k; ++i) {
        permutation[i] = i;
    }
    int best_correct = 0;
    do {
        int correct = 0;
        for (int cluster = 0; cluster < k; ++cluster) {
            correct += confusion[cluster][permutation[cluster]];
        }
        best_correct = std::max(best_correct, correct);
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    return static_cast<double>(best_correct) / static_cast<double>(labels.size());
}

// Rank of `value` in the true spectrum: 1 = largest eigenvalue. `ascending` is sorted ascending.
long rank_in_spectrum(double value, const Eigen::VectorXd& ascending) {
    Eigen::Index nearest = 0;
    (ascending.array() - value).abs().minCoeff(&nearest);
    return static_cast<long>(ascending.size() - nearest);
}

// Largest principal angle (degrees) between span(found) and span(exact). 0 = same subspace.
double subspace_angle_degrees(const Eigen::MatrixXd& found, const Eigen::MatrixXd& exact) {
    const Eigen::MatrixXd found_basis = found.householderQr().householderQ() *
                                        Eigen::MatrixXd::Identity(found.rows(), found.cols());
    const Eigen::VectorXd cosines =
        Eigen::JacobiSVD<Eigen::MatrixXd>(exact.transpose() * found_basis).singularValues();
    const double smallest_cosine = std::min(1.0, cosines.minCoeff());
    return std::acos(smallest_cosine) * 180.0 / M_PI;
}

// Nearest-neighbour upscale, so the tiny test segmentations are visible.
Image upscale(const Image& image, int factor) {
    Image big{image.width * factor, image.height * factor, image.channels, {}};
    big.pixels.resize(static_cast<std::size_t>(big.width) * big.height * big.channels);
    for (int y = 0; y < big.height; ++y) {
        for (int x = 0; x < big.width; ++x) {
            for (int channel = 0; channel < image.channels; ++channel) {
                big.pixels[(static_cast<std::size_t>(y) * big.width + x) * big.channels + channel] =
                    image.pixels[(static_cast<std::size_t>(y / factor) * image.width + x / factor) *
                                     image.channels + channel];
            }
        }
    }
    return big;
}

SparseMatrix build_normalized(const Image& image) {
    const Image blurred = gaussian_blur(image, kBlurSigma);
    const SparseMatrix affinity = build_affinity(blurred, kSigmaColor, kWeightThreshold);
    return normalized_affinity(affinity, degrees(affinity));
}

// ---------------------------------------------------------------------------------------------
// Experiments
// ---------------------------------------------------------------------------------------------

void run_case(const TestCase& test, const std::string& file_stem) {
    const SparseMatrix normalized = build_normalized(test.image);
    const Eigen::Index size = normalized.rows();
    const int k = test.k;

    // Exact spectrum from a dense solver (eigenvalues ascending).
    const Eigen::MatrixXd dense_matrix = Eigen::MatrixXd(normalized);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> dense(dense_matrix);
    const Eigen::VectorXd& exact_values = dense.eigenvalues();
    const Eigen::MatrixXd exact_top = dense.eigenvectors().rightCols(k);

    std::printf("\n=== %s: %d x %d, n = %ld, k = %d ===\n", test.name.c_str(), test.image.width,
                test.image.height, static_cast<long>(size), k);
    std::printf("exact top %d+1 eigenvalues:", k);
    for (int i = 0; i <= k; ++i) {
        std::printf(" %.6f", exact_values[size - 1 - i]);
    }
    std::printf("\neigengap lambda_%d - lambda_%d = %.2e\n", k, k + 1,
                exact_values[size - k] - exact_values[size - k - 1]);

    std::printf("%-6s %9s %7s %9s %-22s %-28s %8s %8s\n", "method", "time ms", "LU", "max res",
                "ranks found (1=top)", "eigenvalues found", "angle", "accuracy");

    // Baseline: the EXACT top-k eigenvectors through the same k-means. Any accuracy lost
    // here is the fault of the model (graph / k-means), not of the eigensolver.
    if (!test.truth.empty()) {
        std::printf("exact eigenvectors -> k-means accuracy for seeds 1..5:");
        for (const unsigned seed : {1u, 2u, 3u, 4u, 5u}) {
            const std::vector<int> labels = kmeans(spectral_embedding(exact_top), k, seed, 100);
            std::printf(" %.1f%%", 100.0 * clustering_accuracy(labels, test.truth, k));
        }
        std::printf("\n");
    }

    for (const bool warm : {false, true}) {
        RqiOptions options;
        options.warmup_steps = warm ? kWarmupSteps : 0;

        const auto start = Clock::now();
        const std::vector<EigenpairResult> pairs = top_eigenpairs_rqi(normalized, k, options, 42);
        const double elapsed = ms_since(start);

        int total_factorizations = 0;
        double worst_residual = 0.0;
        std::string ranks;
        std::string values;
        for (const EigenpairResult& pair : pairs) {
            total_factorizations += pair.factorizations;
            worst_residual = std::max(worst_residual, pair.residual_history.back());
            ranks += std::to_string(rank_in_spectrum(pair.value, exact_values)) + " ";
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.4f ", pair.value);
            values += buffer;
        }

        const Eigen::MatrixXd eigenvectors = eigenvector_matrix(pairs);
        const std::vector<int> labels = kmeans(spectral_embedding(eigenvectors), k, 7, 100);
        std::string accuracy = "n/a";
        if (!test.truth.empty()) {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.1f%%",
                          100.0 * clustering_accuracy(labels, test.truth, k));
            accuracy = buffer;
        }

        std::printf("%-6s %9.1f %7d %9.1e %-22s %-28s %7.2f° %8s\n", warm ? "warm" : "plain",
                    elapsed, total_factorizations, worst_residual, ranks.c_str(), values.c_str(),
                    subspace_angle_degrees(eigenvectors, exact_top), accuracy.c_str());

        save_png("out/bench_" + file_stem + (warm ? "_warm.png" : "_plain.png"),
                 upscale(render_segments(labels, test.image), 4));
    }
}

// Residual after each RQI step for one eigenpair: shows cubic convergence.
void show_convergence(const TestCase& test) {
    const SparseMatrix normalized = build_normalized(test.image);
    RqiOptions options;
    options.warmup_steps = kWarmupSteps;
    options.tolerance = 1e-14;  // run to machine precision so the whole curve is visible
    const std::vector<EigenpairResult> pairs = top_eigenpairs_rqi(normalized, 2, options, 42);

    std::printf("\n=== Convergence on '%s', 2nd eigenpair (after %d warm-up steps) ===\n",
                test.name.c_str(), kWarmupSteps);
    const std::vector<double>& history = pairs[1].residual_history;
    for (std::size_t step = 0; step < history.size(); ++step) {
        std::printf("  step %zu: ||Mv - mu v|| = %.3e\n", step, history[step]);
    }
}

// How many warm-up steps until RQI reliably returns the top k?
void warmup_sweep(const TestCase& test) {
    const SparseMatrix normalized = build_normalized(test.image);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> dense(Eigen::MatrixXd(normalized),
                                                         Eigen::EigenvaluesOnly);
    std::printf("\n=== Warm-up sweep on '%s' (ranks of the %d eigenvalues found; 1=top) ===\n",
                test.name.c_str(), test.k);
    for (const int steps : {0, 1, 2, 3, 5, 10, 20}) {
        RqiOptions options;
        options.warmup_steps = steps;
        std::printf("  warm-up %2d:", steps);
        for (const unsigned seed : {1u, 2u, 3u}) {
            const std::vector<EigenpairResult> pairs =
                top_eigenpairs_rqi(normalized, test.k, options, seed);
            std::vector<long> ranks;
            int lu = 0;
            for (const EigenpairResult& pair : pairs) {
                ranks.push_back(rank_in_spectrum(pair.value, dense.eigenvalues()));
                lu += pair.factorizations;
            }
            std::sort(ranks.begin(), ranks.end());
            std::printf("  [");
            for (long rank : ranks) {
                std::printf("%ld ", rank);
            }
            std::printf("| %d LU]", lu);
        }
        std::printf("\n");
    }
}

// Cost vs. image size (no dense check: too big).
void scaling() {
    std::printf("\n=== Scaling (warm RQI, k = 4, clean quadrants) ===\n");
    std::printf("%10s %8s %10s %6s %12s\n", "image", "n", "time ms", "LU", "ms per LU");
    const std::vector<Colour> colours = {{0.9, 0.2, 0.2}, {0.2, 0.8, 0.2}, {0.2, 0.3, 0.9},
                                         {0.9, 0.9, 0.3}};
    for (const int side : {40, 80, 160, 240}) {
        const TestCase test = make_quadrants("quadrants", side, side, colours, 0.02, 5);
        const SparseMatrix normalized = build_normalized(test.image);
        RqiOptions options;
        options.warmup_steps = kWarmupSteps;
        const auto start = Clock::now();
        const std::vector<EigenpairResult> pairs = top_eigenpairs_rqi(normalized, 4, options, 42);
        const double elapsed = ms_since(start);
        int factorizations = 0;
        for (const EigenpairResult& pair : pairs) {
            factorizations += pair.factorizations;
        }
        std::printf("%5dx%-4d %8ld %10.1f %6d %12.2f\n", side, side,
                    static_cast<long>(normalized.rows()), elapsed, factorizations,
                    elapsed / factorizations);
    }
}

}  // namespace

int main() {
    try {
        std::filesystem::create_directories("out");
        constexpr int kWidth = 48;
        constexpr int kHeight = 40;

        // 1. Easy: four very different colours, a little noise.
        const TestCase clean = make_quadrants(
            "clean quadrants", kWidth, kHeight,
            {{0.9, 0.2, 0.2}, {0.2, 0.8, 0.2}, {0.2, 0.3, 0.9}, {0.9, 0.9, 0.3}}, 0.02, 1);
        // 2. The real sample (320x240 -> 53x40).
        const TestCase shapes = make_shapes("data/samples/shapes.png", 6);
        // 3. Hard: colours only ~0.08 apart (less than sigma_color) and heavy noise.
        const TestCase subtle = make_quadrants(
            "low contrast + noise", kWidth, kHeight,
            {{0.50, 0.50, 0.50}, {0.58, 0.50, 0.50}, {0.50, 0.58, 0.50}, {0.50, 0.50, 0.58}},
            0.08, 2);
        // 4. No cluster structure at all.
        const TestCase gradient = make_gradient(kWidth, kHeight);

        run_case(clean, "clean");
        run_case(shapes, "shapes");
        run_case(subtle, "subtle");
        run_case(gradient, "gradient");

        show_convergence(clean);
        warmup_sweep(clean);
        warmup_sweep(shapes);
        warmup_sweep(subtle);
        warmup_sweep(gradient);
        scaling();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
