#pragma once
#include "nfsu/race_limits.h"
#include "nfsu/ai_driver.h"
#include "nfsu/ai_road.h"
#include "nfsu/race_contacts.h"
#include "nfsu/drive_session.h"
#include "nfsu/traffic_motion.h"
#include "nfsu/ai_traffic_action.h"
#include <memory>
namespace nfsu {
struct AiPathActorSnapshot;
struct RaceTrafficSetup {VehicleSpec spec; VehicleHull hull; std::string name; unsigned car_type=20;};
struct RaceTrafficActionRecord {
    AiOwnedAction owned;
    AiTrafficAvoidanceState avoidance;
    AiTrafficStopAction stop;
    std::uint32_t epoch=0;
};
struct RaceTrafficActor {
    RaceTrafficSetup setup;
    VehicleState car;
    RaceContactPose previous_pose;
    bool previous_valid=false;
    CarAnimationState animation;
    RaceContactBox box;
    RaceTrackBodyBounds bounds;
    TrafficMotion motion;
    AiDriverForwardState driver;
    AiDriverPath path;
    AiRoadCursor cursor;
    AiRoadContext road;
    AiRoadPolicy road_policy;
    AiRoadLocation location;
    AiNeighborhood neighbors;
    AiPlannedPath published_path,driver_path;
    AiContactManeuver maneuver;
    AiTrafficActionOwner action_owner;
    std::array<RaceTrafficActionRecord,2> action_records{};
    AiActionSpeedState action_speed;
    TrackPathCoordinate coordinate;
    std::uint32_t next_action_handle=1;
    unsigned ride_category=0;
    bool actor_override_enabled=false;
    std::array<float,4> actor_override{};
    std::uint64_t registration=0;
    unsigned lane=0;
    std::uint16_t published_heading=0;
    bool active=false,ended=false,impacted=false,crash_stop=false;
    struct PairHistory {float magnitude=0;std::uint64_t last_tick=0;bool valid=false;};
    std::array<PairHistory,1+kRaceRivalCapacity+kRaceTrafficCapacity> pair_history{};
    float target_speed=0,body_height=0,progress=0,steering_scale=1;
    VehicleQuat initial_pitch;
    std::uint64_t activations=0,contacts=0;
};
// Fixed pool / world adapter. The ordinary mover and driver are recovered;
// scheduling, road admission and collision integration are documented separately.
// No allocations or asset loading in advance(). The course outlives this owner.
class RaceTraffic {
public:
    bool load(const std::filesystem::path&,const VehicleTrackCourse&,
              const std::vector<RaceTrafficSetup>&,std::string&,unsigned density=1,std::uint32_t* random=nullptr,bool drag=false);
    bool advance(const std::array<const VehicleState*,4>&,std::uint64_t,std::string&,std::int32_t world_clock=-1,float dt=1.F/60.F,
                 const std::array<bool,4>& ghosts={},
                 const std::array<const RaceContactBox*,4>& racer_shapes={});
    bool stage_grid(const std::array<const VehicleState*,4>&,
                         const std::array<const RaceContactBox*,4>&,
                         float spacing,float clearance,std::int32_t clock,std::string&);
    //420B40 prepares every controller/driver before420310 integrates bodies.
    bool prepare_frame(const std::array<const VehicleState*,4>&,std::uint64_t,std::string&,
                       std::int32_t,float,const std::array<bool,4>&,
                       const std::array<const RaceContactBox*,4>&,bool grid_held=false);
    bool advance_body(unsigned,std::string&);
    void finish_frame();
    // Original583E80 selects the rigid mover on contact.583DA0 separately
    // stops traffic only when a retained contact magnitude exceeds30.
    static void contact(RaceTrafficActor&,unsigned,const ContactVector&,std::uint64_t,const ContactRecord* record=nullptr);
    bool continue_contact(unsigned,double,std::string&,const VehicleState* published_actor=nullptr,bool refreshed=false);
    bool prepare_contact(unsigned,std::string&,bool& activated);
    bool refresh_contact(unsigned,VehicleState&,std::string&,const VehicleState*,float interval);
    float last_component_interval=-1;
    void defer_body_contacts(bool);
    bool world_prediction(unsigned,const AiPathActorSnapshot&,std::uint32_t,float,AiPlannedPath&)const;

    const std::vector<RaceTrafficActor>& actors() const {return actors_;}
    std::vector<RaceTrafficActor>& contact_actors() {return actors_;}
    std::uint64_t decisions=0,spawns=0,retirements=0;
private:
    struct ActionContext;
    RacingLineTrack lines_,backward_lines_;
    std::array<std::array<bool,128>,2> route_reverse_{};
    std::vector<AiTrafficIntersection> intersections_;
    std::array<AiNeighborActor,4+kRaceTrafficCapacity> world_nodes_{};
    std::array<TrackPathCoordinate,4> racer_coordinates_{};
    std::array<const VehicleState*,4> racers_{};
    std::array<const RaceContactBox*,4> racer_shapes_{};
    std::array<bool,4> racer_ghosts_{};
    unsigned track_number_=0;
    std::vector<AiRoadEvent> events_;
    struct SpawnRecord {
        std::int16_t road;
        std::array<std::int16_t,2> points;
        float speed_limit;
    };
    std::vector<SpawnRecord> spawn_records_;
    bool has_spawn_table_=false;
    std::vector<RaceTrafficActor> actors_;
    std::vector<std::unique_ptr<VehicleModel>> models_;
    std::vector<std::unique_ptr<VehicleTrackSession>> impact_sessions_;
    const VehicleTrackCourse* course_=nullptr;
    std::int32_t world_clock_=-1;
    float frame_seconds_=1.F/60.F;
    std::uint32_t rng_=0x60000000U;
    std::uint32_t* world_random_=nullptr;
    unsigned density_=1;
    bool drag_=false;
    unsigned recycle_=0;
    std::array<unsigned,kRaceTrafficCapacity> prepared_actors_{};
    unsigned prepared_count_=0;
    std::array<bool,kRaceTrafficCapacity> advanced_actors_{};
    bool frame_prepared_=false,grid_staged_=false;
    bool activate(unsigned,AiRoadCursor,unsigned,std::uint32_t,bool,float speed_limit,std::string&);
    bool stage_authored(unsigned,float spacing,std::int32_t clock,float* placed_progress,std::string&);
    bool stage_drag_grid(float spacing,float clearance,std::int32_t clock,std::string&);
    bool stage_ordinary_grid(float spacing,float clearance,std::int32_t clock,std::string&);
    bool spawn(unsigned,const std::array<const VehicleState*,4>&,std::uint64_t,std::string&);
    bool prepare_world(std::string&);
    bool plan(unsigned,std::uint32_t,std::string&);
    bool retire(unsigned,std::string&);
};
}
