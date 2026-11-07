#pragma once
#include "nfsu/intro_grid.h"
#include "nfsu/intro_ai.h"
// Compatibility wrapper: probes execute the same planner as the game.
class Ug0PathChoiceProbe : public nfsu::IntroAiPlanner {
public:
    bool load(const std::filesystem::path& root,const nfsu::IntroGrid& grid,bool retail_speed=false) {
        std::array<int,3> contexts{};
        std::array<const nfsu::VehicleHull*,4> hulls{};
        unsigned at=0;
        for(const auto& actor:grid.actors) {
            if(actor.config.role==nfsu::Event0Role::human)hulls[3]=&actor.hull;
            else if(at<3) {hulls[at]=&actor.hull;contexts[at++]=actor.config.selected_context;}
            else return false;
        }
        return at==3&&IntroAiPlanner::load(root,contexts,hulls,retail_speed);
    }
};
