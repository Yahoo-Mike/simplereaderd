#include <fstream>
#include <iostream>
#include <syslog.h>
#include "utils.h"

// loads simplereader.conf file into a map <key, value>
std::unordered_map<std::string, std::string> loadConfig(const std::string &path) {
    std::unordered_map<std::string, std::string> cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open config file: " + path);
    }

    std::string line;
    while (std::getline(f, line)) {
        // strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        // trim spaces
        auto start = line.find_first_not_of(" \t");
        auto end = line.find_last_not_of(" \t");
        if (start == std::string::npos) continue; // blank line
        line = line.substr(start, end - start + 1);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // trim spaces around key/val
        auto trim = [](std::string &s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);

        if (!key.empty()) cfg[key] = val;
    }
    return cfg;
}

// for logging fatal exceptions to syslog
[[noreturn]] void logFatal(const std::exception &ex, int exitCode) {
    std::string msg = std::string("Fatal: ") + ex.what();
    std::cerr << msg << std::endl;
    syslog(LOG_ERR, "%s", msg.c_str());
    closelog();   // flush
    exit(exitCode);
}