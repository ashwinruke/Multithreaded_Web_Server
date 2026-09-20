#include "http.h"
#include <gtest/gtest.h>

namespace {

// Feeds a buffer to the parser and returns the final result.
RequestParser::Result feed(RequestParser& parser, std::string& buffer, const std::string& chunk) {
    buffer += chunk;
    return parser.parse(buffer);
}

std::string simpleRequest(const std::string& target = "/index.html") {
    return "GET " + target + " HTTP/1.1\r\nHost: example.com\r\n\r\n";
}

}  // namespace

TEST(Parser, ParsesMinimalRequest) {
    RequestParser parser;
    std::string buffer = simpleRequest();
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);

    EXPECT_EQ(parser.request().method, "GET");
    EXPECT_EQ(parser.request().path, "/index.html");
    EXPECT_EQ(parser.request().version, "HTTP/1.1");
    EXPECT_EQ(parser.request().header("host"), "example.com");
    EXPECT_TRUE(parser.request().keepAlive);
}

// The core property of a non-blocking server: a request may arrive in any
// number of pieces, split at any byte.
TEST(Parser, HandlesFragmentedArrival) {
    RequestParser parser;
    std::string buffer;
    EXPECT_EQ(feed(parser, buffer, "GET /he"), RequestParser::Result::NeedMore);
    EXPECT_EQ(feed(parser, buffer, "althz HTTP/1.1\r\nHo"), RequestParser::Result::NeedMore);
    EXPECT_EQ(feed(parser, buffer, "st: x\r\n"), RequestParser::Result::NeedMore);
    EXPECT_EQ(feed(parser, buffer, "\r\n"), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().path, "/healthz");
}

TEST(Parser, SplitAtEveryByteStillParses) {
    const std::string request = simpleRequest("/a");
    for (size_t split = 1; split < request.size(); ++split) {
        RequestParser parser;
        std::string buffer;
        feed(parser, buffer, request.substr(0, split));
        const RequestParser::Result result = feed(parser, buffer, request.substr(split));
        ASSERT_EQ(result, RequestParser::Result::Complete) << "failed at split " << split;
        EXPECT_EQ(parser.request().path, "/a");
    }
}

TEST(Parser, ParsesPipelinedRequests) {
    RequestParser parser;
    std::string buffer = simpleRequest("/one") + simpleRequest("/two");

    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().path, "/one");

    parser.reset();
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().path, "/two");
    EXPECT_TRUE(buffer.empty());
}

TEST(Parser, ReadsBodyByContentLength) {
    RequestParser parser;
    std::string buffer;
    feed(parser, buffer, "POST /api/kv HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello");
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::NeedMore);
    ASSERT_EQ(feed(parser, buffer, " world"), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().body, "hello world");
}

TEST(Parser, StopsBodyAtContentLength) {
    RequestParser parser;
    std::string buffer = "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\nabcdEXTRA";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().body, "abcd");
    EXPECT_EQ(buffer, "EXTRA");  // leftovers belong to the next request
}

TEST(Parser, HeaderNamesAreCaseInsensitive) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/1.1\r\nHOST: x\r\nContent-Type: text/plain\r\n\r\n";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().header("Host"), "x");
    EXPECT_EQ(parser.request().header("CONTENT-TYPE"), "text/plain");
}

TEST(Parser, TrimsHeaderValues) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/1.1\r\nHost:    spaced   \r\n\r\n";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().header("host"), "spaced");
}

TEST(Parser, CombinesRepeatedHeaders) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/1.1\r\nAccept: a\r\nAccept: b\r\n\r\n";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().header("accept"), "a, b");
}

TEST(Parser, SearchesHeadersNotBody) {
    RequestParser parser;
    std::string buffer = "POST / HTTP/1.1\r\nContent-Length: 19\r\n\r\nHost: injected\r\nx=1";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().header("host"), "");  // body must not be scanned
}

TEST(Parser, StripsQueryFromPath) {
    RequestParser parser;
    std::string buffer = simpleRequest("/api/kv?key=a&x=1");
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().path, "/api/kv");
    EXPECT_EQ(parser.request().query, "key=a&x=1");
    EXPECT_EQ(parser.request().queryParam("key"), "a");
    EXPECT_EQ(parser.request().queryParam("missing"), "");
}

TEST(Parser, DecodesPercentEncodedQuery) {
    RequestParser parser;
    std::string buffer = simpleRequest("/api/kv?key=a%26b%3Dc");
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().queryParam("key"), "a&b=c");
}

TEST(Parser, KeepAliveDefaultsByVersion) {
    {
        RequestParser parser;
        std::string buffer = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
        ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
        EXPECT_FALSE(parser.request().keepAlive);
    }
    {
        RequestParser parser;
        std::string buffer = "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n";
        ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
        EXPECT_TRUE(parser.request().keepAlive);
    }
    {
        RequestParser parser;
        std::string buffer = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
        ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
        EXPECT_FALSE(parser.request().keepAlive);
    }
}

TEST(Parser, RejectsMalformedRequestLine) {
    for (const std::string bad : {"GARBAGE\r\n\r\n", "GET\r\n\r\n", "GET /\r\n\r\n"}) {
        RequestParser parser;
        std::string buffer = bad;
        EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error) << bad;
        EXPECT_EQ(parser.errorStatus(), 400);
    }
}

TEST(Parser, RejectsNonAbsolutePath) {
    RequestParser parser;
    std::string buffer = "GET index.html HTTP/1.1\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 400);
}

TEST(Parser, RejectsUnsupportedVersion) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/2.0\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 505);
}

TEST(Parser, RejectsChunkedEncoding) {
    RequestParser parser;
    std::string buffer = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 501);
}

TEST(Parser, RejectsNonNumericContentLength) {
    RequestParser parser;
    std::string buffer = "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 400);
}

TEST(Parser, RejectsMissingHeaderColon) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/1.1\r\nBrokenHeader\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 400);
}

// Resource limits: without these one client can exhaust server memory.
TEST(Parser, RejectsOversizedBody) {
    RequestParser parser;
    std::string buffer = "POST / HTTP/1.1\r\nContent-Length: " +
                         std::to_string(RequestParser::maxBody + 1) + "\r\n\r\n";
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 413);
}

TEST(Parser, RejectsOversizedRequestLine) {
    RequestParser parser;
    std::string buffer = "GET /" + std::string(RequestParser::maxRequestLine + 10, 'a');
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 414);
}

TEST(Parser, RejectsOversizedHeaderBlock) {
    RequestParser parser;
    std::string buffer = "GET / HTTP/1.1\r\n";
    for (int i = 0; i < 400; ++i) {
        buffer += "X-Pad-" + std::to_string(i) + ": " + std::string(60, 'v') + "\r\n";
    }
    EXPECT_EQ(parser.parse(buffer), RequestParser::Result::Error);
    EXPECT_EQ(parser.errorStatus(), 431);
}

TEST(Parser, ResetClearsPreviousRequest) {
    RequestParser parser;
    std::string buffer = "POST /a HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc";
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    parser.reset();

    buffer = simpleRequest("/b");
    ASSERT_EQ(parser.parse(buffer), RequestParser::Result::Complete);
    EXPECT_EQ(parser.request().method, "GET");
    EXPECT_EQ(parser.request().path, "/b");
    EXPECT_TRUE(parser.request().body.empty());
}

TEST(Response, SerializesHeadersAndBody) {
    HttpResponse response = HttpResponse::json(R"({"a":1})");
    const std::string wire = response.serialize();

    EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Content-Length: 7\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Content-Type: application/json\r\n"), std::string::npos);
    EXPECT_NE(wire.find("\r\n\r\n{\"a\":1}"), std::string::npos);
}

// HEAD must be byte-identical to GET in its headers, including Content-Length.
TEST(Response, HeadOmitsBodyButKeepsContentLength) {
    HttpResponse response = HttpResponse::text("hello world");
    const std::string wire = response.serialize(true);

    EXPECT_NE(wire.find("Content-Length: 11\r\n"), std::string::npos);
    EXPECT_EQ(wire.find("hello world"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\n"));
}

TEST(Response, ProtocolErrorsCloseConnection) {
    EXPECT_FALSE(HttpResponse::error(400).keepAlive);
    EXPECT_FALSE(HttpResponse::error(413).keepAlive);
    EXPECT_TRUE(HttpResponse::error(404).keepAlive);  // recoverable, keep reusing
}
