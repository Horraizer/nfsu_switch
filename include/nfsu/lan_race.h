#pragma once
#include "nfsu/lan_protocol.h"
#include "nfsu/race_session.h"
#include "nfsu/race_crash_road.h"
#include "nfsu/race_events.h"
#include "nfsu/intro_ai.h"
#include "nfsu/vehicle_tuning.h"
#include <filesystem>
#include <memory>

namespace nfsu {
bool load_lan_car_names(const std::filesystem::path&,std::vector<std::string>&,std::string&);
const std::string* find_lan_car_name(const std::vector<std::string>&,std::uint32_t);
VehicleUpgradeLevels lan_performance_levels(unsigned tier);
class LanRaceWorld {
public:
    bool load(const std::filesystem::path&,std::string&,const LanRaceOptions& options={});
    bool load_local(const std::filesystem::path&,unsigned event,unsigned round,QuickRaceMode,
                    const LanRaceOptions&,std::string&,unsigned traffic_density=0,
                    const std::vector<RaceTrafficSetup>& traffic={});
    bool advance(const std::array<GamepadFrame,2>&,std::string&,RaceSimulationProfile* =nullptr,TimingService* =nullptr);
    const RaceSession& session()const{return *session_;}
    LanRaceFrame snapshot()const;
    const ChaseCameraTable& cameras(unsigned actor=0)const{return cameras_.at(actor);}
    const std::array<std::string,2>& models()const{return models_;}
    const VehicleHull& host_hull()const{return host_hull_;}
private:
    bool load_course(const std::filesystem::path&,unsigned event,unsigned round,
                     const LanRaceOptions&,bool split,std::string&,QuickRaceMode=QuickRaceMode::free_run,unsigned traffic_density=0,
                     const std::vector<RaceTrafficSetup>& traffic={});
    VehicleTrackCourse course_;
    std::array<ChaseCameraTable,2> cameras_;
    std::array<std::string,2> models_;
    VehicleHull host_hull_;
    std::array<RaceCrashRoad,2> crash_roads_;
    struct LocalDrag {
        std::array<IntroAiPlanner,2> planners;
        std::array<DriveControls,2> controls;
    };
    std::unique_ptr<LocalDrag> drag_;
    std::unique_ptr<RaceSession> session_;
};
// A materialized render-only view. It never integrates physics, solves a
// contact, or drives the world RNG. This is not a rollback/save-state API.
class LanRaceView {
public:
    bool open(const RaceSession&,const ChaseCameraTable&,unsigned local_actor,std::string&,const VehicleHull* host_hull=nullptr);
    bool apply(const LanRaceFrame&,double seconds,std::string&,bool look_back=false,bool next_camera=false);
    const RaceSession& session()const{return *view_;}
private:
    std::unique_ptr<RaceSession> view_;unsigned local_=0;bool have_frame_=false;
};
LanRaceFrame lan_interpolate(const LanRaceFrame&,const LanRaceFrame&,float fraction);

// Delayed host timeline for the guest view. A burst of packets changes the
// available history, not the playback clock.
class LanSnapshotBuffer {
public:
    void push(const LanRaceFrame&,std::uint32_t input_ack);
    bool sample(double seconds,LanRaceFrame&,std::uint32_t& input_ack);
    std::uint64_t underrun_frames()const noexcept{return underrun_frames_;}
private:
    struct Entry {LanRaceFrame frame;std::uint32_t input_ack=0;};
    void reset_playback(double delay);
    void observe_delivery(double steps);
    const Entry& at(std::size_t index)const{return (*entries_)[(first_+index)%entries_->size()];}
    // Retain group cadence plus fractional/loss margins without consuming
    // the Switch application stack. Allocated once before the frame loop.
    std::unique_ptr<std::array<Entry,24>> entries_=std::make_unique<std::array<Entry,24>>();
    std::size_t first_=0,count_=0;
    double cursor_=0,rate_=1,delay_steps_=3,delay_age_steps_=0;
    struct Phase {double offset=0,at=0;};
    std::array<Phase,16> phases_{};
    std::size_t phase_next_=0,phase_count_=0;
    double elapsed_steps_=0,clock_phase_=0,clock_rate_=1,fast_until_=0;
    std::uint32_t observed_step_=0;
    std::uint32_t pending_first_step_=0,last_gap_step_=0;
    double gap_headroom_=0,fractional_age_steps_=60;
    double prior_gap_headroom_=0,recovery_delay_=3,starved_steps_=0,normal_interval_=1;
    std::uint32_t prior_gap_step_=0;
    std::array<double,16> delivery_intervals_{};
    std::size_t delivery_next_=0,delivery_count_=0;
    std::uint64_t underrun_frames_=0;
    bool playing_=false,refilling_=false,have_observed_=false,pending_new_=false,recovering_=false,previous_pause_=false;
};
}
