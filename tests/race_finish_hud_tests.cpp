#include "nfsu/race_hud.h"
#include "nfsu/arg_path.h"
#include "nfsu/image.h"
#include "nfsu/frontend.h"
#include "nfsu/gpu_upload.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {
struct NativeRaceOver {unsigned tick;float left,right,top,bottom;unsigned color;};
#include "race_over_native.inc"
struct NativeRaceOverMode {unsigned mode,text,tick,alpha;const char* message;float left,right,top,bottom;};
#include "race_finish_modes_native.inc"
#include "race_finish_flow_native.inc"
#include "race_over_clock_native.inc"
unsigned failures=0;

void check(bool okay,const char* label) {
    if(!okay){++failures;std::fprintf(stderr,"FAIL: %s\n",label);}
}

std::array<float,4> bounds(const nfsu::RaceHudFrame& frame,std::size_t begin,std::size_t end) {
    const auto infinity=std::numeric_limits<float>::infinity();
    std::array<float,4> result{{infinity,-infinity,infinity,-infinity}};
    for(auto q=begin;q<end;++q)for(unsigned i=0;i<4;++i) {
        const auto& v=frame.vertices[4*q+i];
        result[0]=std::min(result[0],v.px);result[1]=std::max(result[1],v.px);
        result[2]=std::min(result[2],v.py);result[3]=std::max(result[3],v.py);
    }
    return result;
}
}

int main(int argc,char** argv) {
    using namespace nfsu;
    if(argc<2||argc>3)return 2;
    const auto root=argument_path(argv[1]);std::string error;
    RaceHudAtlas atlas;
    if(!load_race_hud_atlas(root,atlas,error,true,true,false,1008,true)) {
        std::fprintf(stderr,"FAIL: finish HUD atlas %s\n",error.c_str());return 1;
    }
    std::filesystem::path output;
    if(argc==3) {
        output=argument_path(argv[2]);
        if(std::filesystem::exists(output)){std::fprintf(stderr,"FAIL: capture output exists\n");return 1;}
        std::filesystem::create_directories(output);
    }
    for(const auto size:{std::array<unsigned,2>{{640,480}},{{1280,720}},{{1920,1080}},{{1280,1024}}}) {
        const auto width=size[0],height=size[1];
        const float scale=std::min(width/640.F,height/480.F);
        const float left=(width-640*scale)*.5F,top=(height-480*scale)*.5F;
        for(unsigned sample=0;sample<sizeof(kNativeRaceOverMessage)/sizeof(kNativeRaceOverMessage[0]);++sample) {
            const auto& text=kNativeRaceOverMessage[sample];
            const auto& backing=kNativeRaceOverBacking[sample];
            RaceHudFrame frame;frame.atlas_height=atlas.height;frame.quads=40;
            build_race_over_frame(atlas,double(text.tick)/1200,width,height,frame);
            check(frame.quads==10,"finish presentation retires racing HUD and map before the complete title");
            if(frame.quads<10)continue;
            const auto first=frame.quads-10;
            for(unsigned part=0;part<2;++part) {
                const auto& native=part?text:backing;
                const auto actual=bounds(frame,first+(part?1:0),part?frame.quads:first+1);
                const std::array<float,4> expected{{left+native.left*scale,left+native.right*scale,
                                                  top+native.top*scale,top+native.bottom*scale}};
                float worst=0;for(unsigned i=0;i<4;++i)worst=std::max(worst,std::abs(actual[i]-expected[i]));
                const float actual_width=actual[1]-actual[0],expected_width=expected[1]-expected[0];
                if(worst>.025F || std::abs(actual_width-expected_width)>.025F) {
                    ++failures;std::fprintf(stderr,"FAIL: native Race Over %s tick=%u size=%ux%u max_px=%.6f width=%.6f expected_width=%.6f actual=%.5f,%.5f,%.5f,%.5f\n",
                        part?"font":"backing",text.tick,width,height,worst,actual_width,expected_width,actual[0],actual[1],actual[2],actual[3]);
                }
            }
            const auto before=frame;
            RaceFpsHistory history;for(unsigned i=0;i<90;++i)history.add(1000./60);
            append_race_fps_overlay(atlas,history,width,height,frame);
            check(frame.quads>before.quads,"optional FPS appends after complete finish title");
            for(unsigned q=0;q<before.quads;++q)for(unsigned v=0;v<4;++v) {
                const auto& a=before.vertices[q*4+v];const auto& b=frame.vertices[q*4+v];
                check(a.px==b.px&&a.py==b.py&&a.u==b.u&&a.v==b.v&&std::equal(a.color,a.color+4,b.color),
                      "FPS on/off preserves finish coordinates, texture and color");
            }
            if(!output.empty()&&width==1280&&height==720)for(unsigned fps=0;fps<2;++fps) {
                std::vector<unsigned char> pixels(std::size_t(width)*height*4,64);
                composite_race_hud(atlas,fps?frame:before,pixels.data(),width,height,width*4);
                FrameWriteResult written;
                const auto name="finish-"+std::to_string(text.tick)+"-fps"+std::to_string(fps)+".tga";
                check(write_rgba_tga(width,height,pixels,output/name,{},written),"fresh finish capture written");
            }
        }
    }
    RaceHudAtlas drag_atlas,drift_atlas;
    if(!load_race_hud_atlas(root,drag_atlas,error,false,false,false)||
       !load_race_drag_hud(root,"240SX",stock_240sx_handling(),drag_atlas,error)||
       !load_race_hud_atlas(root,drift_atlas,error,false,false,true,1301)) {
        std::fprintf(stderr,"FAIL: mode HUD atlas %s\n",error.c_str());return 1;
    }
    for(const auto& row:kNativeFinishMessages) {
        RaceRacerTiming result;result.status=static_cast<RacerStatus>(row.status);
        result.position=int(row.place);result.finish_ticks=row.ticks;result.finish_valid=row.status==1;
        const auto message=race_over_message(result,row.drag,drag_atlas);
        if(message!=row.message) {
            ++failures;std::fprintf(stderr,"FAIL: original message status=%u place=%u drag=%u\n",row.status,row.place,row.drag);
        }
    }
    for(const auto& row:kNativeFinishLoading)
        check(race_finish_loading_ready(row.ticks,row.resources_ready)==row.advance,"original loading stays until phase minimum AND resources ready");
    for(const auto& row:kNativeRaceOverModes)for(const auto viewport:{std::array<unsigned,2>{{640,480}},{{1280,720}},{{1920,1080}},{{1280,1024}}}) {
        const bool split=row.mode==3||row.mode==4;
        const auto& mode_atlas=row.mode==1||(split&&std::string_view(row.message)!="Race Over")?drag_atlas:row.mode==2?drift_atlas:atlas;
        const auto width=viewport[0],height=viewport[1]/(split?2:1);
        const auto layout=static_cast<RaceOverLayout>(row.mode);
        RaceHudFrame frame;frame.atlas_height=mode_atlas.height;
        append_race_over(mode_atlas,double(row.tick)/1200,width,height,frame,row.message,layout);
        const auto actual=bounds(frame,row.text?1:0,row.text?frame.quads:1);
        const float scale=std::min(width/640.F,viewport[1]/480.F);
        const float x=(width-640*scale)*.5F,y=(viewport[1]-480*scale)*.5F-(row.mode==4?float(height):0.F);
        const std::array<float,4> expected{{x+row.left*scale,x+row.right*scale,y+row.top*scale,y+row.bottom*scale}};
        float worst=0;for(unsigned i=0;i<4;++i)worst=std::max(worst,std::abs(actual[i]-expected[i]));
        if(worst>.025F) {
            ++failures;std::fprintf(stderr,"FAIL: mode Race Over mode=%u text=%u tick=%u size=%ux%u max_px=%.6f message=%s\n",row.mode,row.text,row.tick,width,height,worst,row.message);
            if(row.tick==1200&&width==640)std::fprintf(stderr,"BOUNDS actual=%.5f,%.5f,%.5f,%.5f expected=%.5f,%.5f,%.5f,%.5f\n",
                actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);
        }
        check(unsigned(frame.vertices[row.text?4:0].color[3]/2)==row.alpha,"original mode-specific reveal alpha");
    }
    for(const auto& row:kNativeRaceOverClock) {
        RaceOverPhase clock;check(clock.observe(true)==0,"original terminal Animate restart begins at zero");
        for(unsigned n=0;n<row.frames;++n)clock.advance(n&1?row.second_seconds:row.first_seconds);
        check(clock.ticks()==row.ticks,"runtime Race Over uses original per-frame integer UI ticks");
        RaceHudFrame actual_frame;
        build_race_over_frame(atlas,clock.observe(true),1280,720,actual_frame,"Race Over");
        const auto actual=bounds(actual_frame,row.text?1:0,row.text?actual_frame.quads:1);
        const std::array<float,4> expected{{160+row.left*1.5F,160+row.right*1.5F,row.top*1.5F,row.bottom*1.5F}};
        for(unsigned n=0;n<4;++n)check(std::abs(actual[n]-expected[n])<.025F,"runtime clock-driven Race Over geometry matches original frame schedule");
        check(unsigned(actual_frame.vertices[row.text?4:0].color[3]/2)==row.alpha,"runtime clock-driven original reveal alpha");
        const auto before=clock.ticks();clock.observe(true);clock.observe(true);
        check(clock.ticks()==before,"re-recording HUD cannot advance its native script age");
    }
    {
        RaceLoadingArt loading;
        if(!loading.load(root,error)||!loading.prepare_overlay(error)) {
            std::fprintf(stderr,"FAIL: finish loading %s\n",error.c_str());return 1;
        }
        const auto& image=loading.atlas();
        check(image.width<=4096&&image.height<=4096&&image.rgba.size()==std::size_t(image.width)*image.height*4,
              "actual loading atlas obeys renderer dimensions and byte contract");
        check(image.rgba.size()<=kMaxUploadResourceBytes,"actual loading image fits a GPU resource");
        FrontendUiFrame initial,later;
        loading.record(1280,720,0,initial);loading.record(1280,720,.4,later);
        check(initial.vertices.size()==16&&initial.additive.size()==4,"GPU loading records black cover and three original sprites");
        check(initial.vertices[0].px==0&&initial.vertices[0].py==0&&initial.vertices[2].px==1280&&initial.vertices[2].py==720,
              "loading covers the complete race viewport before any scene preview");
        check(initial.vertices[12].px!=later.vertices[12].px,"original loading activity advances independently of resource readiness");
        for(const auto& v:later.vertices)check(std::isfinite(v.px)&&std::isfinite(v.py)&&v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1,
              "loading GPU coordinates and atlas UVs are finite and bounded");
        std::printf("LOADING_ATLAS width=%u height=%u bytes=%zu quads=%zu\n",image.width,image.height,image.rgba.size(),initial.additive.size());
    }
    std::printf("finish HUD native geometry and lifecycle: %u failures\n",failures);
    return failures?1:0;
}
