#ifndef SIMPLEREADER_LOGIN_H
#define SIMPLEREADER_LOGIN_H

#include <string>
#include <drogon/drogon.h>

int registerLoginHandler(void);
std::string usernameIfValid(std::string token);

std::string bearerToken(const drogon::HttpRequestPtr& req);

#endif