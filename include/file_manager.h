#pragma once
#include "lru_cache.h"
#include <string>

class FileManager {
public:
    explicit FileManager(int cacheCapacity);
    // Sets found=false only when the file could not be read, so a legitimately
    // empty file still returns 200 rather than 404.
    std::string readFile(const std::string& filePath, bool& found);

    size_t cacheHits() const { return cache.hits(); }
    size_t cacheMisses() const { return cache.misses(); }

private:
    LRUCache cache;
};
