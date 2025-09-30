
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "Config.h"
#include "version.h"

// loads simplereader.conf file into a map <key, value>
static std::unordered_map<std::string, std::string> loadConfig(const std::string &path) {
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

// singleton instance
Config& Config::get() {
    static Config instance;
    return instance;
}

// load the configuration from a file
void Config::load(const std::string& overridePath) {
    std::string path;
    
    if (!overridePath.empty()) {
        path = overridePath;
    } else if (const char* env = std::getenv("SIMPLEREADER_CONF")) {
        path = env;
    } else {
        path = "/etc/simplereader/simplereader.conf"; // default location
    }

    auto cfg = loadConfig(path);
    auto assignStr = [&](const char* key, std::string& out) {
        auto it = cfg.find(key);
        if ( it != cfg.end() && !it->second.empty())
            out = it->second; // only assign if present and non-empty
    };

    auto assignInt = [&](const char* key, int& out, auto validate) {
        auto it = cfg.find(key);
        if (it != cfg.end()) {
            try {
                size_t pos = 0;
                int v = std::stoi(it->second, &pos, 10);
                if (pos == it->second.size() && validate(v)) {
                    out = v; // only assign if fully parsed and in range
                }
            } catch (...) {
                // keep default
            }
        }
    };

    assignStr("host",           host_);
    assignStr("compat",         compat_);
    assignInt("port",           port_,          [](int v){ return v > 0 && v <= 65535; });
    assignInt("maxfilesize",    maxFileSizeMB_, [](int v){ return v > 0; });
    assignInt("tokentimeout",   tokenTimeout_,  [](int v){ return v > 0; });
}

std::string Config::toShortString() const {
    std::ostringstream oss;

    oss << "compat=" << compat_ << ", "
        << "maxFileSize=" << maxFileSizeMB_ << "MB, "
        << "tokenTimeout=" << tokenTimeout_ << "mins";

    return oss.str();
}

std::string Config::toString() const {
    std::ostringstream oss;

    oss << "v" << SIMPLEREADERD_VERSION << " on "
        << host_ << ":" << port_ << " ("
        << toShortString() << ")";

        return oss.str();
}