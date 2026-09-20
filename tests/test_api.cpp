#include "api.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {
HttpRequest postBody(const std::string& body, const std::string& contentType) {
    HttpRequest request;
    request.method = "POST";
    request.path = "/api/kv";
    request.body = body;
    request.headers["content-type"] = contentType;
    return request;
}
}  // namespace

TEST(UrlDecode, DecodesPercentAndPlus) {
    EXPECT_EQ(urlDecode("a%26b%3Dc"), "a&b=c");
    EXPECT_EQ(urlDecode("hello+world"), "hello world");
    EXPECT_EQ(urlDecode("%20space"), " space");
    EXPECT_EQ(urlDecode("plain"), "plain");
}

TEST(UrlDecode, LeavesInvalidEscapesAlone) {
    EXPECT_EQ(urlDecode("100%"), "100%");
    EXPECT_EQ(urlDecode("%zz"), "%zz");
}

TEST(FormEncoded, ParsesPairs) {
    const auto fields = parseFormEncoded("key=a&value=b+c");
    ASSERT_EQ(fields.count("key"), 1u);
    EXPECT_EQ(fields.at("key"), "a");
    EXPECT_EQ(fields.at("value"), "b c");
}

TEST(FormEncoded, IgnoresMalformedSegments) {
    const auto fields = parseFormEncoded("novalue&key=a");
    EXPECT_EQ(fields.count("novalue"), 0u);
    EXPECT_EQ(fields.at("key"), "a");
}

TEST(FlatJson, ParsesObject) {
    bool ok = false;
    const auto fields = parseFlatJson(R"({"key":"a","value":"b"})", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(fields.at("key"), "a");
    EXPECT_EQ(fields.at("value"), "b");
}

TEST(FlatJson, HandlesEscapesAndWhitespace) {
    bool ok = false;
    const auto fields = parseFlatJson("{ \"key\" : \"say \\\"hi\\\"\" }", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(fields.at("key"), "say \"hi\"");
}

TEST(FlatJson, AcceptsEmptyObject) {
    bool ok = false;
    const auto fields = parseFlatJson("{}", ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(fields.empty());
}

TEST(FlatJson, RejectsMalformedInput) {
    for (const std::string bad : {R"({"key":)", "not json", R"({"key" "value"})", R"({"unterminated":"x)"}) {
        bool ok = true;
        parseFlatJson(bad, ok);
        EXPECT_FALSE(ok) << bad;
    }
}

TEST(JsonEscape, EscapesControlCharacters) {
    EXPECT_EQ(jsonEscape(R"(he said "hi")"), R"(he said \"hi\")");
    EXPECT_EQ(jsonEscape("line\nbreak"), "line\\nbreak");
    EXPECT_EQ(jsonEscape("back\\slash"), "back\\\\slash");
}

TEST(KvStore, CreateThenReplace) {
    KvStore store;
    EXPECT_EQ(store.put(postBody(R"({"key":"a","value":"1"})", "application/json")).status, 201);
    EXPECT_EQ(store.put(postBody(R"({"key":"a","value":"2"})", "application/json")).status, 200);

    const HttpResponse got = store.get("a");
    EXPECT_EQ(got.status, 200);
    EXPECT_NE(got.body.find(R"("value":"2")"), std::string::npos);
}

TEST(KvStore, AcceptsFormEncodedBodies) {
    KvStore store;
    ASSERT_EQ(store.put(postBody("key=f&value=b+c", "application/x-www-form-urlencoded")).status, 201);
    EXPECT_NE(store.get("f").body.find(R"("value":"b c")"), std::string::npos);
}

TEST(KvStore, MissingKeyIs400) {
    KvStore store;
    EXPECT_EQ(store.put(postBody(R"({"value":"orphan"})", "application/json")).status, 400);
    EXPECT_EQ(store.get("").status, 400);
    EXPECT_EQ(store.remove("").status, 400);
}

TEST(KvStore, MalformedJsonIs400) {
    KvStore store;
    EXPECT_EQ(store.put(postBody(R"({"key":)", "application/json")).status, 400);
}

TEST(KvStore, AbsentKeyIs404) {
    KvStore store;
    EXPECT_EQ(store.get("ghost").status, 404);
    EXPECT_EQ(store.remove("ghost").status, 404);
}

TEST(KvStore, EnforcesLengthLimits) {
    KvStore store;
    const std::string longKey(KvStore::maxKeyLength + 1, 'k');
    const std::string longValue(KvStore::maxValueLength + 1, 'v');

    EXPECT_EQ(store.put(postBody("key=" + longKey + "&value=x", "form")).status, 413);
    EXPECT_EQ(store.put(postBody("key=x&value=" + longValue, "form")).status, 413);
}

// The endpoint is public on the deployed instance, so the store must refuse to
// grow without bound.
TEST(KvStore, RefusesToGrowPastCapacity) {
    KvStore store;
    for (size_t i = 0; i < KvStore::maxEntries; ++i) {
        ASSERT_EQ(store.put(postBody("key=k" + std::to_string(i) + "&value=v", "form")).status, 201);
    }
    EXPECT_EQ(store.put(postBody("key=overflow&value=v", "form")).status, 507);

    // Replacing an existing key must still work when full.
    EXPECT_EQ(store.put(postBody("key=k0&value=updated", "form")).status, 200);
}

TEST(KvStore, DeleteFreesCapacity) {
    KvStore store;
    for (size_t i = 0; i < KvStore::maxEntries; ++i) {
        store.put(postBody("key=k" + std::to_string(i) + "&value=v", "form"));
    }
    ASSERT_EQ(store.remove("k0").status, 200);
    EXPECT_EQ(store.put(postBody("key=fresh&value=v", "form")).status, 201);
}

TEST(KvStore, ListReportsCount) {
    KvStore store;
    store.put(postBody("key=a&value=1", "form"));
    store.put(postBody("key=b&value=2", "form"));

    const HttpResponse response = store.list();
    EXPECT_EQ(response.status, 200);
    EXPECT_NE(response.body.find(R"("count":2)"), std::string::npos);
    EXPECT_NE(response.body.find(R"("key":"a")"), std::string::npos);
}

TEST(KvStore, EscapesValuesInOutput) {
    KvStore store;
    store.put(postBody(R"({"key":"q","value":"a\"b"})", "application/json"));
    EXPECT_NE(store.get("q").body.find(R"(a\"b)"), std::string::npos);
}

// Every worker thread hits the same store; concurrent writers to one key must
// leave exactly one valid entry.
TEST(KvStore, ConcurrentWritersLeaveConsistentState) {
    KvStore store;
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&store, t] {
            for (int i = 0; i < 100; ++i) {
                store.put(postBody("key=shared&value=v" + std::to_string(t * 100 + i), "form"));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const HttpResponse response = store.get("shared");
    EXPECT_EQ(response.status, 200);
    EXPECT_NE(response.body.find(R"("key":"shared")"), std::string::npos);
    EXPECT_NE(store.list().body.find(R"("count":1)"), std::string::npos);
}
