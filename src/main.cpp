#include <drogon/drogon.h>
#include <iostream>
#include <syslog.h>
#include <signal.h>
#include <stdlib.h>

#include "Config.h"
#include "Database.h"
#include "dh_root.h"
#include "dh_login.h"
#include "dh_check.h"
#include "dh_resolve.h"
#include "dh_get.h"
#include "dh_getSince.h"
#include "dh_getBook.h"
#include "dh_uploadBook.h"
#include "dh_update.h"
#include "dh_delete.h"
#include "utils.h"
#include "version.h"

void handleSignal(int sig) {
    syslog(LOG_ERR, "simplereaderd terminated by signal %d", sig);
    closelog();
    exit(1);
}

int main() {

    // open syslog
    openlog("simplereaderd", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    // catch signals
    signal(SIGINT,  handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGSEGV, handleSignal); // crash (segfault)

    try {
        ////////////////////////////////////////////////////////////////////////
        // configure the daemon
        //
        Config::get().load(); // load singleton Config

        std::string msg = std::string("simplereaderd v") + SIMPLEREADERD_VERSION + " starting on " + Config::get().host() + ":" +
                   std::to_string(Config::get().port()) + " (compat=" + Config::get().compat() +
                   ", maxFileSize=" + std::to_string(Config::get().maxFileSizeMB()) + "MB)";
        
        std::cout << msg << std::endl;
        syslog(LOG_INFO,"%s",msg.c_str());

        ////////////////////////////////////////////////////////////////////////
        // start sqlite server
        //   
        Database::get().open("/var/lib/simplereader/app.db");

        ////////////////////////////////////////////////////////////////////////
        // start the server (app) and wait for http events/requests to roll in
        //                  
        registerLoginHandler();
        registerRootHandler();
        registerCheckHandler();
        registerResolveHandler();
        registerGetHandler();
        registerGetSinceHandler();
        registerGetBookHandler();
        registerUploadBookHandler();
        registerUpdateHandler();
        registerDeleteHandler();
        std::cout << "Running..." << std::endl;
        
        drogon::app()
            .setClientMaxBodySize(Config::get().maxFileSize())             // limit upload requests to maxFileSize
            .setClientMaxMemoryBodySize(2 * 1024 * 1024)                   // keep 2 MB in RAM, then packetize
            .setUploadPath("/var/lib/simplereader/tmp");                   // temp dir for large files

        drogon::app()
            .addListener(Config::get().host(), Config::get().port())
            .run();

    } catch (const std::exception &ex) {
        logFatal(ex,1);
    }

    syslog(LOG_INFO, "simplereaderd shutting down");
    Database::get().close();
    closelog();
    return 0;
}

