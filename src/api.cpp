#include "api.h"
#include <cctype>
#include <sstream>

std::string jsonEscape(const std::string& text) {
    std::ostringstream out;
    for (char character : text) {
        switch (character) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(character) < 0x20) {
                    out << "\\u00" << std::hex << (static_cast<int>(character) & 0xff);
                }
                else {
                    out << character;
                }
        }
    }
    return out.str();
}

std::string urlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        }
        else if (text[i] == '%' && i + 2 < text.size() &&
                 std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else {
            out += text[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parseFormEncoded(const std::string& text) {
    std::map<std::string, std::string> fields;
    std::istringstream stream(text);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        const size_t equals = pair.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        fields[urlDecode(pair.substr(0, equals))] = urlDecode(pair.substr(equals + 1));
    }
    return fields;
}

namespace {
void skipSpace(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
}

bool readString(const std::string& text, size_t& i, std::string& out) {
    if (i >= text.size() || text[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            switch (text[i]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                default:  out += text[i];
            }
        }
        else {
            out += text[i];
        }
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    ++i;  // closing quote
    return true;
}
}  // namespace

std::map<std::string, std::string> parseFlatJson(const std::string& text, bool& ok) {
    std::map<std::string, std::string> fields;
    ok = false;
    size_t i = 0;
    skipSpace(text, i);
    if (i >= text.size() || text[i] != '{') {
        return fields;
    }
    ++i;
    skipSpace(text, i);
    if (i < text.size() && text[i] == '}') {
        ok = true;
        return fields;
    }

    while (i < text.size()) {
        skipSpace(text, i);
        std::string key;
        if (!readString(text, i, key)) {
            return {};
        }
        skipSpace(text, i);
        if (i >= text.size() || text[i] != ':') {
            return {};
        }
        ++i;
        skipSpace(text, i);
        std::string value;
        if (!readString(text, i, value)) {
            return {};
        }
        fields[key] = value;

        skipSpace(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ok = true;
            return fields;
        }
        return {};
    }
    return {};
}

HttpResponse KvStore::list() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    std::ostringstream json;
    json << "{\"count\":" << entries.size() << ",\"entries\":[";
    bool first = true;
    for (const auto& [key, value] : entries) {
        if (!first) {
            json << ",";
        }
        first = false;
        json << "{\"key\":\"" << jsonEscape(key) << "\",\"value\":\"" << jsonEscape(value) << "\"}";
    }
    json << "]}";
    return HttpResponse::json(json.str());
}

HttpResponse KvStore::get(const std::string& key) const {
    if (key.empty()) {
        return HttpResponse::json(R"({"error":"missing key parameter"})", 400);
    }
    std::lock_guard<std::mutex> lock(storeMutex);
    auto iterator = entries.find(key);
    if (iterator == entries.end()) {
        return HttpResponse::json(R"({"error":"not found"})", 404);
    }
    return HttpResponse::json("{\"key\":\"" + jsonEscape(key) + "\",\"value\":\"" + jsonEscape(iterator->second) + "\"}");
}

HttpResponse KvStore::put(const HttpRequest& request) {
    std::map<std::string, std::string> fields;
    const std::string contentType = request.header("content-type");

    if (contentType.find("application/json") != std::string::npos) {
        bool ok = false;
        fields = parseFlatJson(request.body, ok);
        if (!ok) {
            return HttpResponse::json(R"({"error":"malformed JSON body"})", 400);
        }
    }
    else {
        fields = parseFormEncoded(request.body);
    }

    const std::string key = fields.count("key") ? fields["key"] : "";
    const std::string value = fields.count("value") ? fields["value"] : "";

    if (key.empty()) {
        return HttpResponse::json(R"({"error":"key is required"})", 400);
    }
    if (key.size() > maxKeyLength || value.size() > maxValueLength) {
        return HttpResponse::json(R"({"error":"key or value too long"})", 413);
    }

    std::lock_guard<std::mutex> lock(storeMutex);
    const bool replacing = entries.count(key) > 0;
    if (!replacing && entries.size() >= maxEntries) {
        return HttpResponse::json(R"({"error":"store is full"})", 507);
    }
    entries[key] = value;

    return HttpResponse::json("{\"key\":\"" + jsonEscape(key) + "\",\"value\":\"" + jsonEscape(value) + "\"}",
                              replacing ? 200 : 201);
}

HttpResponse KvStore::remove(const std::string& key) {
    if (key.empty()) {
        return HttpResponse::json(R"({"error":"missing key parameter"})", 400);
    }
    std::lock_guard<std::mutex> lock(storeMutex);
    if (entries.erase(key) == 0) {
        return HttpResponse::json(R"({"error":"not found"})", 404);
    }
    return HttpResponse::json(R"({"deleted":true})");
}
