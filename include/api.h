#pragma once
#include "http.h"
#include "router.h"
#include <map>
#include <mutex>
#include <string>

// Small in-memory key/value store behind the JSON API. Bounded on purpose:
// this endpoint is public on the deployed instance, so entry count, key length
// and value length are all capped.
class KvStore {
public:
    static constexpr size_t maxEntries = 50;
    static constexpr size_t maxKeyLength = 64;
    static constexpr size_t maxValueLength = 256;

    HttpResponse list() const;
    HttpResponse get(const std::string& key) const;
    HttpResponse put(const HttpRequest& request);
    HttpResponse remove(const std::string& key);

private:
    mutable std::mutex storeMutex;
    std::map<std::string, std::string> entries;
};

// Percent-decoding and query/form parsing, shared by the API handlers.
std::string urlDecode(const std::string& text);
std::map<std::string, std::string> parseFormEncoded(const std::string& text);
// Flat {"key":"value"} objects only — enough for this API, and it refuses
// anything it does not fully understand rather than guessing.
std::map<std::string, std::string> parseFlatJson(const std::string& text, bool& ok);
std::string jsonEscape(const std::string& text);
