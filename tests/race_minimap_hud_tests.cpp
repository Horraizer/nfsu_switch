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
    RaceSession session(course);
    std::vector<RaceRivalSetup> rivals;
    for(unsigned i=0;i<3;++i){auto pose=course.start;pose.ground.x+=4*(i+1);rivals.push_back({stock_240sx_vehicle_spec(),pose,stock_240sx_hull(),"rival"});}
    check(session.set_rivals(rivals,error),"three rivals configured");
    check(session.start(error),"session starts");
    // The synthetic proving loop has no authored start-line marker. Give
    // presentation an explicit line at its existing grid marker.
    course.route.start=0;
    if(course.route.boundaries.empty())course.route.boundaries.resize(1);
    course.route.boundaries[0].marker=course.start_marker;
    RaceHudAtlas atlas;atlas.map.valid=true;atlas.map.hash=0xA4F2F595;
    atlas.map.width=atlas.map.height=256;atlas.map.world_span=1202;
    RaceHudFrame frame;build_race_hud_frame(session,1280,720,frame);
    const auto before=session.record();const auto count=frame.quads;
    state={};check(append_race_minimap(session,atlas,1280,720,state,frame),"integrated map builds");
    check(frame.quads>count && frame.quads<kRaceHudMaxQuads,"map and player append within capacity");
    for (std::size_t i=count*4;i<frame.quads*4;++i) {
        const auto& v=frame.vertices[i];
        check(std::isfinite(v.px) && std::isfinite(v.py) && v.pz>=0 &&
              std::isfinite(v.u) && std::isfinite(v.v),"finite route and affine marker vertices");
    }
    unsigned blue_vertices=0;
    for(std::size_t i=count*4;i<frame.quads*4;++i) {const auto& v=frame.vertices[i];blue_vertices+=v.pz==0 && v.color[0]==0 && v.color[1]>0 && v.color[2]>v.color[1];}
    check(blue_vertices>=12,"three rival markers submit original blue palette");
    check(frame.vertices[(frame.quads-1)*4].color[0]>0 &&
          frame.vertices[(frame.quads-1)*4].color[2]==0,
          "native human marker is last, above overlapping rivals");
    unsigned line_vertices=0;
    for(std::size_t i=count*4;i<frame.quads*4;++i) {
        const auto& v=frame.vertices[i];
        if(v.pz==0 && v.u>=820.0F/kRaceHudAtlasWidth && v.u<=836.0F/kRaceHudAtlasWidth)++line_vertices;
    }
    check(line_vertices>=4,"start/finish line uses its own atlas sprite");
    RaceHudFrame repeated;build_race_hud_frame(session,1280,720,repeated);
    check(append_race_minimap(session,atlas,1280,720,state,repeated),"same snapshot builds again");
    check(frame.quads==repeated.quads && std::memcmp(frame.vertices.data(),repeated.vertices.data(),
          frame.quads*4*sizeof(TrackVertex))==0,"repeated presentation leaves map unchanged");
    check(before==session.record(),"map presentation leaves physics and recording unchanged");
    // Original58E810 swaps line angles only for reversed point-to-point races.
    // Distinct start/finish art angles catch accidental circuit swapping.
    atlas.map.start_angle=4000;atlas.map.finish_angle=26000;
    RaceHudFrame forward,reverse;RaceMinimapState forward_state{},reverse_state{};
    build_race_hud_frame(session,1280,720,forward);
    check(append_race_minimap(session,atlas,1280,720,forward_state,forward),"forward circuit marker");
    course.route.reverse=true;build_race_hud_frame(session,1280,720,reverse);
    check(append_race_minimap(session,atlas,1280,720,reverse_state,reverse),"reverse circuit marker");
    check(forward.quads==reverse.quads && !std::memcmp(forward.vertices.data(),reverse.vertices.data(),
        forward.quads*4*sizeof(TrackVertex)),"reverse circuit retains authored start-line orientation");
    course.route.reverse=false;atlas.map.start_angle=atlas.map.finish_angle=0;
    // Far outside the authored image, native WRAP still fills the same HUD
    // window. Clipping by source UV would remove the route altogether.
    atlas.map.offset_x=-12020;
    RaceHudFrame outside;build_race_hud_frame(session,1280,720,outside);
    check(append_race_minimap(session,atlas,1280,720,state,outside),"outside artwork still builds");
    unsigned route_vertices=0;
    for (std::size_t i=count*4;i<outside.quads*4;++i) route_vertices+=outside.vertices[i].pz>0;
    check(route_vertices>=4,"HUD window retains route beyond texture edges");
    atlas.map.offset_x=0;
    // Native submission has unit position W, separate projected texture UVQ.
    // Varying texture Q must not perspective-weight alpha: this pixel is 25.
    RaceHudAtlas white;white.rgba.assign(kRaceHudAtlasWidth*kRaceHudAtlasSize*4,255);
    RaceHudFrame alpha;alpha.quads=1;alpha.lap_first_quad=0;
    for (unsigned i=0;i<4;++i) {
        auto& v=alpha.vertices[i];v.px=(i==1 || i==2)?4:0;v.py=i>=2?4:0;
        v.pz=(i==1 || i==2)?4:1;
        v.u=600.0F/kRaceHudAtlasWidth;v.v=100.0F/kRaceHudAtlasSize;
        v.color[0]=v.color[1]=v.color[2]=255;
        v.color[3]=static_cast<unsigned char>(i>=2?200:0);
    }
    std::array<unsigned char,64> pixel{};
    composite_race_hud(white,alpha,pixel.data(),4,4,16);
    check(pixel[0]==25,"route alpha is screen-affine independently of projected UVs");
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
    RaceSession session(course);
    std::vector<RaceRivalSetup> rivals;
    for(unsigned i=0;i<3;++i){auto pose=course.start;pose.ground.x+=8*(i+1);rivals.push_back({stock_240sx_vehicle_spec(),pose,stock_240sx_hull(),"rival"});}
    if(!session.set_rivals(rivals,error)||!session.start(error))return 1;
    std::filesystem::create_directory(output);
    check(atlas.countdown_ready && atlas.lap_fonts[1]['3'].width && atlas.lap_fonts[1]['G'].width,
        "original321_Go object and countdown glyphs loaded");
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
    for(double time:{0.0,.2,.5,2.0}) {
        RaceHudFrame frame;build_race_hud_frame(session,atlas,1280,720,frame);
        check(append_race_minimap(session,atlas,1280,720,state,frame),"finish map fits");
        const auto before=frame.quads;append_race_over(atlas,time,1280,720,frame);
        check(frame.quads>before && frame.quads<kRaceHudMaxQuads,"finish banner fits full HUD");
        std::vector<unsigned char> rgba(1280*720*4,64);
        composite_race_hud(atlas,frame,rgba.data(),1280,720,1280*4);
        std::ofstream stream(output/("finish-"+std::to_string(int(time*1000))+".tga"),std::ios::binary);
        if(!write_tga(stream,1280,720,rgba,error))return 1;
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
