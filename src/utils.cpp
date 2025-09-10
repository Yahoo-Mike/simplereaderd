// because Drogon redefines these, let's capture them as constants
// make sure this is done in utils.cpp before #including anything Drogon
#include "utils.h"  // this must be here to bring in externs (to keep C++ linker happy)
#include <syslog.h>
const int SYSLOG_INFO  = LOG_INFO;
const int SYSLOG_ERR   = LOG_ERR;
const int SYSLOG_DEBUG = LOG_DEBUG;
// now you can safely include Drogon stuff if you want to...

#include <fstream>
#include <iostream>
#include <chrono>


// for logging fatal exceptions to syslog
[[noreturn]] void logFatal(const std::exception &ex, int exitCode) {
  std::string msg = std::string("Fatal: ") + ex.what();
  std::cerr << msg << std::endl;
  syslog(SYSLOG_ERR, "%s", msg.c_str());
  closelog();   // flush
  exit(exitCode);
}

// return UTC time at this moment in milliseconds
long long nowMs(void) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// test for a 64-bit hex number
bool isHex64(std::string& s) {
    if (s.size() != 64) return false;
    for (auto& c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); // normalize lowercase
    }
    return true;
}