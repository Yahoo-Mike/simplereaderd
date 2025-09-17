//*******************************************
// drogon handler for "POST /delete" requests 
//*******************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;


int registerDeleteHandler(void) {
    drogon::app().registerHandler("/delete",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok = [&](long long deletedAt) {
                Json::Value j; j["ok"] = true; 
                j["deletedAt"] = static_cast<Json::Int64>(deletedAt);
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
                return err("invalid_request","no tablename");
            if (!body.isMember("fileId") || !body["fileId"].isString()) 
                return err("invalid_request","no fileId");

            const std::string table  = body["table"].asString();
            const std::string fileId = body["fileId"].asString();

            try {
                Database& db = Database::get();

                if (table == "books" || table == "book_data") {
                    // look up current state
                    auto st = db.select_userBooks_byUserAndFileId(username, fileId);
                    if (!st.exists && !st.deleted) 
                        return err("not_found");

                    if (st.deleted) 
                        return ok(st.deletedAt); // already tombstoned

                    const long long tnow = nowMs();
                    db.softDeleteUserBook(username, fileId, tnow);

                    // also soft delete bookmarks and highlights for this fileId
                    db.softDeleteUserBookmarkAll(username,fileId,tnow);
                    db.softDeleteUserHighlightAll(username,fileId,tnow);
                    return ok(tnow);
                }

                if (table == "bookmark") {
                    if (!body.isMember("id")) 
                        return err("invalid_request","no id");
                    long long itemId = 0; 
                    if (!parseItemId(body["id"], itemId)) 
                        return err("invalid_request","bad id");

                    auto st = db.select_byUserFileAndItemId("user_bookmarks", username, fileId, itemId);
                    if (!st.exists && !st.deleted) 
                        return err("not_found");

                    if (st.deleted) return ok(st.deletedAt);

                    const long long tnow = nowMs();
                    db.softDeleteUserBookmark(username, fileId, itemId, tnow);
                    return ok(tnow);
                }

                if (table == "highlight") {
                    if (!body.isMember("id")) 
                        return err("invalid_request","no id");
                    long long itemId = 0; if (!parseItemId(body["id"], itemId)) 
                        return err("invalid_request","bad id");

                    auto st = db.select_byUserFileAndItemId("user_highlights", username, fileId, itemId);
                    if (!st.exists && !st.deleted) 
                        return err("not_found");

                    if (st.deleted)
                        return ok(st.deletedAt);

                    const long long tnow = nowMs();
                    db.softDeleteUserHighlight(username, fileId, itemId, tnow);
                    return ok(tnow);
                }

                return err("invalid_request", "unknown table");
            } catch (...) {
                return err("server_error");
            }        
        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}