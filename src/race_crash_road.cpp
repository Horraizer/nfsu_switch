#include "nfsu/race_crash_road.h"
#include "nfsu/intro_ai.h"
#include "nfsu/race_events.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/race_setup.h"
#include <cstring>
namespace nfsu {
class RaceCrashRoad::State {
public:
    const VehicleTrackCourse* course=nullptr;
    const IntroAiPlanner* planner=nullptr;
    RacingLineTrack lines;
    std::vector<AiRoadEvent> events;
    std::array<bool,128> reverse{};
    int direction=0,performance_category=0;
    bool prepare(const VehicleState& car,AiRoadChain& road,int& category)const {
        if(planner)return planner->camera_road(road,category);
        AiRoadContext context;context.category=performance_category;
        AiRoadContextInput input;input.selection.position={float(car.position.x),float(car.position.y),float(car.position.z)};
        TrackPathCoordinate coordinate;const float xy[]={input.selection.position[0],input.selection.position[1]};
        update_track_path_coordinate(course->route.spline,course->track,coordinate,xy);
        input.selection.progress=direction?course->route.spline.path_length-coordinate.distance:coordinate.distance;
        input.selection.role=1;input.selection.maximum_distance=999999;input.selection.allow_outside_width=true;
        input.initial_horizon=input.retained_horizon=100;input.back_margin=24;
        AiRoadPolicyInput policy;policy.direction=direction;policy.role=1;policy.decision_start=0;policy.probabilities={0,0,1};
        AiRoadPolicy decisions;std::uint32_t rng=0x60000000U;AiRoadContextResult result;
        if(!ai_road_update_context(lines,events,reverse,input,policy,decisions,rng,context,result)||!context.selected)return false;
        road=context.chain;category=context.category;return true;
    }
};
RaceCrashRoad::RaceCrashRoad()=default;
RaceCrashRoad::~RaceCrashRoad()=default;
bool RaceCrashRoad::load(const std::filesystem::path& root,unsigned track,int direction,
    const VehicleTrackCourse& course,const VehicleHandling& handling,const IntroAiPlanner* planner,std::string& error) {
    if(direction<0||direction>1){error="invalid camera road direction";return false;}
    auto next=std::make_unique<State>();next->course=&course;next->planner=planner;next->direction=direction;
    float rating=0,slip=0;
    if(!ai_setup_performance(handling,rating,slip)){error="camera actor performance unavailable";return false;}
    next->performance_category=int(race_performance_category(rating));
    if(!planner) {
        std::vector<unsigned char> image;
        const auto parsed=load_race_route_track(root/"Tracks"/track_bundle_file_name(static_cast<std::int16_t>(track)),image);
        if(!parsed.ok()||!parse_racing_line_image(image.data(),image.size(),{},next->lines)) {error="camera roads unavailable";return false;}
        bool found=false;
        for(std::size_t p=0;p+8<=image.size();) {
            std::uint32_t id=0,size=0;std::memcpy(&id,image.data()+p,4);std::memcpy(&size,image.data()+p+4,4);p+=8;
            if(size>image.size()-p){error="camera road chunk truncated";return false;}
            if(id==0x34122) {
                if(found||!ai_road_events(image.data()+p,size,next->events)){error="camera road connections invalid";return false;}
                found=true;
            }
            p+=size;
        }
        if(!found||!ai_road_initialize(parsed,next->lines,next->events,error)||!ai_road_orient(next->lines,direction))return false;
        for(const auto& route:next->lines.routes)next->reverse[unsigned(route.index)]=route.reverse;
    }
    state_=std::move(next);error.clear();return true;
}
bool RaceCrashRoad::prepare(const VehicleState& car,float,AiRoadChain& road,int& category)const {
    return state_&&state_->prepare(car,road,category);
}
bool RaceCrashRoad::progress_at(const AiDriverPoint& point,float& progress)const {
    if(!state_)return false;
    TrackPathCoordinate c;update_track_path_coordinate(state_->course->route.spline,state_->course->track,c,point.data());
    progress=c.fraction*100.F;return c.node>=0;
}
}
