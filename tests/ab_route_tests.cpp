#include "nfsu/ab_route.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace nfsu;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){
 try {
    AbRoute route;route.context="1:SKYLINE:recovered1:1:540";
    for(unsigned frame=0;frame<180;++frame){
        AbFrame f;f.count=frame%9;f.state=std::to_string(frame)+" sha\n";f.camera=frame*987654321ULL;
        for(unsigned i=0;i<f.count;++i){GamepadFrame in;
            in.time_us=(frame*8+i)*4167;in.sample_time_us=998877665544ULL+frame*16667;
            in.display_frame=frame+1;in.sample_clock_available=true;in.pads[0].connected=true;
            in.pads[0].buttons=(frame+i)&0xffff;in.pads[0].axes[0]=-32768;in.pads[0].axes[1]=32767;
            in.pads[0].axes[2]=int(frame)-90;f.inputs[i]=AbInput::pack(in);
        }route.frames.push_back(f);
    }
    std::stringstream saved;route.save(saved);const auto text=saved.str();
    AbRoute loaded;check(loaded.load(saved,route.context),"round trip refused");
    std::stringstream again;loaded.save(again);check(again.str()==text,"round trip changed data");
    for(unsigned f=0;f<180;++f)for(unsigned i=0;i<route.frames[f].count;++i){
        const auto a=route.frames[f].inputs[i].unpack(),b=loaded.frames[f].inputs[i].unpack();
        check(a.time_us==b.time_us && a.sample_time_us==b.sample_time_us && a.display_frame==b.display_frame &&
              a.sample_clock_available==b.sample_clock_available && a.pads[0].buttons==b.pads[0].buttons,"input clocks/buttons differ");
        for(unsigned j=0;j<4;++j)check(a.pads[0].axes[j]==b.pads[0].axes[j],"axis changed");
    }
    for(const auto& bad:std::vector<std::string>{text.substr(0,text.size()/2),text+"junk","NFSU_AB_ROUTE_1 \"x\" 6001\n",
        "NFSU_AB_ROUTE_1 \"x\" 1\n9 \"state\" 0\n","NFSU_AB_ROUTE_1 \"x\" 1\n1 \"state\" 0\n1 2 3 1 1 0 99999 0 0 0\n"}){
        std::stringstream in(bad);check(!loaded.load(in,"x"),"malformed accepted");
        check(loaded.frames.size()==180 && loaded.context==route.context,"failure changed old route");
    }
    std::stringstream truncated(text.substr(0,text.size()/2));check(!loaded.load(truncated,route.context),"truncated accepted");
    std::stringstream trailing(text+"junk");check(!loaded.load(trailing,route.context),"trailing accepted");
    std::stringstream wrong(text);check(!loaded.load(wrong,"wrong"),"wrong scene accepted");
    GamepadFrame extra;extra.pads[1].connected=true;bool refused=false;
    try{AbInput::pack(extra);}catch(const std::exception&){refused=true;}check(refused,"extra pad lost silently");
    std::cout<<"PASS AB route: 180 frames, clocks/buttons/axes preserved, malformed/truncated/context/multipad refusal transactional\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
