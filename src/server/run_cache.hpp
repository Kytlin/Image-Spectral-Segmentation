#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database.hpp"
#include "eigensolver.hpp"
#include "graph.hpp"

// Everything the server keeps in memory for one run, so that moving the k slider only costs
// k-means (plus new eigenpairs if k grew), not a new graph and eigensolve.
struct CachedRun {
    // HTTP requests run on several threads. Two slider requests for the same run must not
    // extend `eigenpairs` at the same time, so whoever uses this run locks this mutex first.
    std::mutex mutex;
    SparseMatrix normalized; // L_sym = I - M, where M = D^{-1/2} W D^{-1/2}
    std::vector<EigenpairResult> eigenpairs;  // grows as larger k values are requested

    // The latest segmentation result, kept until the user saves it to the experiment log.
    // Each new result gets the next id, so a save request can name the result it means.
    long long last_result_id = 0;
    std::optional<ExperimentRecord> last_result;
    bool last_result_saved = false;
    long long saved_experiment_id = 0;  // its row in the experiments table, once saved
};

// A map run_id -> CachedRun that holds at most `capacity` runs. When it is full, inserting
// evicts the least recently used run. Safe to call from several threads.
//
// Entries are shared_ptr: a request that is still working on a run keeps it alive even if
// another request evicts it from the map in the meantime.
class RunCache {
public:
    explicit RunCache(std::size_t capacity);

    void insert(long long run_id, std::shared_ptr<CachedRun> run);

    // nullptr if the run isn't cached (never existed, evicted, or the server restarted).
    // A hit marks the run as most recently used.
    std::shared_ptr<CachedRun> find(long long run_id);

    bool contains(long long run_id);

private:
    using RecencyList = std::list<long long>;  // run ids, most recently used first

    std::size_t capacity_;
    std::mutex mutex_;  // guards both members below
    RecencyList recency_;
    std::unordered_map<long long, std::pair<std::shared_ptr<CachedRun>, RecencyList::iterator>>
        entries_;
};
