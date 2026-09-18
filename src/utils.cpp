#include "utils.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>

std::unordered_map<std::string, std::string> parseEnvFile(const std::string& filePath) {
    std::unordered_map<std::string, std::string> envVariables;
    std::ifstream file(filePath);
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream(line);
        std::string key, value;
        if (std::getline(stream, key, '=') && std::getline(stream, value)) {
            envVariables[key] = value;
        }
    }
    return envVariables;
}

int envStrToInt(const std::string& str) {
    try {
        return std::stoi(str);
    }
    catch (const std::exception&) {
        throw std::runtime_error("Cannot convert \"" + str + "\" to an integer");
    }
}

std::string exeDir() {
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return ".";
    }
    buffer[length] = '\0';
    std::string path(buffer);
    size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

std::string projectRoot() {
    std::string dir = exeDir();
    size_t slash = dir.find_last_of('/');
    return (slash == std::string::npos) ? dir : dir.substr(0, slash);
}
