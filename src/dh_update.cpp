//*******************************************
// drogon handler for "POST /update" requests 
//*******************************************
#include <syslog.h>
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

// Accept bool or "true"/"false" (string)
static bool parseBoolFlexible(const Json::Value& v, bool& out) {
    if (v.isBool()) { out = v.asBool(); return true; }
    if (v.isString()) {
        auto s = v.asString();
        if (s == "true" || s == "1")  { out = true;  return true; }
        if (s == "false"|| s == "0")  { out = false; return true; }
    }
    return false;
}

// stringify JSON fields (progress/locator/selection) if not already a string
static std::string toJsonString(const Json::Value& v) {
    if (v.isString()) return v.asString();
    Json::StreamWriterBuilder wb; wb["indentation"] = "";
    return Json::writeString(wb, v);
}

int registerUpdateHandler(void) {
    drogon::app().registerHandler("/update",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok = [&](long long ts) {
                Json::Value j; j["ok"] = true; j["updatedAt"] = static_cast<Json::Int64>(ts);
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k200OK);
                cb(r);
            };
            auto conflict = [&](long long serverTs) {
                Json::Value j; j["ok"] = false; j["error"] = "conflict";
                j["serverUpdatedAt"] = static_cast<Json::Int64>(serverTs);
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

            if (!body.isMember("row")   || !body["row"].isObject())   
                return err("invalid_request","no row data");

            const std::string table = body["table"].asString();
            const Json::Value& row  = body["row"];

            bool force = false;
            if (body.isMember("force") && !parseBoolFlexible(body["force"], force))
                return err("invalid_request","invalid value for force tag");

            if (!row.isMember("updatedAt") || !row["updatedAt"].isInt64())
                return err("invalid_request","invalid updatedAt value");

            const long long clientTs = row["updatedAt"].asInt64();

            try {
                Database& db = Database::get();

                if (table == "books" || table == "book_data") {
                    if (!row.isMember("fileId") || !row["fileId"].isString())
                        return err("invalid_request","no fileId");
                    const std::string fileId = row["fileId"].asString();
                    if (!db.bookExists(fileId))
                        return err("invalid_request","unknown fileId");

                    const std::string progress = row.isMember("progress") ? toJsonString(row["progress"]) : "";

                    auto st = db.select_userBooks_byUserAndFileId(username, fileId);
                    const long long serverTs = st.deleted ? st.deletedAt : st.updatedAt;

                    if ( !force && (serverTs > 0) && (clientTs < serverTs) )
                        return conflict(serverTs);

                    const long long tnow = nowMs();
                    // NOTE: always clear tombstone on update, thereby resurrecting the whole record
                    db.insertUserBook(username, fileId, progress, true, tnow);
                    return ok(tnow);
                }

                if (table == "bookmark") {
                    if (!row.isMember("fileId") || !row["fileId"].isString()) 
                        return err("invalid_request","no fileId");
                    if (!row.isMember("id")) 
                        return err("invalid_request","no id");

                    const std::string fileId = row["fileId"].asString();
                    if (!db.bookExists(fileId))
                        return err("invalid_request","unknown fileId");

                    long long itemId = 0; 
                    if (!parseItemId(row["id"], itemId)) 
                        return err("invalid_request", "bad id");
                    const std::string locator = row.isMember("locator") ? toJsonString(row["locator"]) : "";
                    const std::string label   = row.isMember("label")   ? row["label"].asString()      : "";

                    auto st = db.select_byUserFileAndItemId("user_bookmarks", username, fileId, itemId);
                    const long long serverTs = st.deleted ? st.deletedAt : st.updatedAt;

                    if ( !force && (serverTs > 0) && (clientTs < serverTs) )
                        return conflict(serverTs);

                    const long long tnow = nowMs();
                    // NOTE: always clear tombstone on update, thereby resurrecting the whole record
                    db.insertUserBookmark(username, fileId, itemId, locator, label, true, tnow);
                    return ok(tnow);
                }

                if (table == "highlight") {
                    if (!row.isMember("fileId") || !row["fileId"].isString()) 
                        return err("invalid_request","no fileId");
                    if (!row.isMember("id")) 
                        return err("invalid_request","no id");

                    const std::string fileId = row["fileId"].asString();
                    if (!db.bookExists(fileId))
                        return err("invalid_request","unknown fileId");

                    long long itemId = 0; 
                    if (!parseItemId(row["id"], itemId)) 
                        return err("invalid_request","bad id");

                    const std::string sel    = row.isMember("selection") ? toJsonString(row["selection"]) : "";
                    const std::string label  = row.isMember("label")     ? row["label"].asString()        : "";
                    const std::string colour = row.isMember("colour")    ? row["colour"].asString()       : "";

                    auto st = db.select_byUserFileAndItemId("user_highlights", username, fileId, itemId);
                    const long long serverTs = st.deleted ? st.deletedAt : st.updatedAt;

                    if (!force && serverTs > 0 && clientTs < serverTs)
                        return conflict(serverTs);

                    const long long tnow = nowMs();
                    // NOTE: always clear tombstone on update, thereby resurrecting the whole record
                    db.insertUserHighlight(username, fileId, itemId, sel, label, colour, true, tnow);
                    return ok(tnow);
                }

                return err("invalid_request","unknown table");
            } catch (...) {
                return err("server_error");
            }        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}