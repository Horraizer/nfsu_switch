#pragma once
#include "nfsu/campaign_sweep.h"
#include "nfsu/career.h"
#include "nfsu/frontend.h"

namespace nfsu {
inline FrontendInput campaign_finish_post_input(const PostRaceMenu& menu) {
    FrontendInput input;
    if(menu.fading())return input;
    if(menu.confirming()) {
        input.right=!menu.confirmed();input.accept=menu.confirmed();
    }else if(menu.page()==PostRacePage::menu&&menu.next_race()&&menu.selected()==0) {
        input.down=true;
    }else input.accept=true;
    return input;
}

inline CareerProfile load_campaign_finish_case(const std::filesystem::path& base,
    const CareerCatalog& career,const CampaignSweepCase& test,const std::string& car) {
    CareerProfile profile;
    std::string error;
    if(!test.event)return profile;
    if(test.round) {
        if(!load_career(base,career,profile,error))throw std::runtime_error(error);
        if(profile.selected_event!=test.event||profile.page!=CareerPage::briefing||
           profile.tournament.event!=test.event||profile.tournament.round!=test.round)
            throw std::runtime_error("previous tournament round did not persist the next launch");
        return profile;
    }
    profile.car=car;profile.page=CareerPage::map;profile.selected_event=test.event;
    // Seed only earlier campaign events, so the case remains independent of
    // unrelated failures. Later rounds still require the actual previous save.
    for(const auto& event:career.events().records)
        if(event.number<test.event&&event.kind!=kRaceEventKindNeverAvailable)
            profile.status[event.number]=kEventStatusWon;
    if(!career.begin_event(profile,error) || (profile.movie_phase&&!career.movie_finished(profile,error)))
        throw std::runtime_error(error);
    return profile;
}

inline CareerProfile save_campaign_finish_case(const std::filesystem::path& base,
    const CareerCatalog& career,const CampaignSweepCase& test,bool next_round,
    CareerProfile profile,const std::array<unsigned,4>& places,int style,const CareerRaceReport& race) {
    if(!race.started||!race.won)throw std::runtime_error("finish sweep did not produce a win");
    const auto bank=profile.bank;
    std::string error;
    if(test.event) {
        record_career_statistics(profile.statistics,career.statistics_track_mask(test.event,test.round),race);
        if(!apply_career_race_result(career,profile,1,places,style,error))throw std::runtime_error(error);
    }else if(!career.intro_won(profile,error))throw std::runtime_error(error);
    CareerProfile restored;
    if(!save_career(base,career,profile,error)||!load_career(base,career,restored,error))throw std::runtime_error(error);
    if(restored.status!=profile.status||restored.bank!=profile.bank||restored.page!=profile.page||
       restored.selected_event!=profile.selected_event||restored.races_won!=profile.races_won||
       restored.statistics.wins!=profile.statistics.wins||restored.tournament.event!=profile.tournament.event||
       restored.tournament.round!=profile.tournament.round||restored.tournament.places!=profile.tournament.places)
        throw std::runtime_error("finish sweep save changed across reload");
    if(next_round) {
        if(restored.status[test.event]==kEventStatusWon||restored.bank!=bank||restored.page!=CareerPage::briefing||
           restored.tournament.round!=test.round+1||restored.tournament.places[test.round]!=places||places[0]!=1)
            throw std::runtime_error("finish sweep next round or intermediate standings missing");
    }else {
        const auto* media=career.media(test.event);
        if(restored.status[test.event]!=kEventStatusWon||restored.tournament.event||!media||
           restored.bank!=bank+media->reward[profile.difficulty]||(!test.event&&restored.page!=CareerPage::intro_short))
            throw std::runtime_error("finish sweep win or reward missing after reload");
    }
    return restored;
}
}
