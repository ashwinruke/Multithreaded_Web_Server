#include "handlers.h"
#include <filesystem>
#include <unordered_map>

namespace {
const std::unordered_map<std::string, std::string> kMimeTypes = {
    { "html", "text/html" },
    { "css",  "text/css" },
    { "js",   "text/javascript" },
    { "json", "application/json" },
    { "png",  "image/png" },
    { "jpg",  "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "gif",  "image/gif" },
    { "svg",  "image/svg+xml" },
    { "ico",  "image/x-icon" },
    { "txt",  "text/plain" }
};
}  // namespace

StaticFileHandler::StaticFileHandler(std::string staticRoot, int cacheCapacity)
    : staticRoot(std::move(staticRoot)), fileManager(cacheCapacity) {
    std::error_code errorCode;
    std::filesystem::path canonical = std::filesystem::canonical(this->staticRoot, errorCode);
    if (!errorCode) {
        this->staticRoot = canonical.string();
    }
}

std::string StaticFileHandler::resolve(const std::string& path, bool& escaped) const {
    escaped = false;
    if (path.empty() || path[0] != '/') {
        escaped = true;
        return "";
    }

    std::string relative = (path.back() == '/') ? path + "index.html" : path;

    std::error_code errorCode;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(std::filesystem::path(staticRoot) / relative.substr(1), errorCode);
    if (errorCode) {
        escaped = true;
        return "";
    }

    const std::string resolved = candidate.string();
    if (resolved.rfind(staticRoot, 0) != 0) {
        escaped = true;
        return "";
    }
    return resolved;
}

HttpResponse StaticFileHandler::operator()(const HttpRequest& request) {
    bool escaped = false;
    const std::string filePath = resolve(request.path, escaped);
    if (escaped) {
        return HttpResponse::error(403);
    }

    bool found = false;
    const std::string content = fileManager.readFile(filePath, found);
    if (!found) {
        return HttpResponse::error(404);
    }

    HttpResponse response;
    response.contentType = mimeType(filePath);
    response.body = content;
    return response;
}

std::string StaticFileHandler::mimeType(const std::string& filePath) {
    const size_t dotIndex = filePath.find_last_of('.');
    if (dotIndex == std::string::npos) {
        return "application/octet-stream";
    }
    auto iterator = kMimeTypes.find(filePath.substr(dotIndex + 1));
    return (iterator != kMimeTypes.end()) ? iterator->second : "application/octet-stream";
}
