//*******************************************
// drogon handler for "POST /getSince" requests 
//*******************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerGetSinceHandler(void) {
    drogon::app().registerHandler("/getSince",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok_rows = [&](const Json::Value& rows, long long nextSince) {
                Json::Value j; j["ok"] = true; j["rows"] = rows;
                j["nextSince"] = static_cast<Json::Int64>(nextSince);
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k200OK);
                cb(r);
            };
            auto err = [&](const char* code,const char* info="") {
                Json::Value j;
                j["ok"]    = false;
                j["error"] = code;
                if (*info)
                    j["reason"] = info;
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k200OK); // app-level errors
                cb(r);
            };

            // check whether token is valid
            const std::string token    = bearerToken(req);
            const std::string username = usernameIfValid(token);  // empty if invalid/expired
            if (username.empty()) return err("unauthorised");

            // parse and validate json
            auto bodyPtr = req->getJsonObject();      // Drogon parses for us
            if (!bodyPtr)    return err("invalid_request","parsing failed");
            const auto& body = *bodyPtr;

            if (!body.isMember("table") || !body["table"].isString())
                return err("invalid_request", "no tablename");
            if (!body.isMember("since") || !body["since"].isInt64())
                return err("invalid_request","no \"since\" value");

            long long since = body["since"].asInt64();
            int limit = 100; // sane default
            if (body.isMember("limit")) {
                if (!body["limit"].isInt()) 
                    return err("invalid_request","invalid limit");
                limit = std::max(1, std::min(1000, body["limit"].asInt())); // floor..ceiling [1..1000]
            }

            const std::string table = body["table"].asString();

            try {
                Database& db = Database::get();
                Json::Value rows(Json::arrayValue);
                long long nextSinceOut = since;

                if (table == "books" || table == "book_data") {
                    db.listUserBooksSince(username, since, limit, rows, nextSinceOut);
                } else if (table == "bookmark") {
                    db.listUserBookmarksSince(username, since, limit, rows, nextSinceOut);
                } else if (table == "highlight") {
                    db.listUserHighlightsSince(username, since, limit, rows, nextSinceOut);
                } else {
                    return err("invalid_request","unknown tablename");
                }

                return ok_rows(rows, nextSinceOut);
            } catch (...) {
                return err("server_error");
            }        
        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}