#ifndef SIMPLEREADER_UTILS_H
#define SIMPLEREADER_UTILS_H

#include <unordered_map>
#include <string>

// because Drogon redefines these, let's capture them as constants
extern const int SYSLOG_INFO, SYSLOG_ERR, SYSLOG_DEBUG;

// functions
[[noreturn]] void logFatal(const std::exception &ex, int exitCode);
long long nowMs(void);
bool isHex64(std::string& s);

#endif // SIMPLEREADER_UTILS_H