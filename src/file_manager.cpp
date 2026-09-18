#include "file_manager.h"
#include <filesystem>
#include <fstream>
#include <sstream>

FileManager::FileManager(int cacheCapacity)
    : cache(cacheCapacity) {}

std::string FileManager::readFile(const std::string& filePath, bool& found) {
    std::string cachedContent;
    if (cache.get(filePath, cachedContent)) {
        found = true;
        return cachedContent;
    }

    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(filePath, errorCode)) {
        found = false;
        return "";
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        found = false;
        return "";
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    std::string content = stream.str();
    cache.put(filePath, content);
    found = true;
    return content;
}
