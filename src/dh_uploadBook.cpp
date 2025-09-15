//**************************************************
// drogon handler for "GET /book/{fileId}" requests 
//**************************************************
#include <filesystem>
#include <fstream>
#include <unistd.h>    // for close()

#include <drogon/drogon.h>
#include <sodium.h>

#include "Database.h"
#include "Config.h"
#include "utils.h"
#include "dhutils.h"
#include "dh_login.h"

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
namespace fs = std::filesystem;

static std::string toLower(std::string s){ for(char& c:s) c=std::tolower((unsigned char)c); return s; }

static std::string sha256_file_hex(const std::string& path) {
    crypto_hash_sha256_state st; 
    crypto_hash_sha256_init(&st);
    std::ifstream in(path, std::ios::binary); 
    if (!in) 
        throw std::runtime_error("hash open");

    std::vector<char> buf(1<<20);
    while (in) { 
        in.read(buf.data(), buf.size()); 
        std::streamsize n=in.gcount();
        if (n>0) 
            crypto_hash_sha256_update(&st,(const unsigned char*)buf.data(),(size_t)n);
    }
    unsigned char out[crypto_hash_sha256_BYTES]; 
    crypto_hash_sha256_final(&st,out);
    char hex[crypto_hash_sha256_BYTES*2+1]; 
    sodium_bin2hex(hex,sizeof hex,out,sizeof out);
    return std::string(hex);
}

int registerUploadBookHandler(void) {
    drogon::app().registerHandler("/uploadBook",
        [](const HttpRequestPtr& req, 
           std::function<void (const HttpResponsePtr &)> &&cb) {
            auto ok = [&](const std::string& fileId, long long size, const std::string& sha){
                Json::Value j; j["ok"]=true; 
                j["fileId"]=fileId;
                j["size"]=Json::Int64(size); 
                j["sha256"]=sha;
                auto r=drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(drogon::k200OK); cb(r);
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
            auto httpErr = [&](drogon::HttpStatusCode sc, const char* code){
                Json::Value j; j["ok"]=false; j["error"]=code;
                auto r=drogon::HttpResponse::newHttpJsonResponse(j);
                r->setStatusCode(sc);
                cb(r);
            };

            // check whether token is valid
            const std::string token    = bearerToken(req);
            const std::string username = usernameIfValid(token);  // empty if invalid/expired
            if (username.empty()) 
                return httpErr(drogon::k401Unauthorized, "unauthorised");

            // parse multipart
            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0) 
                return err("invalid_request","failed to parse");

            std::string fileId, shaHex;
            long long sizeClaim = -1;

            for (const auto& p : parser.getParameters()) {
                const auto& name = p.first; 
                const auto& val = p.second;
                if (name == "fileId") 
                    fileId = val;
                else if (name == "sha256") 
                    shaHex = toLower(val);
                else if (name == "size") {
                    try { 
                        sizeClaim = std::stoll(val); 
                    } catch (...) {
                         return err("invalid_request","bad filesize"); 
                    }
                }
            }
            if (!isHex64(shaHex) || sizeClaim <= 0) 
                return err("invalid_request","bad checksum");

            const bool unknownId = fileId.empty() || fileId == "0";

            // policy size check (before writing to disk)
            const long long maxSize = Config::get().maxFileSize();
            if ( (maxSize > 0) && (sizeClaim > maxSize) )
                return err("too_large");

            // If client doesn't know fileId: deduplicate by content first
            Database& db = Database::get();

            auto actualSha = shaHex;
            auto actualSize = sizeClaim;
            if (unknownId) {
                if (auto existing = db.lookupFileIdByHashSize(actualSha, actualSize); !existing.empty()) {
                    return ok(existing, actualSize, actualSha);
                }
            }

            // have file part?
            const auto& files = parser.getFiles();
            if (files.empty()) 
                return err("invalid_request","getFiles() failed to parse");

            // save upload to temp
            fs::path tmpPath;
            try {
                char templ[] = "/tmp/simplereader_upload_XXXXXX";
                int fd = mkstemp(templ); 
                if (fd == -1) 
                    return err("server_error");
                close(fd);
                tmpPath = templ;
                files.front().saveAs(tmpPath.string());
            } catch (...) { 
                return err("server_error"); 
            }

            auto cleanupTmp = [&](void){
                std::error_code ec; fs::remove(tmpPath, ec);
            };

            try {
                // verify size + sha
                std::error_code ec;
                auto actualSize = (long long)fs::file_size(tmpPath, ec);
                if (ec) { 
                    cleanupTmp(); 
                    return err("server_error"); 
                }
                if (actualSize != sizeClaim) { 
                    cleanupTmp(); 
                    return err("server_error","filesize mismatch"); 
                } // size mismatch

                const std::string actualSha = sha256_file_hex(tmpPath.string());
                if (actualSha != shaHex) { 
                    cleanupTmp(); 
                    return err("checksum_mismatch"); 
                }

                // New asset: choose fileId (uuid if unknown)
                std::string newId = unknownId ? drogon::utils::getUuid() : fileId;

                // move into library
                const fs::path libraryRoot = "/var/lib/simplereader/library";
                fs::create_directories(libraryRoot, ec); 
                if (ec) { 
                    cleanupTmp(); 
                    return err("server_error"); 
                }

                const fs::path dstPath = libraryRoot / newId;
                fs::rename(tmpPath, dstPath, ec);
                if (ec) { // cross-device fallback
                    fs::copy_file(tmpPath, dstPath, fs::copy_options::overwrite_existing, ec);
                    if (ec) { cleanupTmp(); 
                        return err("server_error"); 
                    }
                    fs::remove(tmpPath, ec);
                }

                // insert row
                const long long tnow = nowMs();
                try {
                    db.insertBookRecord(newId, actualSha, actualSize, dstPath.string(), tnow);
                } catch (...) {
                    // race: someone inserted first; look up by content and return that id
                    if (auto existing = db.lookupFileIdByHashSize(actualSha, actualSize); !existing.empty())
                        return ok(existing, actualSize, actualSha);
                    return err("server_error");
                }

                return ok(newId, actualSize, actualSha);
            } catch (...) {
                cleanupTmp();
                return err("server_error");
            }
        },
        {drogon::Post}  // limit to POST
    );

    return 0;
}