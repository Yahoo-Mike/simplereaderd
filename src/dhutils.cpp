#include <string>
#include <drogon/drogon.h>

std::string bearerToken(const drogon::HttpRequestPtr& req) {
  auto h = req->getHeader("authorization");
  if (h.size() >= 7 && strncasecmp(h.c_str(), "Bearer ", 7) == 0) 
    return h.substr(7);
  return "";
}

// Accept integer in JSON either as number or stringified digits
bool parseItemId(const Json::Value& v, long long& out) {
    if (v.isInt64()) { out = v.asInt64(); return true; }
    if (v.isString()) {
        const auto& s = v.asString();
        if (!s.empty() && std::all_of(s.begin(), s.end(), ::isdigit)) {
            out = std::stoll(s);
            return true;
        }
    }
    return false;
}