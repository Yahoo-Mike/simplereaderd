//*******************************************
// drogon handler for "POST /check" requests 
//*******************************************
#include <drogon/drogon.h>

#include "Config.h"
#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerCheckHandler(void) {
    drogon::app().registerHandler("/check",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok = [&](bool exists, bool deleted, long long ts = 0) {
                Json::Value j;
                j["ok"]      = true;
                j["exists"]  = exists;
                j["deleted"] = deleted;
                if (ts > 0) j["updatedAt"] = static_cast<Json::Int64>(ts);
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
                return err("invalid_request","bad tablename");

            const std::string table = body["table"].asString();

            // localUpdatedAt is client-only; we accept but ignore. If present and not an integer, reject.
            if (body.isMember("localUpdatedAt") && !body["localUpdatedAt"].isInt64()) {
                const std::string reason = "bad localUpdatedAt [" + body["localUpdatedAt"].asString() + "]";
                return err("invalid_request",reason.c_str());
            }

            // check db & respond
            try {
                Database& db = Database::get();
                Database::RowState st;

                if (table == "books" || table == "book_data") {
                    const bool hasFileId = body.isMember("fileId") && body["fileId"].isString();
                    if (!hasFileId) 
                        return err("invalid_request","no fileId");

                    st = db.select_userBooks_byUserAndFileId(username, body["fileId"].asString());
                }
                else if (table == "bookmark" || table == "highlight") {
                    // Require fileId (string) and id (integer)
                    // "id" is a sequential unique index
                    if (!body.isMember("fileId") || !body["fileId"].isString()) 
                        return err("invalid_request","no fileId");
                    if (!body.isMember("id")) 
                        return err("invalid_request","no id");

                    long long itemId = 0;
                    if (!parseItemId(body["id"], itemId)) 
                        return err("invalid_request","bad id");

                    const std::string fileId = body["fileId"].asString();
                    const std::string tablename =
                        (table.find("highlight") != std::string::npos) ? "user_highlights" : "user_bookmarks";

                    st = db.select_byUserFileAndItemId(tablename, username, fileId, itemId);
                }
                else {
                    return err("invalid_request","table unknown"); // unknown table
                }

                if (!st.exists && !st.deleted) 
                    return ok(false, false);                // never seen
                if (st.deleted)
                    return ok(false, true,  st.deletedAt);  // deleted (tombstone) with timestamp
                return ok(true,  false, st.updatedAt);      // exists with timestamp
            } catch (...) {
                return err("server_error");
            }
        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}