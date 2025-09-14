//*******************************************
// drogon handler for "POST /login" requests 
//*******************************************
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <syslog.h>

#include <drogon/drogon.h>
#include <jsoncpp/json/value.h>
#include <sodium.h>

#include "Config.h"
#include "Database.h"
#include "utils.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::HttpStatusCode;

using Clock = std::chrono::system_clock;

// we store an in-memory map of all the valid session tokens
// note: we don't delete expired tokens, they just stay in memory until daemon dies/restarts
struct Session {
    std::string username;
    std::string device;
    std::chrono::time_point<Clock> expires;
};

static std::unordered_map<std::string, Session> g_sessions;
static std::mutex g_sessionsMutex;

// the magic lives here
static std::string makeToken(size_t bytes = 32) {
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

// check username/password
static bool verifyPassword(const std::string& username, const std::string& password) {
    const char* sql = "SELECT pwd_hash FROM users WHERE username=?;";
    sqlite3_stmt* st = nullptr;
    sqlite3* db = Database::get().handle();

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::string stored;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        const unsigned char* p = sqlite3_column_text(st, 0);
        if (p) stored.assign(reinterpret_cast<const char*>(p));
    }
    sqlite3_finalize(st);

    if (stored.empty()) return false; // user not found

    return crypto_pwhash_str_verify(stored.c_str(), password.c_str(), password.size()) == 0;
}

static void jsonError(const HttpRequestPtr&,
                      std::function<void (const HttpResponsePtr &)> &&cb,
                      HttpStatusCode code,
                      const std::string& msg) {
    Json::Value j(Json::objectValue);
    j["ok"] = false;
    j["error"] = msg;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
    resp->setStatusCode(code);
    cb(resp);
}

// check if this "token" is valid.  If so return username.  If not return empty.
std::string usernameIfValid(std::string token) {
    if (token.empty()) return "";

    std::lock_guard<std::mutex> lock(g_sessionsMutex);
    const auto it = g_sessions.find(token);
    if (it == g_sessions.end()) 
        return "";

    if (it->second.expires <= Clock::now())
        // no lazy cleanup by design; just treat as invalid (will be cleared when daemon stops/restarts)
        return "";

    return it->second.username;
}

// check if this "token" is valid
bool isValid(const std::string& token) {
  return !usernameIfValid(token).empty();
}

int registerLoginHandler(void) {
    int tokenSecs = 3600;     // 1 hour tokens
    std::string compat = Config::get().compat();

    drogon::app().registerHandler(
        "/login",
        [compat, tokenSecs](const HttpRequestPtr &req,
                            std::function<void (const HttpResponsePtr &)> &&cb) {

            if (req->method() != drogon::Post) {
                return jsonError(req, std::move(cb), drogon::k405MethodNotAllowed, "method_not_allowed");
            }

            auto json = req->getJsonObject();
            if (!json) {
                return jsonError(req, std::move(cb), drogon::k400BadRequest, "invalid_json");
            }

            const auto username = (*json)["username"].asString();
            const auto password = (*json)["password"].asString();
            const auto version  = (*json)["version"].asString();
            const auto device  = (*json)["device"].asString();      // "device" tag is optional

            if (username.empty() || password.empty() || version.empty()) {
                return jsonError(req, std::move(cb), drogon::k400BadRequest, "missing_fields");
            }

            // Version gate
            if (version != compat) {
                Json::Value j;
                j["ok"] = false;
                j["error"] = "wrong_version";
                j["expected"] = compat;
                auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
                resp->setStatusCode(drogon::k401Unauthorized);
                return cb(resp);
            }

            // Auth check (stub)
            if (!verifyPassword(username, password)) {
                return jsonError(req, std::move(cb), drogon::k401Unauthorized, "invalid_credentials");
            }

            if (device.empty()) {
                syslog(SYSLOG_INFO, "user [%s] logged in on unidentified device", username.c_str());
            } else {
                syslog(SYSLOG_INFO, "user [%s] logged in on device [%s]", username.c_str(), device.c_str());
            }
            // Issue session token
            const auto token = makeToken(32);
            const auto expires = Clock::now() + std::chrono::seconds(tokenSecs);

            {
                std::lock_guard<std::mutex> lk(g_sessionsMutex);
                g_sessions[token] = Session{username, device, expires};
            }

            Json::Value j;
            j["ok"] = true;
            j["token"] = token;
            j["expiresAt"] = static_cast<Json::Int64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(expires.time_since_epoch()).count()
            );
            auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
            resp->setStatusCode(drogon::k200OK);
            cb(resp);
        },
        {drogon::Post} // limit to POST
    );

    return 0;
}