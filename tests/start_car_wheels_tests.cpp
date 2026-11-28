#include "nfsu/start_crowd.h"
#include "nfsu/intro_grid.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_events.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace nfsu;
#include "start_car_wheels_native.inc"
namespace {
void require(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
void unit() {
    for(const auto& row:kStartWheelNative) {
        const float actual=road_start_wheel_phase(row[0],row[1],row[2]);
        if(actual!=row[3]) {
            std::cerr.precision(12);
            std::cerr<<"native phase previous="<<row[0]<<" distance="<<row[1]<<" radius="<<row[2]
                     <<" expected="<<row[3]<<" actual="<<actual<<'\n';
            require(false,"NIS wheel accumulator differs from original GC arithmetic");
        }
    }
    require(road_start_wheel_phase(.25F,0,.3F)==.25F,"stationary wheel changed phase");
    require(road_start_wheel_phase(.25F,1,0)==.25F,"absent radius changed phase");
    for(const auto& row:kStartClockNative) {
        float frame=row[0];const bool finished=advance_road_start_car(frame,row[1],unsigned(row[2]));
        require(frame==row[3]&&finished==bool(row[4]),"NIS controller/finishing guard differs from original GC");
    }
}
std::set<unsigned> start_scenes(const std::filesystem::path& root) {
    std::vector<unsigned char> bytes;std::vector<RaceTableIssue> issues;
    require(load_race_image(root/"Global/INGAMEB.BUN",bytes,issues),"original NIS playlists unreadable");
    const auto word=[&](std::size_t at) {
        require(at<=bytes.size()&&4<=bytes.size()-at,"playlist word outside input");
        std::uint32_t value;std::memcpy(&value,bytes.data()+at,4);return value;
    };
    std::set<unsigned> scenes;bool found=false;
    for(std::size_t at=0;at<bytes.size();) {
        const auto id=word(at),size=word(at+4);at+=8;
        require(size<=bytes.size()-at,"playlist chunk outside input");
        if(id==0x34036U) {
            require(!found&&size>=96,"original NIS playlist shape");found=true;
            std::size_t cursor=at+96;
            // Original47BC63: road grids use lists0..8, solo drift list9.
            for(unsigned list=0;list<10;++list) {
                const auto count=word(at+4*list);require(count&&count<=64,"original playlist extent");
                require(cursor<=at+size&&4*count<=at+size-cursor,"playlist outside chunk");
                for(unsigned i=0;i<count;++i)scenes.insert(word(cursor+4*i));
                cursor+=4*count;
            }
        }
        at+=size;
    }
    require(found&&!scenes.empty(),"original start playlists absent");return scenes;
}
void retail(const std::filesystem::path& root) {
    std::string error;IntroGrid grid;
    require(load_race_grid(root,48,"RSX",2,grid,error),error);
    for(const auto& actor:grid.actors) {
        // Genuine rims retain independent live wheel spin and front steering.
        // An intro correction must not replace them with one chassis angle.
        CarAnimationState animation;animation.wheel_phase={{.1,.2,.3,.4}};
        auto state=actor.state;
        std::vector<PreparedInstance> original(actor.base_instances.size()),spun(original.size()),steered(original.size());
        require(drive_place_instances(actor.base_instances,state,actor.placement_spec,original,&animation),"real live rim baseline");
        animation.wheel_phase[2]+=.75;
        require(drive_place_instances(actor.base_instances,state,actor.placement_spec,spun,&animation),"independently spinning real wheel");
        animation.wheel_phase[2]-=.75;state.steer_angle+=.2;
        require(drive_place_instances(actor.base_instances,state,actor.placement_spec,steered,&animation),"independently steered real wheel");
        unsigned spinning=0,turning=0;
        for(std::size_t i=0;i<original.size();++i) {
            const auto& part=original[i];
            if(part.part_slot!=kSlotWheel&&part.part_slot!=kSlotBrake)continue;
            const bool spin=!std::equal(part.transform,part.transform+16,spun[i].transform);
            const bool turn=!std::equal(part.transform,part.transform+16,steered[i].transform);
            require(spin==(part.part_slot==kSlotWheel&&part.copy==2),"independent spin changed the wrong wheel or brake slot="+std::to_string(part.part_slot)+" copy="+std::to_string(part.copy));
            require(turn==(part.copy<2),"steering changed a rear wheel or omitted a front wheel/brake");
            spinning+=spin;turning+=turn;
        }
        require(spinning&&turning,"real car variant did not exercise spin and steering");
    }
    const auto scenes=start_scenes(root);
    unsigned moving=0,stationary=0,steering=0,matrices=0,cuts=0;
    for(const auto scene:scenes) {
        StartCrowd clip;RaceStartClock clock;
        require(clip.load_camera(posix_file_system(),root,scene,"L1RA",clock,error),
            "scene "+std::to_string(scene)+": "+error);
        if(!clock.animated_slots)continue;
        auto motion=make_road_start_motion(grid.course,clock.car_keys,error);
        require(bool(motion),error);++cuts;
        std::array<CarAnimationState,4> animations;
        std::array<VehicleVec3,4> previous{};
        std::array<double,4> travel{};
        for(auto& animation:animations)for(unsigned w=0;w<4;++w)animation.wheel_phase[w]=w*.25;
        const unsigned samples=unsigned(std::ceil(clock.release*2.0));
        for(unsigned tick=0;tick<=samples;++tick)for(unsigned slot=0;slot<4;++slot) {
            if(!(clock.animated_slots&(1U<<slot)))continue;
            const auto& actor=grid.actors.at(slot%grid.actors.size());
            const auto before=animations[slot].wheel_phase;
            VehicleVec3 position;PlayerRigidMatrix axes;double steer=0;
            require(motion->sample(slot,float(tick)/60,actor.placement_spec,position,axes,error,
                                  &animations[slot],&steer),error);
            if(tick)travel[slot]+=std::hypot(position.x-previous[slot].x,position.y-previous[slot].y);
            else require(before==animations[slot].wheel_phase,"first NIS placement spun a teleported wheel");
            previous[slot]=position;
            steering+=std::abs(steer)>1e-4;
            // A repeated pose/time is not another animation or physical tick.
            const auto sampled=animations[slot].wheel_phase;
            require(motion->sample(slot,float(tick)/60,actor.placement_spec,position,axes,error,
                                  &animations[slot],&steer),error);
            require(sampled==animations[slot].wheel_phase,"render sampling advanced NIS wheels twice");
            if(tick&&tick%30==0) {
                VehicleState state=actor.state;state.position=position;state.orientation=vehicle_orientation_from_axes(axes);
                state.rigid_body_ready=false;state.steer_angle=steer;
                std::vector<PreparedInstance> placed(actor.base_instances.size());
                require(drive_place_instances(actor.base_instances,state,actor.placement_spec,placed,&animations[slot]),"NIS real rim placement");
                unsigned wheels=0;
                for(const auto& part:placed)if(part.part_slot==kSlotWheel) {
                    require(part.copy<4,"real rim copy outside wheel set");wheels|=1U<<part.copy;
                    for(float value:part.transform)require(std::isfinite(value),"nonfinite real wheel matrix");
                }
                require(wheels==15,"NIS real car lost a wheel");++matrices;
            }
        }
        for(unsigned slot=0;slot<4;++slot) {
            if(!(clock.animated_slots&(1U<<slot)))continue;
            const auto& actor=grid.actors.at(slot%grid.actors.size());
            if(travel[slot]>.25) {
                for(unsigned w=0;w<4;++w)
                    require(animations[slot].wheel_phase[w]-w*.25>.01,"moving authored start car has stationary wheels");
                ++moving;
            }else ++stationary;
            const auto before=animations[slot].wheel_phase;
            VehicleVec3 position;PlayerRigidMatrix axes;double steer;
            require(motion->sample(slot,1000,actor.placement_spec,position,axes,error,&animations[slot],&steer),error);
            require(before==animations[slot].wheel_phase,"clamped final NIS pose added wheel travel");
            require(motion->sample(slot,1001,actor.placement_spec,position,axes,error,&animations[slot],&steer),error);
            require(before==animations[slot].wheel_phase,"held NIS pose spun its wheels");
        }
    }
    require(cuts&&moving&&steering&&matrices,"retail sweep did not exercise motion, steering and real rims");
    std::cout<<"PASS original start cuts="<<cuts<<" moving="<<moving<<" stationary="<<stationary
             <<" steering_samples="<<steering<<" real_matrix_sets="<<matrices<<'\n';
}
}
int main(int argc,char** argv) {
    try {require(argc<=2,"[retail-root]");unit();if(argc==2)retail(argv[1]);
        std::cout<<"PASS start car wheel native phase and independent wheel baselines\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
