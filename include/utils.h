#ifndef SIMPLEREADER_UTILS_H
#define SIMPLEREADER_UTILS_H

#include <unordered_map>
#include <string>

std::unordered_map<std::string, std::string> loadConfig(const std::string &path);
[[noreturn]] void logFatal(const std::exception &ex, int exitCode);

#endif // SIMPLEREADER_UTILS_H