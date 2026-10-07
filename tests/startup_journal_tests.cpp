#include "nfsu/startup_journal.h"
#include <cstdlib>
#include <string>
namespace {
int fail_sync(int) {errno=EIO;return -1;}
unsigned sync_calls=0;
int fail_final_sync(int fd) {return ++sync_calls==1?nfsu::startup_journal_sync(fd):fail_sync(fd);}
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    const std::string mode=argv[1];
    if(mode=="--write-and-abort") {
        nfsu::StartupJournal journal;
        if(!journal.open("startup.log") || !journal.mark("allocation.before","name=scene requested=2457600"))return 2;
        // No destructor or normal stream cleanup: models a fatal SDK abort.
        std::_Exit(23);
    }
    if(mode=="--exclusive") {
        nfsu::StartupJournal journal;
        return !journal.open("startup.log") && errno==EEXIST?0:3;
    }
    if(mode=="--sync-failure") {
        nfsu::StartupJournal journal(fail_sync);
        if(!journal.open("sync-failure.log"))return 4;
        if(journal.mark("allocation.before","name=scene") || std::string(journal.error()).find("fsync failed")==std::string::npos)return 5;
        return !journal.close()?0:6;
    }
    if(mode=="--close-failure") {
        nfsu::StartupJournal journal(fail_final_sync);
        if(!journal.open("close-failure.log") || !journal.mark("renderer.ready","ok=1"))return 7;
        return !journal.close() && std::string(journal.error()).find("final fsync failed")!=std::string::npos?0:8;
    }
    return 9;
}
