#include "nfsu/start_crowd.h"
#include "nfsu/race_events.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/drive_session.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/track_ground.h"
#include "../tests/race_start_native.inc"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
namespace {
class CrowdIoFiles final:public nfsu::FileSystemService {
public:
    std::size_t stream_reads=0,tiny_stream_reads=0;
    bool argument_to_path(const char* value,std::filesystem::path& path,std::string& error)override {
        return base().argument_to_path(value,path,error);
    }
    bool list_directory(const std::filesystem::path& path,std::size_t limit,std::vector<std::string>& names,std::string& error)override {
        return base().list_directory(path,limit,names,error);
    }
    bool file_size(const std::filesystem::path& path,std::uint64_t& bytes,std::string& error)override {
        return base().file_size(path,bytes,error);
    }
    bool read_file(const std::filesystem::path& path,std::uint64_t offset,unsigned char* data,std::size_t count,std::size_t& read,std::string& error)override {
        if(path.filename()=="STREAML1RA.BUN"){++stream_reads;tiny_stream_reads+=count<=20;}
        return base().read_file(path,offset,data,count,read,error);
    }
private:
    static nfsu::FileSystemService& base(){return nfsu::posix_file_system();}
};
}
std::uint64_t crowd_pose_hash(const nfsu::RecordedCommands& commands) {
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto& r:commands.resource_table().resources)if(r.streamed_vertices)
        for(std::size_t i=0;i<r.bytes;++i)hash=(hash^r.data[i])*1099511628211ULL;
    return hash;
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    CrowdIoFiles files;nfsu::StartCrowd crowd;std::string error;
    if(!crowd.load(files,std::filesystem::u8path(argv[1]),error)) {std::cerr<<"FAIL: "<<error<<"\n";return 1;}
    if(files.tiny_stream_reads || files.stream_reads>2048) {
        std::cerr<<"FAIL: crowd metadata reopened stream for tiny headers\n";return 1;
    }
    const auto& commands=crowd.commands();
    std::size_t resource=0;
    for(const auto& r:commands.resource_table().resources) {++resource;if(r.kind==nfsu::UploadResourceKind::vertex_buffer) {
        const auto* vertices=reinterpret_cast<const nfsu::MeshVertex*>(r.data);
        for(std::size_t i=0;i<r.elements;++i) {
            const auto& v=vertices[i];
            if(!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) ||
                std::abs(v.px)>5 || std::abs(v.pz)>5 || v.py < -1 || v.py>4) {
                std::cerr<<"FAIL: resource "<<resource-1<<" posed crowd vertex "<<i<<" at "<<v.px<<","<<v.py<<","<<v.pz<<"\n";return 1;
            }
        }
    }}
    nfsu::VehicleTrackCourse course;
    if(!nfsu::load_vehicle_track_course(std::filesystem::u8path(argv[1])/"Tracks/TRACKB1003.LZC",2,0,0,0,course,error)) {
        std::cerr<<"FAIL: course "<<error<<"\n";return 1;
    }
    nfsu::RaceSession session(course);
    if(!session.start(error)) {std::cerr<<"FAIL: start "<<error<<"\n";return 1;}
    crowd.frame(session);const auto initial=crowd_pose_hash(crowd.commands());
    if(!crowd.draw_count()) {std::cerr<<"FAIL: crowd missing before countdown\n";return 1;}
    const auto* stable=commands.resource_table().resources[0].data;
    const auto original_resources=commands.resource_table().resources;
    const auto stable_streams=[&]() {
        const auto& current=commands.resource_table().resources;
        if(current.size()!=original_resources.size())return false;
        bool streamed=false;
        for(std::size_t i=0;i<current.size();++i)if(original_resources[i].streamed_vertices) {
            streamed=true;
            if(!current[i].streamed_vertices||current[i].data!=original_resources[i].data||
               current[i].bytes!=original_resources[i].bytes)return false;
        }
        return streamed;
    };
    if(!stable_streams()){std::cerr<<"FAIL: no stable streamed pose buffers\n";return 1;}
    bool moved=false;unsigned early_changes=0;auto previous=initial;
    for(unsigned tick=0;tick<600;++tick) {
        auto pad=nfsu::race_input_frame({1,0,0},tick);
        if(!session.advance(pad,error)) {std::cerr<<"FAIL: advance "<<error<<"\n";return 1;}
        crowd.frame(session);
        const auto& timing=session.timing();
        const bool expected=!timing.go_valid ||
            (std::int64_t(timing.now_ticks)-timing.go_ticks)*30<140LL*nfsu::kRaceTicksPerSecond;
        if((crowd.draw_count()!=0)!=expected) {std::cerr<<"FAIL: crowd scene-end lifecycle\n";return 1;}
        const auto sample=crowd_pose_hash(crowd.commands());
        if(!session.timing().go_valid)moved|=sample!=initial;
        if(tick<4&&sample!=previous)++early_changes;
        previous=sample;
        crowd.frame(session);
        if(sample!=crowd_pose_hash(crowd.commands()) || stable!=commands.resource_table().resources[0].data || !stable_streams()) {
            std::cerr<<"FAIL: paused sample or upload changed\n";return 1;
        }
    }
    if(!moved || early_changes<3 || !session.timing().go_valid || crowd.draw_count()) {std::cerr<<"FAIL: no animation or GO\n";return 1;}
    nfsu::RaceSession restarted(course);
    if(!restarted.start(error)) {std::cerr<<"FAIL: restart "<<error<<"\n";return 1;}
    crowd.frame(restarted);
    if(!crowd.draw_count() || crowd_pose_hash(crowd.commands())!=initial || !stable_streams()) {std::cerr<<"FAIL: restart crowd\n";return 1;}
    // Production road camera path, both marker directions, every grid family.
    for(const auto& row:kNativeStartPlaylist)
        if(nfsu::road_start_playlist(row[0],row[1])!=row[2]){std::cerr<<"FAIL: native playlist family\n";return 1;}
    for(const auto& row:kNativeStartLens)
        if(2U*nfsu::player_velocity_angle(2*float(row[0]),36.0099983215332F)!=row[1]){std::cerr<<"FAIL: native authored lens\n";return 1;}
    for(unsigned grid=1;grid<=4;++grid)for(unsigned variant=0;variant<3;++variant) {
        unsigned selected=0;nfsu::StartCrowd camera;nfsu::RaceStartClock clock;
        if(!nfsu::select_road_start_scene(files,std::filesystem::u8path(argv[1]),4,grid,variant,selected,error) ||
           !camera.load_camera(files,std::filesystem::u8path(argv[1]),selected,"L1RA",clock,error)) {
            std::cerr<<"FAIL: road camera grid="<<grid<<" variant="<<variant<<": "<<error<<"\n";return 1;
        }
        if(camera.bytes() || camera.actor_count() || !camera.commands().resource_table().resources.empty()) {std::cerr<<"FAIL: camera adds GPU resources\n";return 1;}
        for(unsigned reverse=0;reverse<2;++reverse) {
            // Production creates a fresh scene for each course/direction.
            // Reload here too, so the cached forward anchor cannot mask a
            // missing BackwardStartLine binding in the reverse witness.
            if(reverse && !camera.load_camera(files,std::filesystem::u8path(argv[1]),selected,"L1RA",clock,error)) {
                std::cerr<<"FAIL: reverse camera reload "<<error<<"\n";return 1;
            }
            nfsu::VehicleTrackCourse road;
            if(!nfsu::load_vehicle_track_course(std::filesystem::u8path(argv[1])/"Tracks/TRACKB1003.LZC",2,int(reverse),0,0,road,error)) {std::cerr<<"FAIL: camera road "<<error<<"\n";return 1;}
            nfsu::RaceSession race(road);
            const float countdown=float((double(clock.go)/30-double(clock.release)/30)*double(1.F/3.F));
            if(!race.start(error,nullptr,countdown,float(double(clock.release)/30))) {std::cerr<<"FAIL: camera start "<<error<<"\n";return 1;}
            nfsu::OrbitCamera first;bool changed=false;
            const unsigned release_ticks=unsigned(std::ceil(double(float(double(clock.release)/30))*nfsu::kRaceTicksPerSecond));
            for(unsigned tick=0;tick<release_ticks/67U+2;++tick) {
                camera.frame(race);nfsu::OrbitCamera view;
                if(!nfsu::drive_render_camera(race.camera(),nfsu::ChaseFovMode::retail,1280,720,view,error)) {std::cerr<<"FAIL: projection "<<error<<"\n";return 1;}
                const bool authored=camera.camera(race,view);
                if(authored!=race.start_cinematic() || !nfsu::validate_camera(view,error)) {std::cerr<<"FAIL: camera lifecycle "<<error<<"\n";return 1;}
                if(tick==0)first=view;
                else changed|=std::abs(first.target.x-view.target.x)+std::abs(first.target.z-view.target.z)>0.01;
                if(!race.advance(nfsu::race_input_frame({0,0,0},tick),error)) {std::cerr<<"FAIL: road advance "<<error<<"\n";return 1;}
            }
            if(!changed || race.start_cinematic()){std::cerr<<"FAIL: camera motion or release\n";return 1;}
            std::cout<<"PASS: road camera scene="<<selected<<" grid="<<grid<<" reverse="<<reverse<<" release="<<clock.release<<"\n";
        }
    }
    for(unsigned track:{1003U,1002U,1102U})for(unsigned variant=0;variant<3;++variant) {
        nfsu::StartCrowd road;unsigned selected=0;
        if(!nfsu::select_road_start_scene(files,std::filesystem::u8path(argv[1]),4,1,variant,selected,error) ||
           !road.load(files,std::filesystem::u8path(argv[1]),error,selected,track,"L1RA")) {
            std::cerr<<"FAIL: full road crowd track="<<track<<" scene="<<selected<<" "<<error<<"\n";return 1;
        }
        if(!road.actor_count() || road.bytes()>48U*1024U*1024U) {std::cerr<<"FAIL: full road crowd missing/oversize\n";return 1;}
        std::cout<<"PASS: full road crowd track="<<track<<" scene="<<selected<<" actors="<<road.actor_count()<<" bytes="<<road.bytes()<<"\n";
    }
    // Exercise the same lazy anchor path as production for every campaign
    // time trial. The race-only checks do not own the start presentation.
    const auto retail=std::filesystem::u8path(argv[1]);
    for(unsigned reverse=0;reverse<2;++reverse) {
        nfsu::VehicleTrackCourse road;
        if(!nfsu::load_vehicle_track_course(retail/"Tracks/TRACKB1003.LZC",2,int(reverse),0,0,road,error)) {
            std::cerr<<"FAIL: finish scene course "<<error<<"\n";return 1;
        }
        nfsu::RaceSession race(road);
        if(!race.start(error)){std::cerr<<"FAIL: finish scene vehicle "<<error<<"\n";return 1;}
        for(unsigned place:{1U,4U})for(unsigned variant=0;variant<3;++variant) {
            nfsu::StartCrowd ending;
            unsigned id=0;
            if(!nfsu::select_race_finish_scene(files,retail,4,place,1003,variant,id,error)||
               id!=2320+2*variant+(place!=1)||
               !ending.load_finish_camera(files,retail,id,"L1RA",road,error)) {
                std::cerr<<"FAIL: ending scene "<<id<<" place="<<place<<" "<<error<<"\n";return 1;
            }
            nfsu::OrbitCamera first,later;
            if(!ending.finish_camera(0,first,1280,720)||!ending.finish_camera(1,later,1280,720)||
               !nfsu::validate_camera(first,error)||!nfsu::validate_camera(later,error)||
               std::abs(first.target.x-later.target.x)+std::abs(first.target.z-later.target.z)<.01) {
                std::cerr<<"FAIL: ending camera motion "<<id<<" "<<error<<"\n";return 1;
            }
            for(unsigned slot=0;slot<4;++slot) {
                auto car=race.car();auto animation=race.animation();
                if(!ending.finish_car(slot,1,road,race.spec(),car,animation)||
                   !std::isfinite(car.position.z)||
                   std::hypot(car.position.x-road.start.ground.x,car.position.y-road.start.ground.y)>100) {
                    std::cerr<<"FAIL: ending car pose "<<id<<" slot="<<slot<<"\n";return 1;
                }
            }
            std::cout<<"PASS: ending scene "<<id<<" place="<<place<<" reverse="<<reverse<<"\n";
        }
    }
    for(unsigned event:{3U,65U}) {
        nfsu::SupportedRaceSelection selection;
        nfsu::VehicleTrackCourse road;
        if(!nfsu::supported_race_selection(event,0,selection)||
           !nfsu::load_vehicle_track_course(retail/"Tracks"/("TRACKB"+std::to_string(selection.slot.track)+".LZC"),
               selection.slot.laps,selection.slot.direction,selection.lap_skip,0,road,error)) {
            std::cerr<<"FAIL: ending car test course "<<error<<"\n";return 1;
        }
        nfsu::RaceSession race(road);
        nfsu::StartCrowd ending;
        if(!race.start(error)||!ending.load_finish_camera(files,retail,2326,selection.region,road,error)) {
            std::cerr<<"FAIL: solo loss pose setup "<<error<<"\n";return 1;
        }
        auto car=race.car();auto animation=race.animation();
        if(!ending.finish_car(0,5,road,race.spec(),car,animation)) {
            std::cerr<<"FAIL: solo loss pose unavailable\n";return 1;
        }
        const auto& q=car.orientation;
        if(event==65&&std::abs(q.x)+std::abs(q.y)<.01) {
            std::cerr<<"FAIL: solo loss pose did not follow sloped road\n";return 1;
        }
        const auto rotate=[&](const nfsu::VehicleVec3& v) {
            const nfsu::VehicleVec3 a{q.y*v.z-q.z*v.y,q.z*v.x-q.x*v.z,q.x*v.y-q.y*v.x};
            const nfsu::VehicleVec3 b{q.y*a.z-q.z*a.y,q.z*a.x-q.x*a.z,q.x*a.y-q.y*a.x};
            return nfsu::VehicleVec3{v.x+2*(q.w*a.x+b.x),v.y+2*(q.w*a.y+b.y),v.z+2*(q.w*a.z+b.z)};
        };
        const auto& handling=race.spec().handling;
        for(const auto& wheel:handling.wheels) {
            const auto offset=rotate({wheel.longitudinal-handling.centre_of_mass[0],
                wheel.lateral-handling.centre_of_mass[1],wheel.vertical-handling.centre_of_mass[2]-wheel.radius});
            const nfsu::VehicleVec3 bottom{car.position.x+offset.x,car.position.y+offset.y,car.position.z+offset.z};
            nfsu::TrackGroundCache cache;nfsu::TrackContact hit;
            if(!nfsu::query_track_ground(road.world,bottom,cache,hit)||std::abs(bottom.z-hit.point[2])>.10) {
                std::cerr<<"FAIL: solo loss wheel ground clearance event="<<event<<" z="<<bottom.z<<" ground="<<hit.point[2]<<"\n";return 1;
            }
        }
        std::cout<<"PASS: solo loss car rests on the road event="<<event<<"\n";
    }
    std::vector<unsigned char> event_bytes;
    const auto events=nfsu::load_race_event_table(retail/"FrontEnd/FrontB.lzc",event_bytes);
    if(!events.ok()){std::cerr<<"FAIL: challenge catalog\n";return 1;}
    for(unsigned event:{7U,15U,26U,47U,68U,77U,97U}) {
        const auto* entry=nfsu::find_race_event(events,event);
        if(!entry||entry->races.size()!=1){std::cerr<<"FAIL: challenge event absent\n";return 1;}
        const auto& slot=entry->races[0];nfsu::VehicleTrackCourse road;
        if(!nfsu::load_vehicle_track_course(retail/"Tracks"/("TRACKB"+std::to_string(slot.track)+".LZC"),
            slot.laps,slot.direction,1,0,road,error)){std::cerr<<"FAIL: challenge course "<<event<<" "<<error<<"\n";return 1;}
        unsigned scene=0;nfsu::StartCrowd camera;nfsu::RaceStartClock clock;
        if(!nfsu::select_road_start_scene(files,retail,1,1,3,scene,error)||
           !camera.load_camera(files,retail,scene,"L1RA",clock,error)){std::cerr<<"FAIL: challenge camera "<<error<<"\n";return 1;}
        nfsu::RaceSession race(road);
        if(!race.start(error,nullptr,1.F,float(double(clock.release)/30))){std::cerr<<"FAIL: challenge start "<<error<<"\n";return 1;}
        try {camera.frame(race);}catch(const std::exception& e){std::cerr<<"FAIL: challenge anchor event="<<event<<" track="<<slot.track<<" "<<e.what()<<"\n";return 1;}
        nfsu::OrbitCamera view;
        if(!camera.camera(race,view)||!nfsu::validate_camera(view,error)){std::cerr<<"FAIL: challenge view "<<event<<" "<<error<<"\n";return 1;}
        std::cout<<"PASS: challenge start event="<<event<<" track="<<slot.track<<" direction="<<int(slot.direction)<<" scene="<<scene<<"\n";
    }
    std::cout<<"PASS: fractional animation, stable streamed pointers, original post-GO disappearance and restart\n";
    std::cout<<"PASS: crowd actors="<<crowd.actor_count()<<" draws="<<commands.draw_count()
        <<" resources="<<commands.resource_table().resources.size()<<" bytes="<<crowd.bytes()<<"\n";
}
