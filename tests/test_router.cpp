#include "router.h"
#include <gtest/gtest.h>

namespace {
HttpRequest makeRequest(const std::string& method, const std::string& path) {
    HttpRequest request;
    request.method = method;
    request.path = path;
    request.version = "HTTP/1.1";
    return request;
}
}  // namespace

TEST(Router, DispatchesExactPath) {
    Router router;
    router.add("GET", "/healthz", [](const HttpRequest&) { return HttpResponse::text("alive"); });

    const HttpResponse response = router.route(makeRequest("GET", "/healthz"));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "alive");
}

TEST(Router, UnknownPathFallsBack) {
    Router router;
    router.setFallback([](const HttpRequest&) { return HttpResponse::text("static", 200); });
    EXPECT_EQ(router.route(makeRequest("GET", "/whatever")).body, "static");
}

TEST(Router, UnknownPathWithoutFallbackIs404) {
    Router router;
    EXPECT_EQ(router.route(makeRequest("GET", "/whatever")).status, 404);
}

// A path that exists under another method is 405, not 404 - and RFC 9110
// requires the Allow header on that response.
TEST(Router, WrongMethodIs405WithAllowHeader) {
    Router router;
    router.add("POST", "/api/kv", [](const HttpRequest&) { return HttpResponse::text("ok"); });

    const HttpResponse response = router.route(makeRequest("DELETE", "/api/kv"));
    ASSERT_EQ(response.status, 405);

    bool foundAllow = false;
    for (const auto& [name, value] : response.extraHeaders) {
        if (name == "Allow") {
            foundAllow = true;
            EXPECT_NE(value.find("POST"), std::string::npos);
        }
    }
    EXPECT_TRUE(foundAllow);
}

TEST(Router, AllowListsEveryRegisteredMethod) {
    Router router;
    router.add("GET", "/api/kv", [](const HttpRequest&) { return HttpResponse::text("g"); });
    router.add("POST", "/api/kv", [](const HttpRequest&) { return HttpResponse::text("p"); });

    const HttpResponse response = router.route(makeRequest("PATCH", "/api/kv"));
    ASSERT_EQ(response.status, 405);
    ASSERT_FALSE(response.extraHeaders.empty());

    const std::string allow = response.extraHeaders.front().second;
    EXPECT_NE(allow.find("GET"), std::string::npos);
    EXPECT_NE(allow.find("HEAD"), std::string::npos);  // implied by GET
    EXPECT_NE(allow.find("POST"), std::string::npos);
}

TEST(Router, HeadIsServedByGetHandler) {
    Router router;
    int calls = 0;
    router.add("GET", "/page", [&calls](const HttpRequest&) {
        ++calls;
        return HttpResponse::text("body");
    });

    const HttpResponse response = router.route(makeRequest("HEAD", "/page"));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(calls, 1);
}

TEST(Router, MethodsOnSamePathAreIndependent) {
    Router router;
    router.add("GET", "/api/kv", [](const HttpRequest&) { return HttpResponse::text("list"); });
    router.add("POST", "/api/kv", [](const HttpRequest&) { return HttpResponse::text("create", 201); });

    EXPECT_EQ(router.route(makeRequest("GET", "/api/kv")).body, "list");
    EXPECT_EQ(router.route(makeRequest("POST", "/api/kv")).status, 201);
}

TEST(Router, HandlerReceivesTheRequest) {
    Router router;
    router.add("GET", "/echo", [](const HttpRequest& request) {
        return HttpResponse::text(request.queryParam("msg"));
    });

    HttpRequest request = makeRequest("GET", "/echo");
    request.query = "msg=hello";
    EXPECT_EQ(router.route(request).body, "hello");
}
