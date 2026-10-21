#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Forward declaration of libpq's connection type, so this header doesn't need <libpq-fe.h>.
struct pg_conn;

struct RunRecord {
    std::string filename;
    int width = 0;
    int height = 0;
    int num_nodes = 0;
    int num_edges = 0;
    double blur_sigma = 0.0;
    double sigma_color = 0.0;
    double weight_threshold = 0.0;
    double graph_ms = 0.0;
    std::string thumbnail;  // may be empty
};

struct ExperimentRecord {
    long long run_id = 0;
    int k = 0;
    int eigenpairs_cached = 0;
    int eigenpairs_computed = 0;
    int factorizations = 0;
    double eigen_ms = 0.0;
    double kmeans_ms = 0.0;
    double max_residual = 0.0;
    bool all_converged = false;
    std::vector<double> eigenvalues;
    double runtime_ms = 0.0;
    int eigensolver_iterations = 0;
    int kmeans_iterations = 0;
    long long memory_bytes = 0;
};

// One connection to PostgreSQL via libpq. Owns the connection (RAII: closed in the destructor).
//
// A libpq connection must not be used by two threads at once, and the HTTP server answers
// requests on several threads, so every method locks `mutex_`. Queries here take about a
// millisecond, so that's fine for now. A busier server would use a pool of connections.
class Database {
public:
    // `url` looks like postgresql://user:password@host:5432/dbname. Throws if connecting fails.
    explicit Database(const std::string& url);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Inserts a run and returns the id that PostgreSQL assigned to it.
    long long insert_run(const RunRecord& run);
    // Inserts an experiment and returns its id.
    long long insert_experiment(const ExperimentRecord& experiment);

    // Deletes one experiment; returns the run it belonged to, or nullopt if the id doesn't exist.
    std::optional<long long> delete_experiment(long long experiment_id);

    // Deletes all experiments of a run and returns how many were deleted.
    int clear_experiments(long long run_id);

    // The most recent runs with at least one saved experiment, newest first, each with its
    // number of experiments and last k.
    nlohmann::json list_runs(int limit);

    // One run and all of its experiments, oldest first; nullopt if the id doesn't exist.
    std::optional<nlohmann::json> get_run(long long run_id);

private:
    // Runs one parameterized query (see database.cpp) and returns its rows as a JSON array.
    nlohmann::json query(const std::string& sql, const std::vector<std::string>& parameters);

    pg_conn* connection_ = nullptr;
    std::mutex mutex_;
};
