//**************************************************
// drogon handler for "GET /catalogue" requests 
//**************************************************
#include <syslog.h>
#include <stdexcept>

#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "SessionManager.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerGetCatalogueHandler(void) {
    drogon::app().registerHandler("/catalogue",
        [](const HttpRequestPtr& req, 
           std::function<void (const HttpResponsePtr &)> &&cb) {

            auto sendOk = [&](const Json::Value& rows) {
                Json::Value j; 
                j["ok"] = true; 
                j["count"] = static_cast<Json::Int64>(rows.size());
                j["rows"] = rows;
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k200OK);
                cb(r);
            };

            auto sendErr = [&](drogon::HttpStatusCode sc, const char* errMsg, const char*reason="") {
                Json::Value j;
                j["ok"] = false;
                j["error"] = errMsg;
                if (reason && *reason)
                    j["reason"] = reason;
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(sc);
                cb(r);
            };

            // check whether token is valid
            const std::string username = SessionManager::instance().usernameIfValid(req);  // empty if invalid/expired
            if (username.empty()) 
                return sendErr(drogon::k401Unauthorized, "unauthorised");

            try {    
                Database& db = Database::get();
                Json::Value rows(Json::arrayValue);

                int rowCount = db.listAllBooks(rows);              // all books [0..n]
                int rowsSize = rows.size();
                if (rowsSize != rowCount) {
                    // log the error, but proceed anyway
                    syslog(SYSLOG_ERR, "row.size [%d] different from rowCount [%d]", rowsSize, rowCount);
                }

                return sendOk(rows);
            } catch (const std::exception& ex) {
                return sendErr(drogon::k500InternalServerError,"server_error", ex.what());                
            } catch (...) {
                return sendErr(drogon::k500InternalServerError, "server error");
            }
        },
        {drogon::Get}  // limit to GET
    );

    return 0;
}