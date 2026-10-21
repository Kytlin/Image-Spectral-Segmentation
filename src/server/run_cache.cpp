#include "run_cache.hpp"

#include <stdexcept>

RunCache::RunCache(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("RunCache: capacity must be > 0");
    }
}

void RunCache::insert(long long run_id, std::shared_ptr<CachedRun> run) {
    const std::lock_guard<std::mutex> lock(mutex_);

    // Replacing an existing entry: drop the old one first.
    const auto existing = entries_.find(run_id);
    if (existing != entries_.end()) {
        recency_.erase(existing->second.second);
        entries_.erase(existing);
    }

    // Full: evict the least recently used run (the back of the list).
    if (entries_.size() >= capacity_) {
        entries_.erase(recency_.back());
        recency_.pop_back();
    }

    recency_.push_front(run_id);
    entries_[run_id] = {std::move(run), recency_.begin()};
}

std::shared_ptr<CachedRun> RunCache::find(long long run_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = entries_.find(run_id);
    if (entry == entries_.end()) {
        return nullptr;
    }
    // Move this id to the front of the recency list. splice relinks the node in O(1),
    // so the iterator stored in the map stays valid.
    recency_.splice(recency_.begin(), recency_, entry->second.second);
    return entry->second.first;
}

bool RunCache::contains(long long run_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return entries_.count(run_id) > 0;
}
