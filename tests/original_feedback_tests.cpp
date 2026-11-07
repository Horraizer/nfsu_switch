#include "nfsu/arg_path.h"
#include "nfsu/race_drift.h"
#include "nfsu/race_hud.h"
#include "nfsu/wheel_paint.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include "original_feedback_native.inc"
namespace {
float real(unsigned word) {float value=0;std::memcpy(&value,&word,4);return value;}
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    using namespace nfsu;
    try {
        for(const auto& row:kNativeWheelPaint)
            require(wheel_paint_pixel(row[0],row[1],row[2])==row[3],"original wheel mask pixel differs");
        for(const auto& row:kNativeTurboAngles)
            require(race_hud_turbo_rotation(real(row[1]),row[0]!=0)==real(row[2]),"original turbo SAT angle differs");
        if(argc==2) {
            const auto root=argument_path(argv[1]);std::vector<unsigned char> bytes;
            unsigned event=0,difficulty=3,grade=4;RaceRouteTrack track;DriftCompetition competition;
            for(const auto& row:kNativeDriftScores) {
                if(event!=row[0]) {
                    event=row[0];difficulty=3;grade=4;
                    track=load_race_route_track(root/"Tracks"/(event==3?"TRACKB1301.lzc":"TRACKB1302.lzc"),bytes);
                    require(track.ok(),"retail drift zones missing");
                }
                if(difficulty!=row[1]||grade!=row[2]) {
                    difficulty=row[1];grade=row[2];std::string error;
                    if(!competition.load(root,event,difficulty,grade,grade==3?140.F:300.F,0x60000000U,error))throw std::runtime_error(error);
                }
                const auto& zone=track.zones.at(row[3]);
                float point[2]{};for(int i=0;i<zone.vertex_count;++i)for(unsigned k=0;k<2;++k)
                    point[k]+=zone.vertices[2*i+k]/float(zone.vertex_count);
                competition.update(track,false,point,int(row[4]),123.5F);
                for(const auto& standing:competition.standings()) {
                    const auto expected=standing.actor?real(row[4+standing.actor]):123.5F;
                    if(standing.score!=expected) {
                        std::cerr<<std::setprecision(9)<<"event="<<event<<" difficulty="<<difficulty<<" class="<<grade<<" lap="<<row[4]
                                 <<" actor="<<standing.actor<<" actual="<<standing.score<<" expected="<<expected<<'\n';
                        throw std::runtime_error("original drift standings score differs");
                    }
                }
            }
        }
        std::cout<<"PASS original wheel paint, turbo angle"<<(argc==2?" and authored drift awards":"")<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL feedback: "<<e.what()<<'\n';return 1;}
}
