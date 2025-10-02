
#include <iomanip>
#include <random>

#include "SessionManager.h"
#include "Config.h"

// singleton instance
SessionManager& SessionManager::instance() {
    static SessionManager inst;     // instantiated once on first-call
    return inst;
}

// add a user/device
// RETURNS: token/expiry
SessionManager::SessionToken SessionManager::add(const std::string& username, 
                                                 const std::string& device) {

    const auto token = makeToken();
    const auto tokenLife = Config::get().tokenTimeout() * 60; // in secs
    const auto expires = Clock::now() + std::chrono::seconds(tokenLife);

    std::lock_guard<std::mutex> lk(mu_);
    pruneExpired(); // do some housekeeping first
    sessions_[token] = Session{username, device, expires};

    return SessionToken{token,expires};
}

// create a token
std::string SessionManager::makeToken(size_t bytes) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::ostringstream oss;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned int b = dist(gen) & 0xFFu;
        oss << std::hex << std::setfill('0') << std::setw(2) << b;
    }
    return oss.str();
}

// extract the token from a HttpRequest header
std::string SessionManager::bearerToken(const drogon::HttpRequestPtr& req) {
  auto h = req->getHeader("authorization");
  if (h.size() >= 7 && strncasecmp(h.c_str(), "Bearer ", 7) == 0) 
    return h.substr(7);
  return "";
}

// validate token inside HttpRequest header
// RETURN: username if token is valid, else empty string
std::string SessionManager::usernameIfValid(const drogon::HttpRequestPtr& req) {
    const std::string token = bearerToken(req);
    return usernameIfValid(token);
}
 
// validate token
// RETURN: username if token is valid, else empty string
std::string SessionManager::usernameIfValid(const std::string& token) {
    if (token.empty()) return {};   // no token
    
    const auto now = Clock::now();

    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return {};   // couldn't find it

    if (it->second.expires <= now) {
        // this single entry has expired already, so may as well delete it now
        sessions_.erase(it);
        return {};
    }

    return it->second.username;
}

// pruneExpired(): delete old tokens
// note: assumes you have already locked the mutex
void SessionManager::pruneExpired() {
    const auto now = Clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->second.expires <= now) it = sessions_.erase(it);
        else ++it;
    }
}
