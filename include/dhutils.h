#ifndef SIMPLEREADER_DHUTILS_H
#define SIMPLEREADER_DHUTILS_H

#include <string>
#include <drogon/drogon.h>

bool parseItemId(const Json::Value& v, long long& out);

#endif // SIMPLEREADER_UTILS_H