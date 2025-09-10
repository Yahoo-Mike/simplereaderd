//****************************************
// drogon handler for "POST /" requests 
//****************************************

#include <drogon/drogon.h>

int registerRootHandler(void) {

    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr &,
            std::function<void (const drogon::HttpResponsePtr &)> &&cb) {
            auto r = drogon::HttpResponse::newHttpResponse();
            r->setBody("simplereaderd up");
            cb(r);
        });

    return 0;
}