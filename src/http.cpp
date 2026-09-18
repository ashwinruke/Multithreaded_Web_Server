#include "http.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}
}  // namespace

const char* statusMessage(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}

std::string HttpRequest::header(const std::string& name) const {
    auto iterator = headers.find(toLower(name));
    return (iterator == headers.end()) ? "" : iterator->second;
}

HttpResponse HttpResponse::json(const std::string& payload, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json";
    response.body = payload;
    return response;
}

HttpResponse HttpResponse::text(const std::string& payload, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/plain";
    response.body = payload;
    return response;
}

HttpResponse HttpResponse::error(int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/html";
    std::ostringstream body;
    body << "<html><body><h1>" << status << " " << statusMessage(status) << "</h1></body></html>";
    response.body = body.str();
    if (status == 400 || status == 408 || status == 413 || status == 431 || status == 505) {
        response.keepAlive = false;  // connection state is no longer trustworthy
    }
    return response;
}

std::string HttpResponse::serialize(bool omitBody) const {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << status << " " << statusMessage(status) << "\r\n";
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Content-Type: " << contentType << "\r\n";
    stream << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
    for (const auto& [name, value] : extraHeaders) {
        stream << name << ": " << value << "\r\n";
    }
    stream << "\r\n";
    if (!omitBody) {
        stream << body;
    }
    return stream.str();
}

void RequestParser::reset() {
    state = State::RequestLine;
    req = HttpRequest{};
    contentLength = 0;
    headerBytes = 0;
    error = 400;
}

bool RequestParser::parseRequestLine(const std::string& line) {
    const size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) {
        error = 400;
        return false;
    }
    const size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        error = 400;
        return false;
    }

    req.method = line.substr(0, firstSpace);
    req.target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    req.version = line.substr(secondSpace + 1);

    if (req.method.empty() || req.target.empty() || req.target[0] != '/') {
        error = 400;
        return false;
    }
    if (req.version != "HTTP/1.1" && req.version != "HTTP/1.0") {
        error = 505;
        return false;
    }

    const size_t queryStart = req.target.find('?');
    if (queryStart == std::string::npos) {
        req.path = req.target;
    }
    else {
        req.path = req.target.substr(0, queryStart);
        req.query = req.target.substr(queryStart + 1);
    }

    // HTTP/1.1 defaults to persistent connections; HTTP/1.0 does not.
    req.keepAlive = (req.version == "HTTP/1.1");
    return true;
}

bool RequestParser::finishHeaders() {
    if (!req.header("transfer-encoding").empty()) {
        error = 501;  // chunked bodies are not supported
        return false;
    }

    const std::string lengthHeader = req.header("content-length");
    if (!lengthHeader.empty()) {
        char* end = nullptr;
        const long long parsed = std::strtoll(lengthHeader.c_str(), &end, 10);
        if (end == lengthHeader.c_str() || *end != '\0' || parsed < 0) {
            error = 400;
            return false;
        }
        if (static_cast<size_t>(parsed) > maxBody) {
            error = 413;
            return false;
        }
        contentLength = static_cast<size_t>(parsed);
    }

    const std::string connectionHeader = toLower(req.header("connection"));
    if (connectionHeader.find("close") != std::string::npos) {
        req.keepAlive = false;
    }
    else if (connectionHeader.find("keep-alive") != std::string::npos) {
        req.keepAlive = true;
    }
    return true;
}

RequestParser::Result RequestParser::parse(std::string& buffer) {
    while (true) {
        switch (state) {
            case State::RequestLine: {
                const size_t lineEnd = buffer.find("\r\n");
                if (lineEnd == std::string::npos) {
                    if (buffer.size() > maxRequestLine) {
                        error = 414;
                        return Result::Error;
                    }
                    return Result::NeedMore;
                }
                if (lineEnd > maxRequestLine) {
                    error = 414;
                    return Result::Error;
                }
                const std::string line = buffer.substr(0, lineEnd);
                buffer.erase(0, lineEnd + 2);
                if (!parseRequestLine(line)) {
                    return Result::Error;
                }
                state = State::Headers;
                break;
            }

            case State::Headers: {
                const size_t lineEnd = buffer.find("\r\n");
                if (lineEnd == std::string::npos) {
                    if (buffer.size() + headerBytes > maxHeaderBlock) {
                        error = 431;
                        return Result::Error;
                    }
                    return Result::NeedMore;
                }

                const std::string line = buffer.substr(0, lineEnd);
                buffer.erase(0, lineEnd + 2);
                headerBytes += lineEnd + 2;
                if (headerBytes > maxHeaderBlock) {
                    error = 431;
                    return Result::Error;
                }

                if (line.empty()) {
                    if (!finishHeaders()) {
                        return Result::Error;
                    }
                    state = (contentLength > 0) ? State::Body : State::Done;
                    break;
                }

                const size_t colon = line.find(':');
                if (colon == std::string::npos || colon == 0) {
                    error = 400;
                    return Result::Error;
                }
                const std::string name = toLower(trim(line.substr(0, colon)));
                const std::string value = trim(line.substr(colon + 1));
                auto [iterator, inserted] = req.headers.emplace(name, value);
                if (!inserted) {
                    iterator->second += ", " + value;  // repeated field, RFC 9110 §5.2
                }
                break;
            }

            case State::Body: {
                if (buffer.size() < contentLength) {
                    return Result::NeedMore;
                }
                req.body = buffer.substr(0, contentLength);
                buffer.erase(0, contentLength);
                state = State::Done;
                break;
            }

            case State::Done:
                return Result::Complete;
        }
    }
}
