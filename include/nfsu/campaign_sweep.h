#pragma once
#include "nfsu/race_events.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nfsu {
struct CampaignSweepCase {
    unsigned event,round;
    int track,direction;

    std::string filename() const {
        return "event-"+std::to_string(event)+"-round-"+std::to_string(round+1)+".log";
    }
};

// Keep the retail manifest independent of the port's supported race list.
// Missing or mismatched runtime support is reported as a failed case.
inline std::vector<CampaignSweepCase> campaign_sweep_cases(const RaceEventTable& table,unsigned last_event) {
    if(!table.ok())throw std::runtime_error("invalid campaign sweep event table");
    std::vector<CampaignSweepCase> cases;
    for(const auto& event:table.records) {
        if(event.number>last_event || event.kind==kRaceEventKindNeverAvailable)continue;
        for(unsigned i=0;i<event.races.size();++i) {
            const auto& race=event.races[i];
            cases.push_back({event.number,i,race.track,race.direction});
        }
    }
    std::sort(cases.begin(),cases.end(),[](const auto& a,const auto& b) {
        return a.event!=b.event?a.event<b.event:a.round<b.round;
    });
    if(cases.empty() || cases.front().event!=0 || cases.back().event!=last_event)
        throw std::runtime_error("incomplete campaign sweep event range");
    return cases;
}

enum class CampaignSweepStatus { pending, interrupted, failed, passed };
struct CampaignSweepRecord {
    CampaignSweepStatus status=CampaignSweepStatus::pending;
    std::string stage;
};

inline CampaignSweepRecord read_campaign_sweep_record(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path))return {};
    std::ifstream log(path,std::ios::binary);
    if(!log)throw std::runtime_error("cannot read campaign sweep case "+path.filename().string());
    CampaignSweepRecord result{CampaignSweepStatus::interrupted,"case.open"};
    bool reached_target=false;
    std::string line;
    // A truncated line from a process abort is never a terminal marker.
    while(std::getline(log,line) && !log.eof()) {
        constexpr const char* prefix="MENU STARTUP seq=";
        const auto at=line.find(" stage=");
        if(line.rfind(prefix,0)!=0 || at==std::string::npos)continue;
        const auto from=at+7;
        const auto stage=line.substr(from,line.find(' ',from)-from);
        if(stage=="race.target")reached_target=true;
        if(stage=="case.fail")result.status=CampaignSweepStatus::failed;
        else if(stage=="case.pass" && reached_target)result.status=CampaignSweepStatus::passed;
        else result.stage=stage;
    }
    if(log.bad())throw std::runtime_error("cannot read complete campaign sweep case "+path.filename().string());
    return result;
}
}
