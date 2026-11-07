#include "nfsu/race_leaderboard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
namespace nfsu {
bool RaceGapMap::build(const RaceRouteTrack& track,const RaceRoute& route,
    const RacingLineTrack& roads,unsigned category,std::vector<RaceGapSegment>* evidence) {
    if(category>=5 || route.finish<0)return false;
    const unsigned lane=category+(route.reverse?5U:0U);
    RaceGapMap next;
    const float finish=route.boundaries.at(std::size_t(route.finish)).percent;
    for(const auto& road:roads.routes) {
        if(road.kind!=RouteKind::racing || !road.category_enabled[lane])continue;
        TrackPathCoordinate coordinate;
        for(std::size_t i=0;i+1<road.points.size();++i) {
            //432F50 traverses point pairs in reverse for the reverse race.
            const auto index=route.reverse?road.points.size()-1-i:i;
            const auto& a=road.points[index];const auto& b=road.points[route.reverse?index-1:index+1];
            const float pa[2]={float(double(a.position[0])+double(a.forward[1])*a.driving_offset[lane]),
                               float(double(a.position[1])-double(a.forward[0])*a.driving_offset[lane])};
            const float pb[2]={float(double(b.position[0])+double(b.forward[1])*b.driving_offset[lane]),
                               float(double(b.position[1])-double(b.forward[0])*b.driving_offset[lane])};
            const auto progress=[&](const float* p) {
                update_track_path_coordinate(route.spline,track,coordinate,p);
                // 433047 stores percent before 434A10 returns the normalized x87 value.
                double v=double(float(coordinate.fraction*100.F))-finish;
                if(route.point_to_point) {if(v< -50)v+=100.F;else if(v>50)v-=100.F;}
                if(v<0)v+=100.F;
                return v*double(.01F);
            };
            float from=float(progress(pa));const float raw_from=coordinate.fraction;
            double to=progress(pb);const float raw_to=coordinate.fraction;
            if(to-from>.5F)from=float(from+1.F);else if(from-to>.5F)to+=1.F;
            const double dx=double(pa[0])-pb[0],dy=double(pa[1])-pb[1];
            const double length=std::sqrt(dx*dx+dy*dy);const float speed=float(a.speed[lane]);
            if(evidence)evidence->push_back({from,float(to),float(length),speed,raw_from,raw_to});
            const float rate=speed>0 ? float((to-from)/(length/speed)) : 0;
            const float start=std::max(0.F,from);
            const auto bin=unsigned((start-std::trunc(start))*256.F);
            if(bin>=256 || !std::isfinite(rate))return false;
            if(rate>0 && next.rate[bin]==0)next.rate[bin]=rate;
        }
    }
    // Missing bins retain the preceding rate, initially original 1/180.
    double sum=0;float previous=0.0055555556900799274F;
    for(unsigned i=0;i<256;++i) {
        next.elapsed[i]=float(sum);
        if(next.rate[i]==0)next.rate[i]=previous;
        previous=next.rate[i];sum+=double(1.F/256)/previous;
    }
    next.lap_seconds=float(sum);next.valid=true;*this=next;return true;
}
float RaceGapMap::time(float progress) const noexcept {
    const float p=std::max(0.F,progress);const int lap=int(p);
    const float fraction=p-float(lap);const auto bin=std::min(255U,unsigned(fraction*256.F));
    return float((double(fraction)-double(bin)/256)/rate[bin]+double(lap)*lap_seconds+elapsed[bin]);
}
std::int32_t race_gap_ticks(const RaceGapMap& map,const RaceGapActor& row,
                           const RaceGapActor& player,std::int32_t since_go) noexcept {
    if(!map.valid)return 0;
    if(row.finished && player.finished)return player.finish_ticks-row.finish_ticks;
    const auto rounded=[](double seconds){return std::int32_t(std::trunc(double(seconds)*4000+.5));};
    if(player.finished)return rounded(double(map.time(float(player.lap)))-map.time(row.progress))+since_go-player.finish_ticks;
    if(row.finished)return rounded(double(map.time(player.progress))-map.time(float(row.lap)))+row.finish_ticks-since_go;
    return rounded(double(map.time(player.progress))-map.time(row.progress));
}
std::string race_gap_text(std::int32_t ticks) {
    const std::int64_t magnitude=ticks<0?-std::int64_t(ticks):ticks;
    const auto hundredths=magnitude/40;const auto seconds=hundredths/100;
    char text[40];
    if(seconds<60)std::snprintf(text,sizeof text,"%c%4lld.%02lld",ticks<0?'-':'+',
        static_cast<long long>(seconds),static_cast<long long>(hundredths%100));
    else std::snprintf(text,sizeof text,"%c%lld:%02lld.%02lld",ticks<0?'-':'+',
        static_cast<long long>(seconds/60),static_cast<long long>(seconds%60),static_cast<long long>(hundredths%100));
    return text;
}
void RaceLeaderboardCycle::update(std::int32_t now) noexcept {
    if(!initialized || now<since){*this={};initialized=true;since=now;}
    shown=mode;
    const double seconds=double(std::int64_t(now)-since)*double(0.00025F);
    if(seconds>(mode==RaceLeaderboardMode::gaps?10:2)) {
        since=now;
        if(mode==RaceLeaderboardMode::gaps)mode=cars_next?RaceLeaderboardMode::cars:RaceLeaderboardMode::names;
        else {cars_next=mode==RaceLeaderboardMode::names;mode=RaceLeaderboardMode::gaps;}
    }
}
bool load_race_car_labels(const std::filesystem::path& root,const std::array<std::string,4>& names,
                          std::array<std::string,4>& output,std::string& error) {
    std::ifstream file(root/"Languages/English.bin",std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),{}};
    const auto u32=[&](std::size_t at)->std::uint32_t {return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);};
    if(bytes.size()<24 || u32(0)!=0x39000 || u32(4)>bytes.size()-8){error="leaderboard language header";return false;}
    const std::size_t count=u32(12),table=8+u32(16),strings=8+u32(20),end=8+u32(4);
    if(table>end || count>(end-table)/8 || strings>=end){error="leaderboard language table";return false;}
    std::array<std::string,4> next;
    for(unsigned n=0;n<4;++n) {
        std::uint32_t hash=0xffffffffU;for(unsigned char c:std::string("CAR_MODEL_NAME_")+names[n]+(names[n]=="TIBURON"?"_USA":""))hash=hash*33+c;
        for(std::size_t i=0;i<count;++i)if(u32(table+i*8)==hash) {
            const auto offset=u32(table+i*8+4);if(offset>=end-strings)break;
            const auto first=strings+offset;auto last=first;
            while(last<end && bytes[last] && last-first<64)++last;
            if(last==end || bytes[last])break;
            next[n].assign(reinterpret_cast<const char*>(bytes.data()+first),last-first);break;
        }
        if(next[n].empty()){error="leaderboard car label absent: "+names[n];return false;}
    }
    output=std::move(next);return true;
}
}
