#pragma once
#include <string>
#include <unordered_set>

class HttpParser {
public:
    static const std::string supportedMethodsStr;

    static std::string getStartLine(const std::string& request);
    static std::string getHeaderFieldVal(const std::string& message, const std::string& field);
    static std::string getHttpMethod(const std::string& request);
    static std::string getEndpoint(const std::string& request);
    static std::string getPayload(const std::string& request);
    static std::string getResponseCode(const std::string& response);

    // Maps a URL path to a file under the static root. Returns "" if the
    // result would escape that root (path traversal) or the file is missing.
    static std::string resolveStaticPath(const std::string& endpoint);
    static void setStaticRoot(const std::string& root);

private:
    static const std::unordered_set<std::string> supportedMethodsSet;
    static std::string staticRoot;
};
