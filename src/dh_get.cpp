//*******************************************
// drogon handler for "POST /get" requests 
//*******************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"
#include "SessionManager.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerGetHandler(void) {
    drogon::app().registerHandler("/get",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok_rows = [&](const Json::Value& rows) {
                Json::Value j; j["ok"] = true; j["rows"] = rows;  // [0..n]
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
            const std::string username = SessionManager::instance().usernameIfValid(req);
            if (username.empty()) return err("unauthorised");

            // parse and validate json
            auto bodyPtr = req->getJsonObject();      // Drogon parses for us
            if (!bodyPtr)
                return err("invalid_request","parsing failed");
            const auto& body = *bodyPtr;

            if (!body.isMember("table") || !body["table"].isString())
                return err("invalid_request","no table");
            if (!body.isMember("fileId") || !body["fileId"].isString())
                return err("invalid_request", "no fileId");

            bool hasId = body.isMember("id");
            if (hasId && !body["id"].isInt())
                return err("invalid_request", "invalid id");

            const std::string table  = body["table"].asString();
            const std::string fileId = body["fileId"].asString();
            const int id = hasId ? body["id"].asInt() : -1;   // -1 signals listUser*() to return all rows, not just one

            try {
                Database& db = Database::get();
                Json::Value rows(Json::arrayValue);

                if (table == "books" || table == "book_data") {
                    db.listUserBook(username, fileId, rows);              // [0..1]
                } else if (table == "bookmark") {
                    db.listUserBookmarks(username, fileId, id, rows);         // [0..n]
                } else if (table == "highlight") {
                    db.listUserHighlights(username, fileId, id, rows);        // [0..n]
                } else if (table == "note") {
                    db.listUserNotes(username, fileId, id, rows);        // [0..n]
                } else {
                    return err("invalid_request","unknown table");
                }

                return ok_rows(rows);
            } catch (...) {
                return err("server_error");
            }        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}