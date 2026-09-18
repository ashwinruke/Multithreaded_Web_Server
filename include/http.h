#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

const char* statusMessage(int statusCode);

struct HttpRequest {
    std::string method;
    std::string target;   // raw request target, query included
    std::string path;     // target with the query stripped
    std::string query;
    std::string version;
    std::unordered_map<std::string, std::string> headers;  // names lowercased
    std::string body;
    bool keepAlive = true;

    std::string header(const std::string& name) const;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/html";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extraHeaders;
    bool keepAlive = true;

    static HttpResponse json(const std::string& payload, int status = 200);
    static HttpResponse text(const std::string& payload, int status = 200);
    static HttpResponse error(int status);

    // omitBody is set for HEAD: headers are identical, the body is dropped.
    std::string serialize(bool omitBody = false) const;
};

// Incremental HTTP/1.1 request parser.
//
// Bytes arrive in arbitrary chunks: half a request line in one read, three
// pipelined requests in the next. parse() consumes whatever is complete from
// the front of the buffer and keeps its state between calls, so the caller
// just feeds it everything it has read so far.
class RequestParser {
public:
    enum class Result { NeedMore, Complete, Error };

    Result parse(std::string& buffer);
    HttpRequest& request() { return req; }
    int errorStatus() const { return error; }
    void reset();

    static constexpr size_t maxRequestLine = 8 * 1024;
    static constexpr size_t maxHeaderBlock = 16 * 1024;
    static constexpr size_t maxBody = 1024 * 1024;

private:
    enum class State { RequestLine, Headers, Body, Done };

    State state = State::RequestLine;
    HttpRequest req;
    size_t contentLength = 0;
    size_t headerBytes = 0;
    int error = 400;

    bool parseRequestLine(const std::string& line);
    bool finishHeaders();
};
