//****************************************
// drogon handler for "GET /" requests
//****************************************

#include <drogon/drogon.h>

int registerRootHandler(void) {

    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr &,
            std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
            Json::Value j;
            j["ok"]  = true;            // use boolean true
            j["status"] = "server up";
            auto r = drogon::HttpResponse::newHttpJsonResponse(j);
            r->setStatusCode(drogon::k200OK);
            cb(r);
        },
        {drogon::Get}   // limit to GET
    );

    return 0;
}