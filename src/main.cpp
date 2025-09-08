#include <drogon/drogon.h>
#include <iostream>
#include <syslog.h>
#include <signal.h>
#include <stdlib.h>

#include "utils.h"


void handleSignal(int sig) {
    syslog(LOG_ERR, "simplereaderd terminated by signal %d", sig);
    closelog();
    exit(1);
}

int main() {

    // open syslog
    openlog("simplereaderd", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "simplereaderd starting up");

    
    // catch signals
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGSEGV, handleSignal); // crash (segfault)

    try {
        /////////////////////////////////////////////////////////////
        // config path (default or from env)
        //
        std::string confPath = "/etc/simplereader/simplereader.conf";
        if (const char* env = std::getenv("SIMPLEREADER_CONF")) {
            confPath = env;
        }

        auto cfg = loadConfig(confPath);

        std::string host   = cfg["host"];
        int port           = std::stoi(cfg["port"]);
        std::string compat = cfg["compat"];
        int maxFileSizeMB  = std::stoi(cfg["maxfilesize"]);

        auto msg = "Loaded config: host=" + host +
                   " port="   + std::to_string(port) + 
                   " compat=" + compat +
                   " maxFileSize=" + std::to_string(maxFileSizeMB) + "MB";

        std::cout << msg << std::endl;
        syslog(LOG_INFO,"%s",msg.c_str());

        /////////////////////////////////////////////////////////////
        // start the server (app) and wait for http events/requests
        //                  
        std::cout << "Running..." << std::endl;
        drogon::app()
            .addListener(host, port)
            .registerHandler("/", [](const drogon::HttpRequestPtr &,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody("Hello, Simplereaderd!");
                cb(resp);
            })
            .run();

    } catch (const std::exception &ex) {
        logFatal(ex,1);
    }

    syslog(LOG_INFO, "simplereaderd shutting down");
    closelog();

}

