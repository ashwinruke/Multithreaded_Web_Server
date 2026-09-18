#pragma once
#include "http.h"
#include <functional>
#include <map>
#include <set>
#include <string>

using Handler = std::function<HttpResponse(const HttpRequest&)>;

// Exact-path routing table with a fallback (used for static files).
// A path that exists under a different method yields 405 with an Allow header,
// which is what separates a real router from a chain of if-statements.
class Router {
public:
    void add(const std::string& method, const std::string& path, Handler handler);
    void setFallback(Handler handler);
    HttpResponse route(const HttpRequest& request) const;

private:
    std::map<std::pair<std::string, std::string>, Handler> routes;  // (method, path)
    std::set<std::string> knownPaths;
    Handler fallback;
};
