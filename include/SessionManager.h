#ifndef SIMPLEREADER_SESSIONMANAGER_H
#define SIMPLEREADER_SESSIONMANAGER_H

#include <unordered_map>
#include <mutex>
#include <string>
#include <chrono>

#include <drogon/drogon.h>

//
// SessionManager:  handles authorisation tokens
//
class SessionManager {
public:
    using Clock = std::chrono::system_clock;
    struct SessionToken {
        const std::string token;
        const std::chrono::time_point<Clock> expiry;
    };

    // robust singleton - no copying or assignment
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    static SessionManager& instance();  // singleton instance

    // add a session for this user and return token/expiry
    SessionToken add(const std::string& username,
                     const std::string& device);

    // returns username if token valid (non-empty), else empty string.
    std::string usernameIfValid(const drogon::HttpRequestPtr& req);
    std::string usernameIfValid(const std::string& token);
    
    bool isValid(const std::string& token) { return !usernameIfValid(token).empty(); }

private:
    struct Session {
        std::string username;
        std::string device;
        std::chrono::time_point<Clock> expires;
    };

    SessionManager() = default; // singleton

    std::string bearerToken(const drogon::HttpRequestPtr& req);
    std::string makeToken(size_t bytes = 32);   // create a token
    void pruneExpired();                        // deletes old tokens

    std::unordered_map<std::string, Session> sessions_;
    std::mutex mu_;
};

#endif // SIMPLEREADER_SESSIONMANAGER_H