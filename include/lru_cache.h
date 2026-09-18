#pragma once
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

// Thread-safe LRU cache. std::list gives O(1) splice to the front; the map
// stores iterators into that list, so lookup and eviction are both O(1).
class LRUCache {
public:
    explicit LRUCache(size_t capacity);

    bool get(const std::string& key, std::string& value);
    void put(const std::string& key, const std::string& value);

    size_t hits() const;
    size_t misses() const;

private:
    using Entry = std::pair<std::string, std::string>;  // key, value

    size_t capacity;
    std::list<Entry> entries;                                            // front = most recent
    std::unordered_map<std::string, std::list<Entry>::iterator> index;
    mutable std::mutex cacheMutex;
    size_t hitCount = 0;
    size_t missCount = 0;
};
