//*******************************************
// drogon handler for "POST /login" requests 
//*******************************************
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <syslog.h>

#include <drogon/drogon.h>
#include <jsoncpp/json/value.h>
#include <sodium.h>

#include "Config.h"
#include "Database.h"
#include "utils.h"
#include "SessionManager.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::HttpStatusCode;

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

int registerLoginHandler(void) {

    drogon::app().registerHandler(
        "/login",
        [](const HttpRequestPtr &req,
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
            const auto device  = json->get("device","unidentified").asString();      // "device" tag is optional

            if (username.empty() || password.empty() || version.empty()) {
                return jsonError(req, std::move(cb), drogon::k400BadRequest, "missing_fields");
            }

            // check that we support this client's version#
            const auto compat = Config::get().compat();
            if (version != compat) {
                syslog(SYSLOG_ERR, "invalid version [%s]: login rejected for user [%s] on device [%s], not the supported version [%s]",
                        version.c_str(), username.c_str(), device.c_str(), compat.c_str());

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
                syslog(SYSLOG_ERR, "invalid username/password for user [%s] on device [%s]", username.c_str(),device.c_str());
                return jsonError(req, std::move(cb), drogon::k401Unauthorized, "invalid_credentials");
            }

            syslog(SYSLOG_INFO, "user [%s] logged in on device [%s]", username.c_str(), device.c_str());

            // Issue session token
            const auto session = SessionManager::instance().add(username,device);

            Json::Value j;
            j["ok"] = true;
            j["token"] = session.token;
            j["expiresAt"] = static_cast<Json::Int64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(session.expiry.time_since_epoch()).count()
            );
            auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
            resp->setStatusCode(drogon::k200OK);
            cb(resp);
        },
        {drogon::Post} // limit to POST
    );

    return 0;
}