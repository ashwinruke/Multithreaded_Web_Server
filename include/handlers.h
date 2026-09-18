#pragma once
#include "file_manager.h"
#include "router.h"
#include <string>

// Serves files under staticRoot. Request paths are canonicalized and must stay
// inside that root, so traversal attempts get 403 rather than the file.
class StaticFileHandler {
public:
    StaticFileHandler(std::string staticRoot, int cacheCapacity);
    HttpResponse operator()(const HttpRequest& request);
    const FileManager& files() const { return fileManager; }
    FileManager& files() { return fileManager; }

private:
    std::string staticRoot;
    FileManager fileManager;

    std::string resolve(const std::string& path, bool& escaped) const;
    static std::string mimeType(const std::string& filePath);
};
