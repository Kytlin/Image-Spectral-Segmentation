#include "database.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

#include <libpq-fe.h>

namespace {

// PostgreSQL type ids (from the pg_type catalog) for the column types this server reads.
constexpr Oid kBoolOid = 16;
constexpr Oid kInt8Oid = 20;
constexpr Oid kInt4Oid = 23;
constexpr Oid kFloat8Oid = 701;
constexpr Oid kNumericOid = 1700;
constexpr Oid kJsonOid = 114;

// Owns a PGresult and frees it with PQclear when it goes out of scope (RAII again, like Image).
using ResultPointer = std::unique_ptr<PGresult, decltype(&PQclear)>;

// Doubles go to PostgreSQL as text. "%.17g" prints enough digits to read back the exact value.
std::string format_double(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

// A PostgreSQL array literal: {1.5,2.25,3}.
std::string format_double_array(const std::vector<double>& values) {
    std::string text = "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            text += ",";
        }
        text += format_double(values[i]);
    }
    return text + "}";
}

// Every value comes back from libpq as text. Convert it into the matching JSON type.
nlohmann::json cell_to_json(const PGresult* result, int row, int column) {
    if (PQgetisnull(result, row, column)) {
        return nullptr;
    }
    const std::string text = PQgetvalue(result, row, column);
    switch (PQftype(result, column)) {
        case kBoolOid:
            return text == "t";
        case kInt4Oid:
        case kInt8Oid:
            return std::stoll(text);
        case kFloat8Oid:
        case kNumericOid:
            return std::stod(text);
        case kJsonOid:
            return nlohmann::json::parse(text);
        default:
            return text;
    }
}

}  // namespace

Database::Database(const std::string& url) {
    connection_ = PQconnectdb(url.c_str());
    if (PQstatus(connection_) != CONNECTION_OK) {
        const std::string message = PQerrorMessage(connection_);
        PQfinish(connection_);
        connection_ = nullptr;
        throw std::runtime_error("cannot connect to PostgreSQL: " + message);
    }
}

Database::~Database() {
    if (connection_ != nullptr) {
        PQfinish(connection_);
    }
}

nlohmann::json Database::query(const std::string& sql, const std::vector<std::string>& parameters) {
    const std::lock_guard<std::mutex> lock(mutex_);

    // If PostgreSQL was restarted, the old connection is dead: reconnect once.
    if (PQstatus(connection_) != CONNECTION_OK) {
        PQreset(connection_);
    }

    // Parameterized query: the SQL contains placeholders $1, $2, ... and the values are sent
    // separately. Values are never pasted into the SQL text, so a filename like
    // "x'); DROP TABLE runs; --" is stored as a harmless string (no SQL injection).
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const std::string& parameter : parameters) {
        values.push_back(parameter.c_str());
    }
    const ResultPointer result(
        PQexecParams(connection_, sql.c_str(), static_cast<int>(values.size()),
                     /*paramTypes=*/nullptr, values.data(), /*paramLengths=*/nullptr,
                     /*paramFormats=*/nullptr, /*resultFormat=*/0),
        &PQclear);

    const ExecStatusType status = PQresultStatus(result.get());
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw std::runtime_error(std::string("query failed: ") + PQerrorMessage(connection_));
    }

    // Turn the rows into [{column: value, ...}, ...].
    nlohmann::json rows = nlohmann::json::array();
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        nlohmann::json object = nlohmann::json::object();
        for (int column = 0; column < PQnfields(result.get()); ++column) {
            object[PQfname(result.get(), column)] = cell_to_json(result.get(), row, column);
        }
        rows.push_back(std::move(object));
    }
    return rows;
}

long long Database::insert_run(const RunRecord& run) {
    // RETURNING id hands back the id the database generated for the new row.
    const nlohmann::json rows = query(
        "INSERT INTO runs (filename, width, height, num_nodes, num_edges, blur_sigma, "
        "                  sigma_color, weight_threshold, graph_ms, thumbnail) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, NULLIF($10, '')) "
        "RETURNING id",
        {run.filename, std::to_string(run.width), std::to_string(run.height),
         std::to_string(run.num_nodes), std::to_string(run.num_edges),
         format_double(run.blur_sigma), format_double(run.sigma_color),
         format_double(run.weight_threshold), format_double(run.graph_ms), run.thumbnail});
    return rows.at(0).at("id").get<long long>();
}

long long Database::insert_experiment(const ExperimentRecord& experiment) {
    const nlohmann::json rows = query(
        "INSERT INTO experiments (run_id, k, eigenpairs_cached, eigenpairs_computed, "
        "                         factorizations, eigen_ms, kmeans_ms, max_residual, "
        "                         all_converged, eigenvalues, runtime_ms, "
        "                         eigensolver_iterations, kmeans_iterations, memory_bytes) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
        "RETURNING id",
        {std::to_string(experiment.run_id), std::to_string(experiment.k),
         std::to_string(experiment.eigenpairs_cached),
         std::to_string(experiment.eigenpairs_computed),
         std::to_string(experiment.factorizations), format_double(experiment.eigen_ms),
         format_double(experiment.kmeans_ms), format_double(experiment.max_residual),
         experiment.all_converged ? "true" : "false",
         format_double_array(experiment.eigenvalues), format_double(experiment.runtime_ms),
         std::to_string(experiment.eigensolver_iterations),
         std::to_string(experiment.kmeans_iterations),
         std::to_string(experiment.memory_bytes)});
    return rows.at(0).at("id").get<long long>();
}

std::optional<long long> Database::delete_experiment(long long experiment_id) {
    // RETURNING works for DELETE too: it hands back columns of the rows that were removed.
    const nlohmann::json rows = query("DELETE FROM experiments WHERE id = $1 RETURNING run_id",
                                      {std::to_string(experiment_id)});
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0).at("run_id").get<long long>();
}

int Database::clear_experiments(long long run_id) {
    const nlohmann::json rows = query("DELETE FROM experiments WHERE run_id = $1 RETURNING id",
                                      {std::to_string(run_id)});
    return static_cast<int>(rows.size());
}

nlohmann::json Database::list_runs(int limit) {
    // The (inner) JOIN keeps only runs with at least one saved experiment, so runs the user
    // never saved stay out of the history. GROUP BY r.id collapses each run's experiments
    // into one row, so count() and sum() can summarize them.
    return query(
        "SELECT r.id, "
        "       extract(epoch FROM r.created_at) * 1000 AS created_at_ms, "
        "       r.filename, r.width, r.height, r.num_nodes, r.thumbnail, "
        "       count(e.id) AS num_experiments, "
        "       sum(e.runtime_ms) AS total_ms, "
        "       (SELECT e2.k FROM experiments e2 WHERE e2.run_id = r.id "
        "        ORDER BY e2.created_at DESC LIMIT 1) AS last_k "
        "FROM runs r "
        "JOIN experiments e ON e.run_id = r.id "
        "GROUP BY r.id "
        "ORDER BY r.created_at DESC "
        "LIMIT $1",
        {std::to_string(limit)});
}

std::optional<nlohmann::json> Database::get_run(long long run_id) {
    const nlohmann::json runs = query(
        "SELECT id, extract(epoch FROM created_at) * 1000 AS created_at_ms, filename, width, "
        "       height, num_nodes, num_edges, blur_sigma, sigma_color, weight_threshold, "
        "       graph_ms, thumbnail "
        "FROM runs WHERE id = $1",
        {std::to_string(run_id)});
    if (runs.empty()) {
        return std::nullopt;
    }

    // array_to_json turns the DOUBLE PRECISION[] column into JSON, which cell_to_json parses.
    nlohmann::json run = runs.at(0);
    run["experiments"] = query(
        "SELECT id, extract(epoch FROM created_at) * 1000 AS created_at_ms, k, "
        "       eigenpairs_cached, eigenpairs_computed, factorizations, eigen_ms, kmeans_ms, "
        "       max_residual, all_converged, array_to_json(eigenvalues) AS eigenvalues, "
        "       runtime_ms, eigensolver_iterations, kmeans_iterations, memory_bytes "
        "FROM experiments WHERE run_id = $1 ORDER BY created_at",
        {std::to_string(run_id)});
    return run;
}
