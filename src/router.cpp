#include "router.h"

void Router::add(const std::string& method, const std::string& path, Handler handler) {
    routes[{method, path}] = std::move(handler);
    knownPaths.insert(path);
}

void Router::setFallback(Handler handler) {
    fallback = std::move(handler);
}

HttpResponse Router::route(const HttpRequest& request) const {
    // HEAD is served by the GET handler; the caller drops the body.
    const std::string method = (request.method == "HEAD") ? "GET" : request.method;

    auto iterator = routes.find({method, request.path});
    if (iterator != routes.end()) {
        return iterator->second(request);
    }

    if (knownPaths.count(request.path) > 0) {
        HttpResponse response = HttpResponse::error(405);
        std::string allowed;
        for (const auto& [key, handler] : routes) {
            if (key.second == request.path) {
                if (!allowed.empty()) {
                    allowed += ", ";
                }
                allowed += key.first;
                if (key.first == "GET") {
                    allowed += ", HEAD";
                }
            }
        }
        response.extraHeaders.emplace_back("Allow", allowed);
        return response;
    }

    if (fallback) {
        return fallback(request);
    }
    return HttpResponse::error(404);
}
