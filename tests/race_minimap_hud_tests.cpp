#include "nfsu/race_hud.h"
#include "nfsu/arg_path.h"
#include "nfsu/image.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstring>

using namespace nfsu;
namespace {
int failures=0;
void check(bool ok,const char* message) {
    if (!ok) {++failures;std::fprintf(stderr,"FAIL: %s\n",message);}
}
void focused() {
    RaceMinimapState state;
    update_race_minimap_direction({{1,0}},0,0,state);
    update_race_minimap_direction({{0,1}},67,0,state);
    check(std::abs(state.direction[0]-0.9993250966072083F)<1e-7F &&
          std::abs(state.direction[1]-0.0006749150925315917F)<1e-8F &&
          std::abs(state.velocity[1]-0.08908646553754807F)<1e-7F,
          "camera filter matches original supplied 67-tick turn");
    const auto saved=state;
    update_race_minimap_direction({{0,1}},67,0,state);
    check(state.direction==saved.direction && state.velocity==saved.velocity,"same tick/pause retains filter");
    update_race_minimap_direction({{-1,0}},134,1,state);
    check(state.direction==MapPoint{{-1,0}} && state.velocity==MapPoint{},"port reset restarts map history");
    VehicleTrackCourse course;std::string error;
    check(build_vehicle_proving_ground(course,error),"course builds");
    RaceSession session(course);check(session.start(error),"session starts");
    RaceHudAtlas atlas;atlas.map.valid=true;atlas.map.hash=0xA4F2F595;
    atlas.map.width=atlas.map.height=256;atlas.map.world_span=1202;
    RaceHudFrame frame;build_race_hud_frame(session,1280,720,frame);
    const auto before=session.record();const auto count=frame.quads;
    state={};check(append_race_minimap(session,atlas,1280,720,state,frame),"integrated map builds");
    check(frame.quads>count && frame.quads<kRaceHudMaxQuads,"map and player append within capacity");
    for (std::size_t i=count*4;i<frame.quads*4;++i) {
        const auto& v=frame.vertices[i];
        check(std::isfinite(v.px) && std::isfinite(v.py) && v.pz>0,"finite perspective vertices");
        check(v.u>=512.0F/kRaceHudAtlasWidth && v.u<837.0F/kRaceHudAtlasWidth &&
              v.v>=0 && v.v<258.0F/kRaceHudAtlasSize,"map UVs cannot bleed into gauge/fonts");
    }
    RaceHudFrame repeated;build_race_hud_frame(session,1280,720,repeated);
    check(append_race_minimap(session,atlas,1280,720,state,repeated),"same snapshot builds again");
    check(frame.quads==repeated.quads && std::memcmp(frame.vertices.data(),repeated.vertices.data(),
          frame.quads*4*sizeof(TrackVertex))==0,"repeated presentation leaves map unchanged");
    check(before==session.record(),"map presentation leaves physics and recording unchanged");
    repeated.quads=kRaceHudMaxQuads;
    check(!append_race_minimap(session,atlas,1280,720,state,repeated) &&
          repeated.quads==kRaceHudMaxQuads,"capacity failure does not publish partial map");
    atlas.map.valid=false;
    check(append_race_minimap(session,atlas,1280,720,state,repeated),"unselected track does not render first-track map");
}
int capture(const std::filesystem::path& root,const std::filesystem::path& output) {
    if (std::filesystem::exists(output)) {std::puts("FAIL: fresh capture directory required");return 1;}
    RaceHudAtlas atlas;VehicleTrackCourse course;std::string error;
    if (!load_race_hud_atlas(root,atlas,error,true) ||
        !load_vehicle_track_course(root/"Tracks/TRACKB1003.lzc",2,0,0,0,course,error)) {
        std::fprintf(stderr,"FAIL: %s\n",error.c_str());return 1;
    }
    RaceSession session(course);if(!session.start(error))return 1;
    std::filesystem::create_directory(output);
    RaceMinimapState state;
    for (unsigned tick=0;tick<=360;++tick) {
        if (tick==0 || tick==240 || tick==360) {
            RaceHudFrame frame;build_race_hud_frame(session,atlas,1280,720,frame);
            if (!append_race_minimap(session,atlas,1280,720,state,frame))return 1;
            RaceFpsHistory history;for (int i=0;i<90;++i)history.add(1000.0/60);
            append_race_fps_overlay(atlas,history,1280,720,frame);
            check(frame.quads<kRaceHudMaxQuads,"lap, gauge, map and FPS fit together");
            std::vector<unsigned char> rgba(1280*720*4,64);
            composite_race_hud(atlas,frame,rgba.data(),1280,720,1280*4);
            std::ofstream stream(output/("hud-"+std::to_string(tick)+".tga"),std::ios::binary);
            if(!write_tga(stream,1280,720,rgba,error))return 1;
        }
        if (tick<360) {
            auto pad=race_input_frame({1,tick?gamepad_button_bit(GamepadButton::trigger_right):0,tick>240?8000:0},tick);
            if(!session.advance(pad,error)) {std::fprintf(stderr,"FAIL: %s\n",error.c_str());return 1;}
            RaceHudFrame ignored;build_race_hud_frame(session,atlas,1280,720,ignored);
            if(!append_race_minimap(session,atlas,1280,720,state,ignored))return 1;
        }
    }
    std::puts("PASS: retail map/fonts decoded and integrated first-track HUD captures written");
    return failures?1:0;
}
}
int main(int argc,char** argv) {
    focused();
    if (argc==4 && std::strcmp(argv[1],"--capture")==0)
        return capture(argument_path(argv[2]),argument_path(argv[3]));
    if (argc!=1)return 1;
    std::printf("minimap HUD: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
