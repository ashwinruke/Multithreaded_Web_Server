#include "http_response.h"
#include <sstream>

const std::unordered_map<std::string, std::string> HttpResponse::mimeTypes = {
    { "html", "text/html" },
    { "css",  "text/css" },
    { "js",   "text/javascript" },
    { "json", "application/json" },
    { "png",  "image/png" },
    { "jpg",  "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "gif",  "image/gif" },
    { "svg",  "image/svg+xml" },
    { "ico",  "image/x-icon" }
};

const std::unordered_map<int, std::string> HttpResponse::statusMessages = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {501, "Not Implemented"}
};

HttpResponse::HttpResponse(int cacheCapacity)
    : file_manager(cacheCapacity) {}

std::string HttpResponse::makeResponse(const std::string& request) {
    const std::string method = http_parser.getHttpMethod(request);
    if (method.empty()) {
        return makeErrorResponse(405);
    }
    if (method == "POST") {
        // Dynamic endpoints land here in a later milestone.
        return makeErrorResponse(501);
    }
    return handleGetHead(request, method);
}

std::string HttpResponse::handleGetHead(const std::string& request, const std::string& method) {
    const std::string endpoint = http_parser.getEndpoint(request);
    if (endpoint.empty()) {
        return makeErrorResponse(400);
    }

    const std::string filePath = http_parser.resolveStaticPath(endpoint);
    if (filePath.empty()) {
        // Escaped the static root, e.g. GET /../../etc/passwd
        return makeErrorResponse(403);
    }

    bool found = false;
    const std::string content = file_manager.readFile(filePath, found);
    if (!found) {
        return makeErrorResponse(404);
    }
    return makeSuccessResponse(200, method, getMimeType(filePath), content);
}

std::string HttpResponse::getMimeType(const std::string& filePath) {
    const size_t dotIndex = filePath.find_last_of('.');
    if (dotIndex == std::string::npos) {
        return "text/plain";
    }
    std::string extension = filePath.substr(dotIndex + 1);
    auto iterator = mimeTypes.find(extension);
    return (iterator != mimeTypes.end()) ? iterator->second : "text/plain";
}

std::string HttpResponse::makeSuccessResponse(int statusCode, const std::string& method, const std::string& contentType, const std::string& content) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << statusCode << " " << statusMessages.at(statusCode) << "\r\n";
    stream << "Content-Length: " << content.size() << "\r\n";
    stream << "Content-Type: " << contentType << "\r\n";
    stream << "Connection: close\r\n\r\n";
    if (method != "HEAD") {
        stream << content;
    }
    return stream.str();
}

std::string HttpResponse::makeErrorResponse(int statusCode) {
    std::ostringstream body;
    body << "<html><body><h1>" << statusCode << " " << statusMessages.at(statusCode) << "</h1></body></html>";

    std::ostringstream stream;
    stream << "HTTP/1.1 " << statusCode << " " << statusMessages.at(statusCode) << "\r\n";
    stream << "Content-Length: " << body.str().size() << "\r\n";
    stream << "Content-Type: text/html\r\n";
    if (statusCode == 405) {
        stream << "Allow: " << HttpParser::supportedMethodsStr << "\r\n";
    }
    stream << "Connection: close\r\n\r\n" << body.str();
    return stream.str();
}
