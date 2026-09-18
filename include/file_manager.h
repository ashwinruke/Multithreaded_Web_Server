#pragma once
#include "lru_cache.h"
#include <string>

class FileManager {
public:
    explicit FileManager(int cacheCapacity);
    
    std::string readFile(const std::string& filePath, bool& found);

private:
    LRUCache cache;
};
