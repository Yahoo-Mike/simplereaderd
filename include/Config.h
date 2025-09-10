#pragma once
#include <string>
#include <unordered_map>

class Config {
public:
    // Singleton accessor
    static Config& get();

    // Load once from file
    void load(const std::string& overridePath = "");

    // Getters
    const std::string& host() const { return host_; }
    int port() const { return port_; }
    const std::string& compat() const { return compat_; }
    int maxFileSizeMB() const { return maxFileSizeMB_; }

private:
    // Private constructor
    Config() = default;

    // Disable copy
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // Values
    std::string host_ = "127.0.0.1";
    int port_ = 9000;
    std::string compat_ = "0.0.0";
    int maxFileSizeMB_ = 20;
};
