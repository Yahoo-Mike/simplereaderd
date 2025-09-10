//*******************************************
// drogon handler for "POST /resolve" requests 
//*******************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerResolveHandler(void) {
    drogon::app().registerHandler("/resolve",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok = [&](bool exists, const std::string& fileId = "") {
                Json::Value j;
                j["ok"]     = true;
                j["exists"] = exists;
                if (exists) j["fileId"] = fileId;
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

            if (!body.isMember("sha256") || !body["sha256"].isString()) 
                return err("invalid_request","no or bad sha256");
            if (!body.isMember("filesize") || !body["filesize"].isInt64()) 
                return err("invalid_request", "no or bad filesize");

            std::string sha = body["sha256"].asString();
            if (!isHex64(sha)) return err("invalid_request","sha256 is not hex");

            const long long size = body["filesize"].asInt64();
            if (size <= 0) return err("invalid_request","invalid filesize");

            try {
                auto fileId = Database::get().lookupFileIdByHashSize(sha, size);
                if (fileId.empty()) 
                    return ok(false);       // file does not exist
                return ok(true, fileId);    // file exists
            } catch (...) {
                return err("server_error");
            }        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}