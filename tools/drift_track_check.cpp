#include "nfsu/race_gpu_scene.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/race_hud.h"
#include "nfsu/start_crowd.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "../tests/drift_popup_native.inc"
#include "../tests/race_camera_native.inc"
int main(int argc,char** argv) {
    std::string error;std::filesystem::path root;
    const bool finish_probe=argc==3&&std::string(argv[2])=="--finish";
    if((argc!=2&&argc!=5&&!finish_probe) || !nfsu::argument_to_path(argv[1],root,error))return 2;
    const float slide_speed=argc==5?std::strtof(argv[2],nullptr):22;
    const unsigned slide_frames=argc==5?unsigned(std::strtoul(argv[3],nullptr,10)):60;
    const int slide_axis=argc==5?std::atoi(argv[4]):-12000;
    if(!std::isfinite(slide_speed)||slide_speed<10||slide_speed>30||slide_frames>120||std::abs(slide_axis)>32767)return 2;
    nfsu::DriftScorePublication publication;nfsu::DriftScoreDisplay popup;unsigned popup_zone=0;
    const auto from_bits=[](unsigned u){float v;std::memcpy(&v,&u,4);return v;};
    for(const auto& row:kNativeDriftPopup) {
        if(row[0]!=popup_zone){publication={};popup={};popup_zone=row[0];}
        if(row[2]!=0xffffffffU)publication.publish(from_bits(row[3]),int(row[5]),row[2]==4,int(row[1]));
        popup.sample(publication,int(row[1]),row[0]);
        const auto& c=popup.color;const unsigned packed=(unsigned(c[0])<<24)|(unsigned(c[1])<<16)|(unsigned(c[2])<<8)|c[3];
        if(popup.points!=from_bits(row[4])||popup.multiplier!=int(row[5])||popup.failed!=bool(row[6])||
           popup.visible!=bool(row[7])||packed!=row[8]||(popup.visible&&int(popup.points)!=int(row[9]))) {
            std::cerr<<"FAIL native drift popup at "<<row[1]<<" zone "<<row[0]<<'\n';return 1;
        }
        const auto before=popup;popup.sample(publication,int(row[1]),row[0]);
        if(popup.visible!=before.visible||popup.points!=before.points||popup.color!=before.color){
            std::cerr<<"FAIL repeated same-frame popup render changed state";return 1;
        }
    }
    std::cout<<"PASS original drift item0 publication, failed retention, color and expiration\n";
    nfsu::RaceGpuScene scene;nfsu::RaceRun race;
    if(!race.set_event(3,error) || !race.set_car("CIVIC",error) || !race.prepare_metadata(root,error) ||
       !scene.load(nfsu::posix_file_system(),root,error,nullptr,nullptr,false,false,false,nullptr,nullptr,3) ||
       !race.start(root,scene.track(),error)) {std::cerr<<"FAIL: "<<error<<'\n';return 1;}
    if(race.session().camera().pov!=int(kNativeRaceCameraStart[4][2])) {
        std::cerr<<"FAIL original drift entry high camera";return 1;
    }
    std::cout<<"PASS original drift entry selects high camera\n";
    nfsu::RaceHudAtlas atlas;
    if(!nfsu::load_race_hud_atlas(root,atlas,error,false,false,true)){std::cerr<<"FAIL HUD: "<<error;return 1;}
    const auto appearance=[&](const char* object,const char* state,unsigned tick) {
        return nfsu::sample_drift_hud_animation(atlas,object,state,tick);
    };
    const auto pulse=appearance("drift_multiplier_text","Highlight",300);
    const auto end=appearance("drift_multiplier_text","Highlight",600);
    const auto warning=appearance("SlowMessage","LosingMult",0);
    const auto repeat=appearance("SlowMessage","LosingMult",400);
    const auto slow=appearance("SlowMessage","TooSlowToGain",0);
    const auto total=appearance("SP_DRIFT_TOTAL","Highlight",0);
    const auto settled=appearance("SP_DRIFT_TOTAL","Highlight",400);
    if(std::abs(pulse.scale-1.05F)>1e-6F || std::abs(end.scale-.9F)>1e-6F ||
       warning.color!=repeat.color || warning.color!=std::array<unsigned char,3>{{160,32,32}} ||
       slow.color!=std::array<unsigned char,3>{{255,128,32}} ||
       total.color!=std::array<unsigned char,3>{{160,32,32}} ||
       settled.color!=std::array<unsigned char,3>{{207,233,242}} || settled.alpha!=200) {
        std::cerr<<"FAIL authored HUD keyframes/loop/return to Init";return 1;
    }
    std::cout<<"PASS authored HUD highlights, warning loop and return to Init\n";
    if(atlas.drift_comment.position!=std::array<float,2>{{0,-157}}||
       atlas.drift_comment.width!=.75F||atlas.drift_comment.color!=std::array<unsigned char,4>{{0,128,192,180}}){
        std::cerr<<"FAIL authored drift achievement geometry/color";return 1;
    }
    for(char letter:std::string("GOOD DRIFTGREAT SUPERB!COLOSSAL!OUTRAGEOUS!INSANE DRIFT!!DRIFT KING!!"))
        if(letter!=' '&&!atlas.lap_fonts[1][unsigned(letter)].width){std::cerr<<"FAIL drift achievement glyph absent";return 1;}
    nfsu::StartCrowd crowd;
    if(!crowd.load(nfsu::posix_file_system(),root,error,nfsu::kFirstDriftStartScene,1301)) {
        std::cerr<<"FAIL drift crowd: "<<error;return 1;
    }
    crowd.frame(race.session());
    if(!crowd.draw_count() || crowd.actor_count()<2){std::cerr<<"FAIL drift crowd missing";return 1;}
    const auto* upload=crowd.commands().resource_table().resources[0].data;
    const auto first_sample=crowd.commands().owning_draws()[0].first_vertex;
    bool crowd_moved=false,rolling_wheels=false;
    nfsu::CarAnimationState preceding_animation;
    nfsu::VehicleState preceding_car;
    bool preceding_pose=false;
    nfsu::RaceHudFrame hud;nfsu::RaceMinimapState map;
    nfsu::build_race_hud_frame(race.session(),atlas,1280,720,hud);
    if(!nfsu::append_race_minimap(race.session(),atlas,1280,720,map,hud)||hud.quads || !race.session().start_cinematic()){std::cerr<<"FAIL cinematic HUD visible";return 1;}
    std::cout<<"PASS cinematic hides drift HUD and map: "<<hud.quads<<" quads\n";
    nfsu::RaceGpuFrame frame;
    if(!scene.frame(race.session(),frame,error)) {std::cerr<<"FAIL: "<<error<<'\n';return 1;}
    if(!frame.track_draws||!frame.car_draws||!frame.triangles)return 1;
    nfsu::RaceRouteDriver driver(nfsu::RaceDrivingLine::progress_spline);
    float largest_angle=0,largest_score=0,smallest_grip=2,largest_speed=0;
    unsigned slide_at=1500;
    for(unsigned tick=0;tick<1500;++tick) {
        auto pad=driver.sample(race.session());
        // Input-only high-speed slide probe. The ordinary route driver caps
        // speed below the drift admission threshold; leave physics unchanged.
        if(!nfsu::race_car_held(race.session().timing()) && tick<slide_at+slide_frames)
            pad.pads[0].buttons=nfsu::gamepad_button_bit(nfsu::GamepadButton::trigger_right);
        if(slide_at==1500 && race.session().car().owner_speeds.magnitude>slide_speed)slide_at=tick;
        if(tick>=slide_at && tick<slide_at+slide_frames) {
            if(tick<slide_at+3)pad.pads[0].buttons|=nfsu::gamepad_button_bit(nfsu::GamepadButton::face_south);
            pad.pads[0].axes[0]=slide_axis;
        }
        if(!race.simulate(pad,error)||!race.commit(error)){std::cerr<<"FAIL slide: "<<error;return 1;}
        crowd.frame(race.session());
        nfsu::VehicleState shown;auto animation=race.session().animation();
        if(crowd.car(race.session(),shown,&animation)) {
            if(preceding_pose && std::hypot(shown.position.x-preceding_car.position.x,shown.position.y-preceding_car.position.y)>.0001)
                rolling_wheels|=animation.wheel_phase!=preceding_animation.wheel_phase;
            auto repeat=race.session().animation();nfsu::VehicleState same;
            if(!crowd.car(race.session(),same,&repeat)||repeat.wheel_phase!=animation.wheel_phase) {
                std::cerr<<"FAIL cinematic wheel phase advances while paused";return 1;
            }
            preceding_car=shown;preceding_animation=animation;preceding_pose=true;
        }
        if(upload!=crowd.commands().resource_table().resources[0].data) {
            std::cerr<<"FAIL drift crowd upload changed";return 1;
        }
        if(tick<192*2)crowd_moved|=crowd.commands().owning_draws()[0].first_vertex!=first_sample;
        if((tick<810 && !crowd.draw_count()) || (tick>825 && crowd.draw_count())) {
            std::cerr<<"FAIL drift crowd retirement epoch";return 1;
        }
        if(tick==420 || tick==680) {
            nfsu::build_race_hud_frame(race.session(),atlas,1280,720,hud);
            if(race.session().start_cinematic() || !hud.quads ||
               (tick==420 && hud.lap.presentation_ready) ||
               (tick==680 && !hud.lap.presentation_ready)){std::cerr<<"FAIL HUD not restored tick="<<tick<<" quads="<<hud.quads<<" lap_first="<<hud.lap_first_quad<<" cinematic="<<race.session().start_cinematic()<<" stage="<<unsigned(race.session().timing().stage);return 1;}
        }
        largest_angle=std::max(largest_angle,std::abs(race.session().drift().angle()));
        largest_score=std::max(largest_score,race.session().style().item(0).points);
        smallest_grip=std::min(smallest_grip,float(race.session().car().wheels[2].drift_grip));
        largest_speed=std::max(largest_speed,float(race.session().car().owner_speeds.magnitude));
    }
    std::cout<<"LIVE slide tick="<<slide_at<<" speed="<<largest_speed<<" angle="<<largest_angle<<" pending="<<largest_score<<" banked="<<race.session().style().item(0).total<<" grip="<<smallest_grip<<"\n";
    if(!crowd_moved||!rolling_wheels){std::cerr<<"FAIL drift crowd or rolling wheels did not animate";return 1;}
    std::cout<<"PASS drift crowd animation, stable uploads, retirement and staged HUD visibility\n";
    if(largest_score<=0 || smallest_grip>=1 || race.session().style().item(0).total<=0){std::cerr<<"FAIL live drift did not bank";return 1;}
    if(finish_probe) {
        unsigned tick=1500;
        while(race.session().timing().outcome==nfsu::RaceOutcome::running&&tick<30000) {
            if(!race.simulate(driver.sample(race.session()),error)||!race.commit(error)) {
                std::cerr<<"FAIL drift completion: "<<error;return 1;
            }
            ++tick;
        }
        if(race.session().timing().outcome!=nfsu::RaceOutcome::finished) {
            std::cerr<<"FAIL drift route did not finish at step "<<tick;return 1;
        }
        const auto result=nfsu::race_timing_results(race.session().course().route,race.session().timing());
        const auto before=race.session().car().position;
        const auto at=race.session().timing().step;
        unsigned braking=0;
        for(;braking<900;++braking) {
            if(!race.simulate(nfsu::race_input_frame({1,1,0},tick++),error)||!race.commit(error)) {
                std::cerr<<"FAIL drift finish driver: "<<error;return 1;
            }
            if(race.session().car().owner_speeds.magnitude<=1.F)break;
        }
        const auto after=race.session().car().position;
        const auto ended=nfsu::race_timing_results(race.session().course().route,race.session().timing());
        if(braking==900||race.session().timing().step<=at||
           std::hypot(after.x-before.x,after.y-before.y)<.01||
           result[0].total_ticks!=ended[0].total_ticks) {
            std::cerr<<"FAIL drift must keep its result while braking to rest";return 1;
        }
        std::cout<<"PASS drift crosses finish and continues physical braking: "<<braking<<" frames\n";
    }
    std::cout<<"PASS drift track 1301 first frame: "<<frame.track_draws<<" track draws, "
             <<frame.car_draws<<" car draws, "<<frame.triangles<<" triangles; live input-only slide verified\n";
}
