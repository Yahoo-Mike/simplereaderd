//**************************************************
// drogon handler for "GET /book/{fileId}" requests 
//**************************************************
#include <drogon/drogon.h>

#include "Database.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"
#include "SessionManager.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

int registerGetBookHandler(void) {
    drogon::app().registerHandler("/book/{1}",
        [](const HttpRequestPtr& req, 
           std::function<void (const HttpResponsePtr &)> &&cb,
           const std::string& fileId) {

            auto jsonErr = [&](drogon::HttpStatusCode sc, const char* errMsg) {
                Json::Value j;
                j["ok"] = false;
                j["error"] = errMsg;
                auto r = drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(sc);
                cb(r);
            };
            // check whether token is valid
            const std::string username = SessionManager::instance().usernameIfValid(req);  // empty if invalid/expired
            if (username.empty()) 
                return jsonErr(drogon::k401Unauthorized, "unauthorised");

            // --- lookup book metadata ---
            std::string path, sha256, clientFileName;
            long long   size = 0;
            try {
                clientFileName = Database::get().getBookForDownload(fileId, path, size, sha256);
                if (clientFileName.empty()) {
                    return jsonErr(drogon::k404NotFound, "book record not found");
                }
            } catch (...) {
                return jsonErr(drogon::k500InternalServerError, "server_error");
            }

            // --- basic file checks before streaming ---
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(path, ec) || ec) {
                return jsonErr(drogon::k404NotFound, "file not found");
            }
            auto actualSize = fs::file_size(path, ec);
            if (ec)
                return jsonErr(drogon::k500InternalServerError, ec.message().c_str());
            if ( (size >= 0) && (static_cast<long long>(actualSize) != size) ) {
                // size mismatch: treat as server error (index corrupt)
                return jsonErr(drogon::k500InternalServerError, "size mismatch");
            }        

            // --- stream the file ---
            auto resp = drogon::HttpResponse::newFileResponse(
                path,
                clientFileName,
                drogon::CT_APPLICATION_OCTET_STREAM
            );
            resp->setStatusCode(drogon::k200OK);
            resp->addHeader("X-Checksum-SHA256", sha256);
            resp->addHeader("X-Filename", clientFileName);
            cb(resp);
        },
        {drogon::Get}  // limit to GET
    );

    return 0;
}