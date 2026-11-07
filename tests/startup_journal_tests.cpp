#include "nfsu/startup_journal.h"
#include "nfsu/campaign_sweep.h"
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
    if(mode=="--sweep-abort") {
        nfsu::StartupJournal journal;
        if(!journal.open("event-0-round-1.log") || !journal.mark("case.begin","event=0",false) ||
           !journal.mark("race.first_frame","",false))return 10;
        std::_Exit(23);
    }
    if(mode=="--sweep-resume") {
        using namespace nfsu;
        const auto intro=read_campaign_sweep_record("event-0-round-1.log");
        if(intro.status!=CampaignSweepStatus::interrupted || intro.stage!="race.first_frame" ||
           read_campaign_sweep_record("event-1-round-1.log").status!=CampaignSweepStatus::pending)return 11;
        StartupJournal log;
        if(!log.open("event-1-round-1.log") || !log.mark("case.begin","event=1",false) ||
           !log.mark("race.target","after_go_ms=2000",false) || !log.mark("case.pass","elapsed_ms=3000",false) ||
           !log.close())return 12;
        if(!log.open("event-1-round-2.log") || !log.mark("case.begin","event=1",false) ||
           !log.mark("race.gpu_upload","",false) || !log.mark("case.fail","reason=upload refused",false) ||
           !log.close())return 13;
        const auto passed=read_campaign_sweep_record("event-1-round-1.log");
        const auto failed=read_campaign_sweep_record("event-1-round-2.log");
        if(passed.status!=CampaignSweepStatus::passed || passed.stage!="race.target" ||
           failed.status!=CampaignSweepStatus::failed || failed.stage!="race.gpu_upload")return 14;
        for(const bool torn:{false,true}) {
            std::ofstream f("torn.log",std::ios::binary|std::ios::trunc);
            f<<"MENU STARTUP seq=1 stage=case.begin event=2\n";
            if(torn)f<<"MENU STARTUP seq=2 stage=race.target after_go_ms=2000\n";
            f<<"MENU STARTUP seq=3 stage=case.pass elapsed_ms=3000";
            if(!torn)f<<'\n';
            f.close();
            if(read_campaign_sweep_record("torn.log").status!=CampaignSweepStatus::interrupted)return 15;
        }
        return 0;
    }
    return 9;
}
