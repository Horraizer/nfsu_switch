#pragma once
#include "nfsu/input.h"
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <stdexcept>
namespace nfsu {
// Diagnostic only: bounded RAM capture, no I/O in the measured loop.
struct AbInput {
    std::uint64_t time=0, sample=0, display=0;
    GamepadState pad{};
    bool clock=false;
    static AbInput pack(const GamepadFrame& f) {
        for (unsigned i=1;i<kMaxGamepads;++i)
            if(f.pads[i].connected) throw std::runtime_error("AB supports pad zero only");
        return {f.time_us,f.sample_time_us,f.display_frame,f.pads[0],f.sample_clock_available};
    }
    GamepadFrame unpack() const {
        GamepadFrame f{}; f.time_us=time;f.sample_time_us=sample;
        f.display_frame=display;f.pads[0]=pad;f.sample_clock_available=clock;return f;
    }
};
struct AbFrame {
    unsigned count=0;
    std::array<AbInput,8> inputs{};
    std::string state;
    std::uint64_t camera=0;
};
struct AbRoute {
    static constexpr std::size_t limit=6000;
    std::string context;
    std::vector<AbFrame> frames;
    AbRoute()=default;
    void save(std::ostream& out) const {
        out<<"NFSU_AB_ROUTE_1 "<<std::quoted(context)<<' '<<frames.size()<<'\n';
        for(const auto& f:frames){
            out<<f.count<<' '<<std::quoted(f.state)<<' '<<f.camera<<'\n';
            for(unsigned i=0;i<f.count;++i){const auto& a=f.inputs[i];
                out<<a.time<<' '<<a.sample<<' '<<a.display<<' '<<a.clock<<' '<<a.pad.connected<<' '<<a.pad.buttons;
                for(auto axis:a.pad.axes)out<<' '<<axis;
                out<<'\n';
            }
        }
    }
    bool load(std::istream& in,const std::string& expected) {
        AbRoute candidate;std::string magic;std::size_t count=0;
        if(!(in>>magic>>std::quoted(candidate.context)>>count) || magic!="NFSU_AB_ROUTE_1" ||
           candidate.context!=expected || count==0 || count>limit)return false;
        for(std::size_t n=0;n<count;++n){AbFrame f;
            if(!(in>>f.count>>std::quoted(f.state)>>f.camera)||f.count>8||f.state.size()>100)return false;
            for(unsigned i=0;i<f.count;++i){auto& a=f.inputs[i];unsigned clock=0,connected=0;
                if(!(in>>a.time>>a.sample>>a.display>>clock>>connected>>a.pad.buttons)||clock>1||connected>1)return false;
                a.clock=clock!=0;a.pad.connected=connected!=0;
                for(auto& axis:a.pad.axes)if(!(in>>axis)||axis < -32768 || axis>32767)return false;
            }
            candidate.frames.push_back(f);
        }
        in>>std::ws;if(!in.eof())return false;
        *this=std::move(candidate);return true;
    }
};
inline std::uint64_t ab_hash(const void* data,std::size_t bytes,std::uint64_t hash=14695981039346656037ULL){
    const auto* p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<bytes;++i){hash^=p[i];hash*=1099511628211ULL;}return hash;
}
struct AbCpuRow {
    std::uint64_t frame=0,tick=0,triangles=0;
    unsigned ticks=0,track=0,car=0,glow=0,active=0;
    std::array<double,14> ms{};
};
} // namespace nfsu
