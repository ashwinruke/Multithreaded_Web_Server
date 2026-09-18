#include "http_parser.h"
#include "utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

const std::string HttpParser::supportedMethodsStr = "GET, HEAD, POST";
const std::unordered_set<std::string> HttpParser::supportedMethodsSet = {"GET", "HEAD", "POST"};
std::string HttpParser::staticRoot;

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
}  // namespace

void HttpParser::setStaticRoot(const std::string& root) {
    std::error_code errorCode;
    std::filesystem::path canonical = std::filesystem::canonical(root, errorCode);
    staticRoot = errorCode ? root : canonical.string();
}

std::string HttpParser::getStartLine(const std::string& request) {
    const size_t endStartLine = request.find("\r\n");
    return (endStartLine == std::string::npos) ? "" : request.substr(0, endStartLine);
}

std::string HttpParser::getHeaderFieldVal(const std::string& message, const std::string& field) {
    // Headers end at the first blank line; never search the body.
    size_t headerEnd = message.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = message.size();
    }
    const std::string headers = toLower(message.substr(0, headerEnd));
    const std::string needle = "\r\n" + toLower(field) + ":";

    size_t position = headers.find(needle);
    if (position == std::string::npos) {
        return "";
    }
    size_t valueStart = position + needle.size();
    size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = headers.size();
    }
    // Slice the original string so the value keeps its original case.
    std::string value = message.substr(valueStart, valueEnd - valueStart);
    const size_t firstChar = value.find_first_not_of(" \t");
    if (firstChar == std::string::npos) {
        return "";
    }
    const size_t lastChar = value.find_last_not_of(" \t");
    return value.substr(firstChar, lastChar - firstChar + 1);
}

std::string HttpParser::getHttpMethod(const std::string& request) {
    const size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) {
        return "";
    }
    const std::string method = request.substr(0, firstSpace);
    return (supportedMethodsSet.count(method) > 0) ? method : "";
}

std::string HttpParser::getEndpoint(const std::string& request) {
    const size_t startEndpoint = request.find(' ');
    if (startEndpoint == std::string::npos) {
        return "";
    }
    const size_t endEndpoint = request.find(' ', startEndpoint + 1);
    if (endEndpoint == std::string::npos) {
        return "";
    }
    std::string endpoint = request.substr(startEndpoint + 1, endEndpoint - startEndpoint - 1);

    const size_t queryStart = endpoint.find('?');
    if (queryStart != std::string::npos) {
        endpoint = endpoint.substr(0, queryStart);
    }
    return endpoint;
}

std::string HttpParser::getPayload(const std::string& request) {
    const size_t bodyStart = request.find("\r\n\r\n");
    return (bodyStart == std::string::npos) ? "" : request.substr(bodyStart + 4);
}

std::string HttpParser::getResponseCode(const std::string& response) {
    const size_t firstSpace = response.find(' ');
    if (firstSpace == std::string::npos || response.size() < firstSpace + 4) {
        return "";
    }
    return response.substr(firstSpace + 1, 3);
}

std::string HttpParser::resolveStaticPath(const std::string& endpoint) {
    if (endpoint.empty() || endpoint[0] != '/') {
        return "";
    }
    std::string relative = (endpoint == "/") ? "/index.html" : endpoint;

    std::error_code errorCode;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(std::filesystem::path(staticRoot) / relative.substr(1), errorCode);
    if (errorCode) {
        return "";
    }

    // Reject anything that canonicalizes outside the static root.
    const std::string resolved = candidate.string();
    if (resolved.rfind(staticRoot, 0) != 0) {
        return "";
    }
    // Existence is not checked here: a path inside the root that is missing
    // must produce 404, while an escape attempt produces 403.
    return resolved;
}
