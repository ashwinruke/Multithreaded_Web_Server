#include "lru_cache.h"

LRUCache::LRUCache(size_t capacity)
    : capacity(capacity == 0 ? 1 : capacity) {}

bool LRUCache::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto iterator = index.find(key);
    if (iterator == index.end()) {
        ++missCount;
        return false;
    }
    // Move the hit entry to the front without copying it.
    entries.splice(entries.begin(), entries, iterator->second);
    value = iterator->second->second;
    ++hitCount;
    return true;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto iterator = index.find(key);
    if (iterator != index.end()) {
        iterator->second->second = value;
        entries.splice(entries.begin(), entries, iterator->second);
        return;
    }

    entries.emplace_front(key, value);
    index[key] = entries.begin();

    if (index.size() > capacity) {
        index.erase(entries.back().first);
        entries.pop_back();
    }
}

size_t LRUCache::hits() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return hitCount;
}

size_t LRUCache::misses() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return missCount;
}
