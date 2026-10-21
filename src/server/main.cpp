// seg_server: HTTP API for the React frontend.
//
//   POST /api/runs                     upload pixels, build the graph, segment with k
//   POST /api/runs/:id/segmentations   re-segment a cached run with a new k (the slider)
//   POST /api/runs/:id/experiments     save the run's latest result to the experiment log
//   DELETE /api/runs/:id/experiments   clear a run's experiment log
//   DELETE /api/experiments/:id        delete one experiment-log entry
//   GET  /api/runs                     history: recent runs from PostgreSQL
//   GET  /api/runs/:id                 one run and its experiment log
//   GET  /api/health                   liveness check
//
// Everything else is served from STATIC_DIR (the built React app), so the browser loads the
// page and the API from the same origin.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Project headers (which pull in Eigen) must come before httplib.h: httplib includes
// <resolv.h>, which #defines `_res`, a name Eigen uses for function parameters.
#include "base64.hpp"
#include "clustering.hpp"
#include "database.hpp"
#include "eigensolver.hpp"
#include "graph.hpp"
#include "preprocess.hpp"
#include "run_cache.hpp"
#include "types.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace {

using nlohmann::json;
using Clock = std::chrono::steady_clock;

// Graph and solver settings. Blur sigma and weight threshold are defaults; requests can set them.
constexpr double kBlurSigma = 1.0;
constexpr double kMaxBlurSigma = 5.0;
constexpr double kSigmaColor = 0.1;
constexpr double kWeightThreshold = 1e-4;
constexpr double kMaxWeightThreshold = 0.99;
constexpr int kWarmupSteps = 5;
constexpr unsigned kSeed = 42;
constexpr int kKmeansIterations = 100;

// Request limits. The browser downsamples to at most 160 px on the long side, so these leave
// room. They protect the server from images that would take minutes to factorize.
constexpr int kMaxSide = 256;
constexpr int kMinK = 2;
constexpr int kMaxK = 8;
constexpr std::size_t kMaxThumbnailBytes = 64 * 1024;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : fallback;
}

// Thrown by request handlers for bad input. Becomes an HTTP error with a JSON body.
struct HttpError : std::runtime_error {
    HttpError(int status_code, const std::string& message)
        : std::runtime_error(message), status(status_code) {}
    int status;
};

void send_json(httplib::Response& response, const json& body, int status = 200) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

json parse_body(const httplib::Request& request) {
    try {
        return json::parse(request.body);
    } catch (const json::parse_error&) {
        throw HttpError(400, "request body is not valid JSON");
    }
}

int read_k(const json& body) {
    if (!body.contains("k") || !body["k"].is_number_integer()) {
        throw HttpError(400, "missing integer field 'k'");
    }
    const int k = body["k"].get<int>();
    if (k < kMinK || k > kMaxK) {
        throw HttpError(400, "k must be between " + std::to_string(kMinK) + " and " +
                                 std::to_string(kMaxK));
    }
    return k;
}

// An optional number field in [min, max], or `fallback` if absent.
double read_number(const json& body, const char* name, double fallback, double min, double max) {
    if (!body.contains(name)) {
        return fallback;
    }
    if (!body[name].is_number() || body[name].get<double>() < min || body[name].get<double>() > max) {
        throw HttpError(400, std::string("'") + name + "' must be a number between " +
                                 std::to_string(min) + " and " + std::to_string(max));
    }
    return body[name].get<double>();
}

// Segments a cached run with k clusters. Computes new eigenpairs only if fewer than k are
// cached. Fills `record`, keeps a copy as the run's latest result (saved to the experiment log
// only if the user asks), and returns the JSON the frontend draws from.
// `graph_ms` is the graph-building time when this request built the graph (0 otherwise), so
// runtime_ms is the total algorithm time of this request.
json segment_cached_run(CachedRun& run, int k, ExperimentRecord& record, double graph_ms = 0.0) {
    // One request at a time per run (see CachedRun::mutex).
    const std::lock_guard<std::mutex> lock(run.mutex);

    record.k = k;
    record.eigenpairs_cached = static_cast<int>(run.eigenpairs.size());

    // ---- Eigenpairs: reuse the cached ones, compute only the missing ones. ----
    const auto eigen_start = Clock::now();
    RqiOptions options;
    options.warmup_steps = kWarmupSteps;
    const std::size_t first_new = run.eigenpairs.size();
    extend_eigenpairs(run.normalized, run.eigenpairs, k, options, kSeed);
    record.eigen_ms = ms_since(eigen_start);
    record.eigenpairs_computed = static_cast<int>(run.eigenpairs.size() - first_new);

    for (std::size_t i = first_new; i < run.eigenpairs.size(); ++i) {
        record.factorizations += run.eigenpairs[i].factorizations;
        record.eigensolver_iterations += kWarmupSteps + run.eigenpairs[i].iterations;
    }

    // The first k pairs are the ones clustering uses (more may be cached from a larger k).
    const std::vector<EigenpairResult> used(run.eigenpairs.begin(), run.eigenpairs.begin() + k);
    record.all_converged = true;
    json residual_histories = json::array();
    for (const EigenpairResult& pair : used) {
        record.eigenvalues.push_back(pair.value);
        record.max_residual = std::max(record.max_residual, pair.residual_history.back());
        record.all_converged = record.all_converged && pair.converged;
        residual_histories.push_back(pair.residual_history);
    }

    // ---- Clustering. ----
    const auto kmeans_start = Clock::now();
    const std::vector<int> labels =
        kmeans(spectral_embedding(eigenvector_matrix(used)), k, kSeed, kKmeansIterations,
               /*restarts=*/5, &record.kmeans_iterations);
    record.kmeans_ms = ms_since(kmeans_start);
    record.runtime_ms = graph_ms + record.eigen_ms + record.kmeans_ms;

    // ---- Memory: what this run needs at its peak. M (CSC: a value and a row index per
    // nonzero, plus column starts), the stored eigenvectors, and the largest LU factorization
    // (also a value and an index per nonzero), which exists during each solve. ----
    constexpr long long kEntryBytes = sizeof(double) + sizeof(int);
    long long peak_factor_nonzeros = 0;
    for (const EigenpairResult& pair : run.eigenpairs) {
        peak_factor_nonzeros = std::max(peak_factor_nonzeros, pair.peak_factor_nonzeros);
    }
    record.memory_bytes =
        run.normalized.nonZeros() * kEntryBytes + (run.normalized.cols() + 1) * sizeof(int) +
        run.normalized.rows() * static_cast<long long>(run.eigenpairs.size()) * sizeof(double) +
        peak_factor_nonzeros * kEntryBytes;

    run.last_result = record;
    run.last_result_saved = false;
    ++run.last_result_id;

    // One byte per pixel is plenty for k <= 8.
    const std::vector<std::uint8_t> label_bytes(labels.begin(), labels.end());

    // All cached eigenvalues go to the eigenvalue chart, not just the first k.
    json cached_eigenvalues = json::array();
    for (const EigenpairResult& pair : run.eigenpairs) {
        cached_eigenvalues.push_back(pair.value);
    }

    return {
        {"k", k},
        {"labels", base64_encode(label_bytes)},
        {"eigenvalues", record.eigenvalues},
        {"cached_eigenvalues", cached_eigenvalues},
        {"residual_histories", residual_histories},
        {"eigenpairs_cached", record.eigenpairs_cached},
        {"eigenpairs_computed", record.eigenpairs_computed},
        {"factorizations", record.factorizations},
        {"eigen_ms", record.eigen_ms},
        {"kmeans_ms", record.kmeans_ms},
        {"max_residual", record.max_residual},
        {"all_converged", record.all_converged},
        {"runtime_ms", record.runtime_ms},
        {"eigensolver_iterations", record.eigensolver_iterations},
        {"kmeans_iterations", record.kmeans_iterations},
        {"memory_bytes", record.memory_bytes},
        {"result_id", run.last_result_id},
    };
}

// POST /api/runs
// Body: {filename, width, height, pixels (base64 RGB, row-major), k, thumbnail (data URL),
//        blur_sigma, weight_threshold (both optional)}
void handle_create_run(const httplib::Request& request, httplib::Response& response,
                       Database& database, RunCache& cache) {
    const json body = parse_body(request);

    // ---- Validate the input. ----
    if (!body.contains("width") || !body.contains("height") || !body.contains("pixels") ||
        !body["width"].is_number_integer() || !body["height"].is_number_integer() ||
        !body["pixels"].is_string()) {
        throw HttpError(400, "need integer 'width', 'height' and base64 string 'pixels'");
    }
    const int width = body["width"].get<int>();
    const int height = body["height"].get<int>();
    if (width < 2 || height < 2 || width > kMaxSide || height > kMaxSide) {
        throw HttpError(400, "width and height must be between 2 and " + std::to_string(kMaxSide));
    }
    const int k = read_k(body);
    const double blur_sigma = read_number(body, "blur_sigma", kBlurSigma, 0.0, kMaxBlurSigma);
    const double weight_threshold =
        read_number(body, "weight_threshold", kWeightThreshold, 0.0, kMaxWeightThreshold);

    std::vector<std::uint8_t> pixels;
    try {
        pixels = base64_decode(body["pixels"].get<std::string>());
    } catch (const std::invalid_argument&) {
        throw HttpError(400, "'pixels' is not valid base64");
    }
    if (pixels.size() != static_cast<std::size_t>(width) * height * 3) {
        throw HttpError(400, "'pixels' must hold width * height * 3 bytes (RGB)");
    }

    const std::string filename = body.value("filename", std::string("image"));
    std::string thumbnail = body.value("thumbnail", std::string());
    if (thumbnail.size() > kMaxThumbnailBytes) {
        thumbnail.clear();  // too big to store; the history list just shows no preview
    }

    // ---- Build the graph (the same pipeline as `segment`, minus the downsampling, which
    // the browser already did). ----
    const auto graph_start = Clock::now();
    const Image image{width, height, 3, std::vector<unsigned char>(pixels.begin(), pixels.end())};
    const Image blurred = gaussian_blur(image, blur_sigma);
    auto run = std::make_shared<CachedRun>();
    const SparseMatrix affinity = build_affinity(blurred, kSigmaColor, weight_threshold);
    run->normalized = normalized_affinity(affinity, degrees(affinity));
    const double graph_ms = ms_since(graph_start);

    // ---- Record the run in PostgreSQL. The database assigns the run_id. ----
    RunRecord run_record;
    run_record.filename = filename;
    run_record.width = width;
    run_record.height = height;
    run_record.num_nodes = static_cast<int>(affinity.rows());
    run_record.num_edges = static_cast<int>(affinity.nonZeros());
    run_record.blur_sigma = blur_sigma;
    run_record.sigma_color = kSigmaColor;
    run_record.weight_threshold = weight_threshold;
    run_record.graph_ms = graph_ms;
    run_record.thumbnail = thumbnail;
    long long run_id = 0;
    try {
        run_id = database.insert_run(run_record);
    } catch (const std::exception& error) {
        throw HttpError(503, std::string("database unavailable: ") + error.what());
    }

    cache.insert(run_id, run);

    // ---- First segmentation. ----
    ExperimentRecord experiment;
    experiment.run_id = run_id;
    json segmentation = segment_cached_run(*run, k, experiment, graph_ms);

    send_json(response,
              {{"run",
                {{"id", run_id},
                 {"filename", filename},
                 {"width", width},
                 {"height", height},
                 {"num_nodes", run_record.num_nodes},
                 {"num_edges", run_record.num_edges},
                 {"blur_sigma", blur_sigma},
                 {"weight_threshold", weight_threshold},
                 {"graph_ms", graph_ms}}},
               {"segmentation", std::move(segmentation)}},
              201);
}

long long read_run_id(const httplib::Request& request) {
    try {
        return std::stoll(request.matches[1]);
    } catch (const std::exception&) {
        throw HttpError(400, "invalid run id");
    }
}

// POST /api/runs/:id/segmentations   Body: {k}
void handle_resegment(const httplib::Request& request, httplib::Response& response,
                      RunCache& cache) {
    const long long run_id = read_run_id(request);
    const int k = read_k(parse_body(request));

    const std::shared_ptr<CachedRun> run = cache.find(run_id);
    if (run == nullptr) {
        // 410 Gone: the run may exist in the database, but its graph is no longer in memory
        // (evicted, or the server restarted). The frontend asks the user to upload again.
        throw HttpError(410, "run " + std::to_string(run_id) + " is no longer in memory");
    }

    ExperimentRecord experiment;
    experiment.run_id = run_id;
    json segmentation = segment_cached_run(*run, k, experiment);
    send_json(response, segmentation);
}

// POST /api/runs/:id/experiments   Body: {result_id}
// Saves the run's latest result to the experiment log. The browser sends the result_id it is
// showing, so a result that was replaced in the meantime is never saved by mistake.
void handle_save_result(const httplib::Request& request, httplib::Response& response,
                        Database& database, RunCache& cache) {
    const long long run_id = read_run_id(request);
    const json body = parse_body(request);
    if (!body.contains("result_id") || !body["result_id"].is_number_integer()) {
        throw HttpError(400, "missing integer field 'result_id'");
    }
    const long long result_id = body["result_id"].get<long long>();

    const std::shared_ptr<CachedRun> run = cache.find(run_id);
    if (run == nullptr) {
        throw HttpError(410, "run " + std::to_string(run_id) + " is no longer in memory");
    }

    const std::lock_guard<std::mutex> lock(run->mutex);
    if (!run->last_result || run->last_result_id != result_id) {
        throw HttpError(409, "this result is no longer the latest one for the run");
    }
    // Saving the same result twice is a no-op, so double clicks don't create duplicate rows.
    if (!run->last_result_saved) {
        try {
            run->saved_experiment_id = database.insert_experiment(*run->last_result);
        } catch (const std::exception& error) {
            throw HttpError(503, std::string("database unavailable: ") + error.what());
        }
        run->last_result_saved = true;
    }
    send_json(response, {{"saved", true}, {"experiment_id", run->saved_experiment_id}}, 201);
}

// DELETE /api/experiments/:id
void handle_delete_experiment(const httplib::Request& request, httplib::Response& response,
                              Database& database, RunCache& cache) {
    const long long experiment_id = read_run_id(request);  // same "(\d+)" capture
    const std::optional<long long> run_id = database.delete_experiment(experiment_id);
    if (!run_id) {
        throw HttpError(404, "experiment " + std::to_string(experiment_id) + " not found");
    }
    // If that row was the saved copy of the run's latest result, it can be saved again.
    if (const std::shared_ptr<CachedRun> run = cache.find(*run_id)) {
        const std::lock_guard<std::mutex> lock(run->mutex);
        if (run->saved_experiment_id == experiment_id) {
            run->last_result_saved = false;
        }
    }
    send_json(response, {{"deleted", 1}, {"run_id", *run_id}});
}

// DELETE /api/runs/:id/experiments
void handle_clear_log(const httplib::Request& request, httplib::Response& response,
                      Database& database, RunCache& cache) {
    const long long run_id = read_run_id(request);
    const int deleted = database.clear_experiments(run_id);
    if (const std::shared_ptr<CachedRun> run = cache.find(run_id)) {
        const std::lock_guard<std::mutex> lock(run->mutex);
        run->last_result_saved = false;
    }
    send_json(response, {{"deleted", deleted}});
}

// GET /api/runs?limit=20
void handle_list_runs(const httplib::Request& request, httplib::Response& response,
                      Database& database, RunCache& cache) {
    int limit = 20;
    if (request.has_param("limit")) {
        try {
            limit = std::clamp(std::stoi(request.get_param_value("limit")), 1, 100);
        } catch (const std::exception&) {
            throw HttpError(400, "invalid limit");
        }
    }
    json runs = database.list_runs(limit);
    // Tell the frontend which runs can still be re-segmented without uploading again.
    for (json& run : runs) {
        run["in_memory"] = cache.contains(run["id"].get<long long>());
    }
    send_json(response, runs);
}

// GET /api/runs/:id
void handle_get_run(const httplib::Request& request, httplib::Response& response,
                    Database& database, RunCache& cache) {
    const long long run_id = read_run_id(request);
    std::optional<json> run = database.get_run(run_id);
    if (!run) {
        throw HttpError(404, "run " + std::to_string(run_id) + " not found");
    }
    (*run)["in_memory"] = cache.contains(run_id);
    send_json(response, *run);
}

// Wraps a handler so that any exception becomes a JSON error response instead of a crash.
template <typename Handler>
httplib::Server::Handler with_errors(Handler handler) {
    return [handler](const httplib::Request& request, httplib::Response& response) {
        try {
            handler(request, response);
        } catch (const HttpError& error) {
            send_json(response, {{"error", error.what()}}, error.status);
        } catch (const std::exception& error) {
            std::cerr << "error: " << request.method << " " << request.path << ": "
                      << error.what() << "\n";
            send_json(response, {{"error", "internal server error"}}, 500);
        }
    };
}

}  // namespace

int main() {
    const std::string database_url =
        env_or("DATABASE_URL", "postgresql://seg:seg@localhost:5432/seg");
    const int port = std::stoi(env_or("PORT", "8080"));
    const std::string static_dir = env_or("STATIC_DIR", "web/dist");
    const std::size_t cache_capacity = std::stoul(env_or("CACHE_CAPACITY", "20"));

    try {
        Database database(database_url);
        RunCache cache(cache_capacity);
        httplib::Server server;

        // Runs above ~2 MB of JSON are not something this frontend sends.
        server.set_payload_max_length(4 * 1024 * 1024);

        server.Get("/api/health", [](const httplib::Request&, httplib::Response& response) {
            send_json(response, {{"status", "ok"}});
        });
        server.Post("/api/runs", with_errors([&](const auto& request, auto& response) {
            handle_create_run(request, response, database, cache);
        }));
        // R"(...)" is a raw string: the regex (\d+) captures the id into request.matches[1].
        server.Post(R"(/api/runs/(\d+)/segmentations)",
                    with_errors([&](const auto& request, auto& response) {
                        handle_resegment(request, response, cache);
                    }));
        server.Post(R"(/api/runs/(\d+)/experiments)",
                    with_errors([&](const auto& request, auto& response) {
                        handle_save_result(request, response, database, cache);
                    }));
        server.Delete(R"(/api/runs/(\d+)/experiments)",
                      with_errors([&](const auto& request, auto& response) {
                          handle_clear_log(request, response, database, cache);
                      }));
        server.Delete(R"(/api/experiments/(\d+))",
                      with_errors([&](const auto& request, auto& response) {
                          handle_delete_experiment(request, response, database, cache);
                      }));
        server.Get("/api/runs", with_errors([&](const auto& request, auto& response) {
            handle_list_runs(request, response, database, cache);
        }));
        server.Get(R"(/api/runs/(\d+))", with_errors([&](const auto& request, auto& response) {
            handle_get_run(request, response, database, cache);
        }));

        if (std::filesystem::is_directory(static_dir)) {
            server.set_mount_point("/", static_dir);
            std::cout << "serving frontend from " << static_dir << "\n";
        } else {
            std::cout << "no frontend at " << static_dir << " (API only)\n";
        }

        std::cout << "listening on http://0.0.0.0:" << port << "\n" << std::flush;
        if (!server.listen("0.0.0.0", port)) {
            std::cerr << "error: cannot listen on port " << port << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
