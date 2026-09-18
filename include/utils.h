#pragma once
#include <string>
#include <unordered_map>

// Parse a KEY=VALUE file. Blank lines and lines starting with '#' are ignored.
std::unordered_map<std::string, std::string> parseEnvFile(const std::string& filePath);

int envStrToInt(const std::string& str);

// Absolute path of the directory holding the running executable (e.g. .../build).
std::string exeDir();

// Project root: the parent of exeDir(). Static files, logs and .env live here.
std::string projectRoot();
