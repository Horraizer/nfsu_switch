#pragma once
#include "nfsu/world_motion.h"

#include "nfsu/drive_session.h"
#include "nfsu/crash_camera.h"
#include "nfsu/track_ground.h"
#include "nfsu/camera_motion.h"
#include "nfsu/race_timing.h"
#include "nfsu/vehicle_track.h"
#include "nfsu/steering.h"
#include "nfsu/nitrous.h"
#include "nfsu/nitrous_controls.h"
#include "nfsu/race_contacts.h"
#include "nfsu/race_props.h"
#include "nfsu/ai_driver.h"
#include "nfsu/race_standings.h"
#include "nfsu/race_leaderboard.h"
#include "nfsu/race_style.h"
#include "nfsu/race_drift.h"
#include "nfsu/race_traffic.h"
#include <memory>
#include <limits>
#include <optional>

namespace nfsu {
class RaceRouteDriver;
class TimingService;
// Optional CPU measurements, in milliseconds. No clocks are read when null.
// Six disjoint simulation stages; renderer/presentation are measured separately.
struct RaceSimulationProfile {
    double planner=0,drivers=0,rivals=0,player=0,contacts=0,tail=0;
    // Children of contacts, not additional frame time. Clock reads remain opt-in.
    double traffic_motion=0,prop_motion=0,prop_contacts=0;
    std::array<double,6> values() const noexcept {return {{planner,drivers,rivals,player,contacts,tail}};}
};
// Rivals have no local player's camera. Full presentation remains available
// for diagnostic comparisons, but is not required to simulate a car.
enum class RacePresentation { player, none };
struct RaceSessionWork {
    std::uint64_t frames=0, pad_updates=0, direct_updates=0, physics_steps=0;
    std::uint64_t pair_candidates=0, camera_updates=0, record_encodes=0;
    std::uint64_t rival_driver_updates=0;
};
// A completed planner/publication result for one world step. Bodies, wheel
// feedback and driver history come from the actual session, never this packet.
struct RaceRivalPlan {
    AiDriverPath path;
    float desired_speed=0,throttle_cap=1,route_distance=0,physical_scale=1;
    int driver_category=0;
    bool enabled=true,recovery_cleanup=false,reset_car=false;
    float grid_throttle=0;
};
struct RaceRivalPlans {
    std::size_t step=0,count=0,dropped=0;
    std::array<RaceRivalPlan,3> cars{};
    RaceRivalPlan human;
    bool drag_steering=false;
    // Owned by the planner for the entire live race; never points into plans.
    std::uint32_t* drag_world_random=nullptr;
};
struct RacePairObservation {
    unsigned first=0,second=0; // Player0, then the configured rival slots1..3.
    RacePairContact contact;
    std::uint32_t audio_order=0;
    std::uint8_t other_kind=0;
    VehicleVec3 forward{1,0,0},up{0,0,1};
    double slide_mps=0;
};
struct RacePairFrame {
    std::size_t step=0,count=0,dropped=0;
    std::array<RacePairObservation,128> pairs{};
};
// Optional diagnostic sink for the production queue; absent in ordinary play.
struct RaceQueuedContact {
    unsigned a=0,b=0;
    std::size_t polygon=std::numeric_limits<std::size_t>::max();
    float remaining=0;
    ContactVector impulse{};
    VehicleVec3 point{};
    std::array<VehicleVec3,2> position{},velocity{},angular_velocity{};
    std::array<PlayerRigidMatrix,2> axes{};
    std::size_t prop=std::numeric_limits<std::size_t>::max();
};
struct RaceRivalSetup {
    VehicleSpec spec;
    VehiclePose pose;
    VehicleHull hull;
    std::string name;
    std::string car_type{};
    unsigned ai_mode=1; // Original configuration736, independent of difficulty735.
};
struct RaceDriverWallContact {bool active=false,recent_strong=false;};

// Segment adapter for the chase camera's collision hook. The hook itself
// is an approximation documented by #59; contacts use #60's track world.
class RaceCameraWorld final : public ChaseCameraWorld, public CrashCameraWorld {
public:
    explicit RaceCameraWorld(const TrackContactWorld& world) : world_(world) {}
    double clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const override;
    bool ground(const AiDriverPoint&,float&) const override;
private:
    const TrackContactWorld& world_;
    mutable TrackGroundCache ground_cache_;
};

// Event-1 integration: one pad tick at60Hz, four physics/collision slices,
// route events, race timing, then chase camera. No wall clock or file access.
// The course must outlive the session. Failure is terminal for this session.
class RaceSession {
public:
    explicit RaceSession(const VehicleTrackCourse& course);
    RaceSession(const VehicleTrackCourse& course, const VehicleSpec& spec,
                RaceSteering steering = RaceSteering::baseline,
                const VehicleHull& hull = stock_240sx_hull(),
                RacePresentation presentation = RacePresentation::player);
    ~RaceSession();
    bool set_crash_camera(const CrashCameraRoadSource*,unsigned intensity,unsigned track,std::string&);
    unsigned crash_camera_event()const noexcept{return crash_camera_.event();}
    bool crash_windows_damaged() const noexcept { return crash_windows_damaged_; }
    float next_world_frame_seconds()const noexcept;
    bool crash_camera_enabled()const noexcept{return crash_source_!=nullptr;}
    double world_frame_seconds()const noexcept{return frame_seconds_;}
    std::uint64_t world_visual_frame()const noexcept{return crash_source_?std::uint64_t(visual_frames_):timing_.step;}
    bool set_camera_table(const ChaseCameraTable&,std::string& error);
    bool set_drag(bool enabled,std::string& error,float shift_scale=1.F);
    bool is_drag() const {return drag_;}
    bool bind_world_obstacle(std::uint32_t type,int enter,int leave,WorldObstacleBinding&)const;
    bool world_obstacle_status(const WorldObstacleBinding&,const VehicleState&,float progress,WorldObstacleStatus&)const;
    const VehicleState* world_object_state(unsigned index) const {
        return index<world_objects_.size()&&world_objects_[index].enabled?&world_objects_[index].state:nullptr;
    }
    float travelled_distance() const noexcept{return travelled_distance_;}
    unsigned drag_shifts() const {return drag_shifts_;}
    unsigned drag_perfect_shifts() const {return drag_perfect_shifts_;}
    unsigned drag_category() const {return drag_category_;}
    float drag_shift_scale() const {return drag_shift_scale_;}
    float drag_upshift_rpm() const {return model_.request_upshift_rpm()[(car().gear>=0&&car().gear<int(kVehicleGearSlots))?car().gear:0];}
    bool set_lap_knockout(bool enabled,std::string& error,unsigned laps=3);
    bool is_lap_knockout() const {return lap_knockout_;}
    bool set_time_trial(std::int32_t ticks,std::string& error);
    bool is_time_trial()const{return time_limit_ticks_>0;}
    std::int32_t time_limit_ticks()const{return time_limit_ticks_;}
    std::int32_t time_left_ticks()const{return time_limit_ticks_>timing_.race_ticks?time_limit_ticks_-timing_.race_ticks:0;}
    bool set_rivals(const std::vector<RaceRivalSetup>&,std::string& error,
                    RacePresentation presentation = RacePresentation::none,
                    unsigned player_grid = 1);
    // Explicit original-driver/selector3 seam. The caller supplies recovered
    // low-slip configuration and complete current planner results. No fallback
    // to the test route driver when planning is missing or refuses.
    // Experimental two-human world. Both use the ordinary player controls;
    // no AI planning or physical assistance is applied to the remote human.
    bool set_human_rival(const RaceRivalSetup&,std::string& error);
    bool advance_humans(const std::array<GamepadFrame,2>&,std::string& error);
    bool human_rival() const noexcept {return human_rival_;}
    bool set_planned_rivals(const std::vector<RaceRivalSetup>&,
                            const std::array<float,3>& low_slip,std::string& error,
                            unsigned player_grid = 1);
    bool set_traffic(const std::filesystem::path&,const std::vector<RaceTrafficSetup>&,std::string&,unsigned density=1,std::uint32_t* random=nullptr);
    const RaceProps* props() const {return props_.get();}
    const VehiclePropContactFrame& prop_contacts() const {return prop_contacts_;}
    const RaceTraffic* traffic() const {return traffic_.get();}
    bool start(std::string& error,const VehiclePose* pose=nullptr,
               float countdown_step=kRaceCountdownStepSeconds,float cinematic_seconds=0);
    // Diagnostics: replay authored route crossings at the finish scene.
    bool arm_finish_probe(std::string& error);
    bool prepare_leaderboard(const std::filesystem::path&,const std::string& player_car,std::string&);
    bool prepare_style(const std::filesystem::path&,std::string&,unsigned event=3);
    const RaceStyle& style() const {return style_;}
    void consume_drift_notice() noexcept {style_.consume_drift_notice();}
    const RaceDrift& drift() const {return drift_;}
    const DriftCompetition& drift_competition() const {return drift_competition_;}
    bool is_drift() const {return spec_.drift_mode;}
    void set_player_name(const std::string& name) { if(!name.empty())player_name_=name; }
    const std::string& player_name() const { return player_name_; }
    std::string leaderboard_label(unsigned actor) const;
    RaceLeaderboardMode leaderboard_mode() const {return leaderboard_cycle_.shown;}
    bool leaderboard_ready() const {return gap_map_.valid;}
    bool advance(const GamepadFrame& pad, std::string& error,
                 RaceSimulationProfile* profile=nullptr,TimingService* timer=nullptr);
    bool advance_planned(const GamepadFrame&,const RaceRivalPlans&,std::string& error,
                         RaceSimulationProfile* profile=nullptr,TimingService* timer=nullptr);
    // Already processed driver controls: no pad quantization, player reverse
    // assist or player steering filter. Uses selector1 unless configure_ai
    // explicitly selects the bounded selector3 adapter; no AI planning here.
    // Only a configured selector3 car accepts the ordinary reverse request0.
    bool advance_driver(const VehicleInput& input, std::string& error,int ai_gear_request=-1);
    // Diagnostic opt-in until planner/recovery integration is complete.
    bool configure_ai(float low_slip,std::string& error);
    const AiPhysicsState& ai_physics() const { return vehicle_.ai_physics(); }
    const VehicleState& car() const { return vehicle_.state(); }
    const CarAnimationState& animation() const { return animation_; }
    double light_service_brake() const { return service_brake_; }
    const RaceProgress& progress() const { return vehicle_.progress(); }
    const RaceTimingState& timing() const { return timing_; }
    bool actor_ghost(unsigned actor) const {
        // Native contact diagnostics can own bodies before a timing roster.
        if(actor>=timing_.racers.size())return false;
        return race_racer_body_ghost(timing_.racers[actor],timing_.now_ticks);
    }
    bool actor_recovering(unsigned actor) const {
        if(actor==0)return vehicle_.owner_recovery_active(timing_.now_ticks);
        return actor<=rivals_.size()&&rivals_[actor-1]->vehicle_.owner_recovery_active(timing_.now_ticks);
    }
    bool actor_visible(unsigned actor) const {
        return !race_racer_hidden(timing_.racers.at(actor),timing_.now_ticks,
            float(frame_seconds_),timing_.step);
    }
    const RaceTimingConfig& timing_config() const { return timing_config_; }
    bool start_cinematic() const { return timing_.stage==RaceStage::prerace; }
    bool skip_start_cinematic();
    std::int64_t start_scene_ticks() const {
        return std::int64_t(timing_.now_ticks)-kRaceWorldStartTicks+cinematic_skip_ticks_;
    }
    const ChaseCameraView& camera() const { return view_; }
    const std::array<float,3>& camera_velocity()const {return camera_motion_.velocity();}
    const VehicleSpec& spec() const { return spec_; }
    NitrousSnapshot nitrous() const {
        return lan_nitrous_view_?*lan_nitrous_view_:nitrous_snapshot(nitrous_resource_,nitrous_clock_);
    }
    bool nitrous_purging() const {
        return timing_.stage==RaceStage::countdown&&timing_.outcome==RaceOutcome::running&&nitrous().active;
    }
    bool nitrous_boosting() const {
        return timing_.stage==RaceStage::racing&&timing_.outcome==RaceOutcome::running&&nitrous().active;
    }
    float turbo_pressure() const { return turbo_.value; }
    const VehicleTrackTelemetry& telemetry() const { return track_telemetry_.back(); }
    const std::array<VehicleTrackTelemetry,kDriveStepsPerFrame>& physics_telemetry() const { return track_telemetry_; }
    const std::vector<std::uint8_t>& record() const;
    const RaceSessionWork& work() const { return work_; }
    const VehicleTrackCourse& course() const { return course_; }
    RaceSteering steering_mode() const { return steering_mode_; }
    const SteeringState& steering_state() const { return steering_state_; }
    const SteeringResult& steering_result() const { return steering_result_; }
    std::size_t rival_count() const {return rivals_.size();}
    const RaceSession& rival(std::size_t i) const {return *rivals_.at(i);}
    const AiDriverForwardState& rival_driver(std::size_t i) const {return original_drivers_.at(i);}
    const std::string& rival_name(std::size_t i) const {return rival_setup_.at(i).name;}
    unsigned rival_ai_mode(std::size_t i) const {return rival_setup_.at(i).ai_mode;}
    std::uint64_t pair_impulses() const {return pair_impulses_;}
    // Last completed port pair-solver batch. Geometry/impulses are explicit
    // adapter outputs, not original contact-list strength or callback parity.
    const RacePairFrame& pair_contacts() const {return pair_frame_;}
    // Read the completed world owner's persistent contacts, in stored order.
    // Actor0 is human,1..3 rivals. No solver work or allocation on this read.
    void visit_actor_contacts(unsigned actor,void* context,
        void (*visit)(void*,const ContactRecord&)) const;
    void visit_actor_pairs(unsigned actor,void* context,
        void (*visit)(void*,unsigned,unsigned,const ContactRecord&)) const;
    RaceDriverWallContact driver_wall_contact(unsigned actor) const noexcept;
    unsigned player_position() const;
    bool player_won() const;
    std::array<unsigned,4> result_positions() const noexcept;
    std::vector<RaceResultRow> result_rows() const;
    const RaceStandings& standings() const {return standings_;}
    float competitive_progress(unsigned actor) const {return gap_actors_.at(actor).progress;}
private:
    bool advance_impl(const GamepadFrame*, const VehicleInput*, std::string&,int ai_gear_request=-1,
                       const RaceRivalPlans* plans=nullptr,
                       RaceSimulationProfile* profile=nullptr,TimingService* timer=nullptr,
                       const GamepadFrame* remote_pad=nullptr);
    bool set_rivals_impl(const std::vector<RaceRivalSetup>&,std::string&,RacePresentation,
        const std::array<float,3>*,unsigned player_grid=1);
    void encode() const;
    const VehicleTrackCourse& course_;
    VehicleSpec spec_;
    VehicleModel model_;
    VehicleTrackSession vehicle_;
    NitrousControls controls_;
    NitrousResource nitrous_resource_;
    std::optional<NitrousSnapshot> lan_nitrous_view_;
    NitrousEngineState nitrous_engine_;
    std::uint32_t nitrous_clock_ = 0;
    TurboDisplayState turbo_;
    ChaseCamera chase_;
    RaceCameraWorld camera_world_;
    RaceCrashCamera crash_camera_;
    const CrashCameraRoadSource* crash_source_=nullptr;
    double frame_seconds_=kDriveFrameSeconds,external_frame_seconds_=0;
    double steering_seconds_=0,shift_seconds_=0,visual_frames_=0;
    std::int32_t cinematic_end_ticks_=0;
    std::int32_t cinematic_skip_ticks_=0;
    ChaseCameraView view_;
    CameraMotion camera_motion_;
    int presented_pov_=0;
    unsigned presented_crash_=0;
    bool presented_look_back_=false;
    RaceTimingConfig timing_config_;
    RaceTimingState timing_;
    std::vector<RaceTimingEvent> timing_events_;
    VehicleInput inputs_[kDriveStepsPerFrame]{};
    std::array<VehicleTrackTelemetry,kDriveStepsPerFrame> track_telemetry_{};
    mutable std::vector<std::uint8_t> record_;
    mutable bool record_dirty_ = false;
    mutable RaceSessionWork work_;
    RacePresentation presentation_;
    CarAnimationState animation_;
    double service_brake_ = 0;
    bool started_ = false;
    bool lap_knockout_ = false;
    std::int32_t time_limit_ticks_=0;
    bool drag_ = false;
    float travelled_distance_=0; // actor3B8; absolute-speed integral, not road coordinate
    unsigned drag_category_ = 3;
    unsigned drag_shifts_=0,drag_perfect_shifts_=0;
    float drag_shift_scale_ = 1.F;
    AiDriverForwardState drag_driver_;
    bool ai_physics_ = false;
    RaceSteering steering_mode_ = RaceSteering::baseline;
    SteeringState steering_state_;
    SteeringResult steering_result_;
    std::uint64_t steering_steps_ = 0, shift_step_ = 0;
    bool shifted_ = false;
    float steering_max_speed_ = 1;
    bool steer_input(VehicleInput&, int substep, std::string& error);
    RaceContactBox contact_box_;
    RaceTrackBodyBounds contact_bounds_;
    std::unique_ptr<RaceTraffic> traffic_;
    std::unique_ptr<RaceProps> props_;
    VehiclePropContactFrame prop_contacts_;
    std::vector<RaceRivalSetup> rival_setup_;
    std::vector<std::unique_ptr<RaceSession>> rivals_;
    unsigned player_grid_=1;
    unsigned registration_rank(unsigned actor) const noexcept;
    std::vector<std::unique_ptr<RaceRouteDriver>> rival_drivers_;
    std::array<AiDriverForwardState,3> original_drivers_{};
    AiDriverForwardState finish_driver_;
    std::vector<RaceProgressEvent> finish_probe_events_;
    bool planned_rivals_=false;
    bool human_rival_=false,lan_player_=false;
    friend class LanRaceView;
    std::uint64_t pair_impulses_=0;
    RacePairFrame pair_frame_;
    // Four racers and a bounded authored traffic pool. Records
    // and the world PRNG survive successive contacts.
    struct WorldObjectBody {
        VehicleState state;
        VehicleSpec spec;
        RaceContactBox box;
        RaceTrackBodyBounds bounds;
        bool enabled=false;
        bool animated=false;
        WorldMotionClock clock;
        RaceContactPose history;
        std::array<std::size_t,2> clips{};
        std::array<float,16> placement{};
        TrackPathCoordinate reference;
        bool reference_ready=false;
        WorldTrainPrediction train_prediction;
    };
    std::vector<WorldObjectBody> world_objects_;
    std::vector<WorldMotionClip> world_motion_clips_;
    void initialize_world_object(unsigned);
    bool prepare_world_motion(const std::filesystem::path&,std::string&);
    bool reset_world_motion(std::string&);
    bool advance_world_motion(float,int,std::string&);
    bool place_world_motion(unsigned,bool,std::string&);
    struct WorldContactPair {
        unsigned a=0,b=0;
        std::size_t polygon=std::numeric_limits<std::size_t>::max();
        std::size_t prop_a=std::numeric_limits<std::size_t>::max();
        std::size_t prop=std::numeric_limits<std::size_t>::max();
        bool object=false;
        ContactRecord record;
        RaceHullCache query;
        bool track() const {return polygon!=std::numeric_limits<std::size_t>::max();}
        bool scenery() const {return prop!=std::numeric_limits<std::size_t>::max()||
            prop_a!=std::numeric_limits<std::size_t>::max();}
        bool cars() const {return !track()&&!scenery()&&!object;}
    };
    std::vector<WorldContactPair> world_contact_pairs_;
    struct WorldTrackShape {std::size_t polygon;unsigned id;bool expanded=true;};
    std::vector<WorldTrackShape> world_track_shapes_;
    std::vector<bool> world_shape_ids_;
    // Reserve only ordinary racer/legacy placeholder slots until traffic is configured.
    std::vector<RaceTrackPolygonCache> world_track_caches_=std::vector<RaceTrackPolygonCache>(10);
    bool external_contact_owner_=false;
    friend struct RaceWorldContactProbe;
    bool resolve_world_contacts(const std::array<RaceContactPose,4>&,
        const std::array<std::uint64_t,4>&,std::string&,RaceSimulationProfile*,TimingService*,
        std::vector<RaceQueuedContact>* trace=nullptr);
    std::vector<std::uint64_t> contact_traffic_epochs_;
    std::uint32_t contact_seed_=12345;
    std::uint32_t* world_random_=nullptr;
    bool crash_windows_damaged_=false;
    float contact_component_interval_=0;
    RaceStandings standings_;
    RaceGapMap gap_map_;
    RaceLeaderboardCycle leaderboard_cycle_;
    std::array<std::string,4> car_labels_;
    std::string player_name_="YOU";
    std::array<RaceGapActor,4> gap_actors_{};
    void update_standings();
    RaceStyle style_;
    RaceDrift drift_;
    DriftCompetition drift_competition_;
    unsigned drift_class_=0;
    float drift_reference_speed_=0;
    RaceStyleDriving style_driving_;
    RaceStyleNearMiss near_miss_;
    std::vector<RaceHullCache> near_pass_cache_=std::vector<RaceHullCache>(3);
    float drafting_=0;
    float external_human_drafting_=-1;
    std::vector<bool> style_shortcuts_;
    void update_style();
};

struct RaceInputRun {
    std::uint32_t ticks = 0;
    std::uint32_t buttons = 0;
    std::int32_t steer = 0;
};
constexpr std::size_t kRaceInputMaxTicks = 120000;
bool parse_race_input(const std::string& text, std::vector<RaceInputRun>& runs,
                      std::string& error);
std::string format_race_input(const std::vector<RaceInputRun>& runs);
GamepadFrame race_input_frame(const RaceInputRun& run, std::uint64_t tick);
void append_race_input(std::vector<RaceInputRun>& runs, const GamepadFrame& pad);

enum class RaceDrivingLine {
    progress_spline,
    event1_forward,
    event8_forward,
    // One derived lane per circuit track and direction (see race_session.cpp
    // for how each table was produced and pinned), then the derived drift
    // lanes.
    circuit1001_forward, circuit1001_reverse, circuit1002_reverse,
    circuit1004_forward, circuit1004_reverse, circuit1005_reverse,
    circuit1006_forward, circuit1006_reverse, circuit1007_forward,
    circuit1007_reverse, circuit1008_forward, circuit1008_reverse,
    drift1301_forward, drift1302_forward, drift1303_forward,
    drift1304_forward, drift1305_forward, drift1306_forward,
    drift1307_forward, drift1308_forward,
    // The forward drag lanes (owner task 2026-10-15). The reverse drag
    // events stay on the progress spline until the reverse derivation is
    // fixed: a reverse table came out byte-identical to its forward one,
    // which a traversal of the same corridor backwards cannot be.
    drag1201_forward, drag1202_forward, drag1206_forward, drag1207_forward,
    drag1210_forward, drag1214_forward,
    // Track 1003's reverse lane, the one direction no circuit event
    // races; the lap-knockout events 5 and 10 (owner task 2026-10-18) do.
    circuit1003_reverse,
};

// The lane the auto-driver follows for one supported circuit event: events 1
// and 8 keep their named lines, every other supported circuit takes its
// track-and-direction table, and anything else follows the raw progress
// spline. Events sharing a track and direction share one table.
RaceDrivingLine race_event_driving_line(std::uint32_t event) noexcept;

// Pure pursuit with curvature braking, 10 Hz decisions and quantized steering.
// Events 1 and 8 select authored test-driver offsets; other courses follow their spline.
// This changes controller values only, never pose, progress or reset state.
// Host regression driver, not the retail AI. Event1 includes a brief opening
// parking-brake stroke to exercise live tyre smoke before the clean drive.
class RaceRouteDriver {
public:
    explicit RaceRouteDriver(RaceDrivingLine line = RaceDrivingLine::progress_spline) : line_(line) {}
    GamepadFrame sample(const RaceSession& session);
private:
    RaceDrivingLine line_;
    std::uint64_t ticks_ = 0;
    bool accelerating_ = true;
    double desired_speed_ = 10.0; // host route driver; not the retail AI
    // Stuck-recovery state for the derived circuit lanes; zero when idle.
    int stuck_ = 0;
    int recovering_ = 0;
    GamepadFrame held_;
};

std::vector<std::string> race_session_hud(const RaceSession& session, std::uint32_t event_number = 1);

// Opaque passes rendered with the same camera, track first and car second.
// A depth tie belongs to the car. MaterialStateMode::off is required for
// both passes; alpha blending across separate depth buffers is not modeled.
bool compose_race_frame(const Frame& track, const Frame& car, Frame& composed);
bool compose_race_frame(const Frame& track, const Frame& car,
                        std::vector<unsigned char>& rgba);

} // namespace nfsu
