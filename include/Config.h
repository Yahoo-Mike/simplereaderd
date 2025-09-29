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
    int maxFileSize() const   { return maxFileSizeMB_ * 1024 * 1024; } // returns in bytes
    int maxFileSizeMB() const { return maxFileSizeMB_; }               // returns in MB
    int tokenTimeout() const  { return tokenTimeout_; }                // returns in mins

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
    int maxFileSizeMB_ = 200;   // MB
    int tokenTimeout_ = 60;     // mins
};
