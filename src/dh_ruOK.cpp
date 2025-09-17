//**********************************************
// drogon handler for "GET /ruOK" requests 
//    use this as a heartbeat and token verifier
//**********************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerRUOKHandler(void) {
    drogon::app().registerHandler("/ruOK/{1}",
        [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb,const std::string& token) {
            Json::Value j;

            if (usernameIfValid(token).empty()) {
                j["ok"] = false;
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k401Unauthorized);
                return cb(r);
            }
            // else everything's hunky dory
            j["ok"] = true;
            auto r = drogon::HttpResponse::newHttpJsonResponse(j);
            r->setStatusCode(drogon::k200OK);
            return cb(r);
        },
        {drogon::Get}  // limit to GET
    );

    return 0;
}