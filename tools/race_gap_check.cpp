#include "nfsu/race_leaderboard.h"
#include "nfsu/vehicle_track.h"
#include "nfsu/arg_path.h"
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    using namespace nfsu;
    const auto root=argument_path(argv[1]),out=argument_path(argv[2]);
    if(std::filesystem::exists(out)){std::cerr<<"FAIL existing output\n";return 1;}
    VehicleTrackCourse course;std::string error;
    if(!load_vehicle_track_course(root/"Tracks/TRACKB1003.LZC",2,0,0,0,course,error)){std::cerr<<"FAIL "<<error;return 1;}
    std::vector<unsigned char> bytes;const auto track=load_race_route_track(root/"Tracks/TRACKB1003.LZC",bytes);
    RacingLineTrack lines;if(!track.ok() || !parse_racing_line_image(bytes.data(),bytes.size(),{},lines))return 1;
    std::ofstream f(out);f<<std::setprecision(10);
    f<<"finish "<<course.route.boundaries[course.route.finish].percent<<"\n";
    for(unsigned category=0;category<4;++category) {
        RaceGapMap map;std::vector<RaceGapSegment> segments;
        if(!map.build(course.track,course.route,lines,category,&segments))return 1;
        f<<"category "<<category<<" "<<segments.size()<<" "<<map.lap_seconds<<"\n";
        for(const auto& s:segments)f<<"segment "<<s.raw_from<<" "<<s.raw_to<<"\n";
        for(unsigned i=0;i<256;++i)f<<"bin "<<i<<" "<<map.rate[i]<<" "<<map.elapsed[i]<<"\n";
        const std::array<RaceGapActor,8> actors{{{.1F,false,0,0},{.5F,false,0,0},
            {.99F,false,0,0},{1.01F,false,1,0},{1.95F,false,1,0},
            {2,true,2,400000},{2,true,2,392000},{-.01F,false,0,0}}};
        for(const auto& row:actors)for(const auto& player:actors) {
            f<<"gap "<<row.progress<<" "<<row.finished<<" "<<row.lap<<" "<<row.finish_ticks
             <<" "<<player.progress<<" "<<player.finished<<" "<<player.lap<<" "<<player.finish_ticks
             <<" "<<410000<<" "<<race_gap_ticks(map,row,player,410000)<<"\n";
        }
        std::cout<<"PASS category "<<category<<" segments "<<segments.size()<<" lap seconds "<<map.lap_seconds<<"\n";
    }
    return f?0:1;
}
