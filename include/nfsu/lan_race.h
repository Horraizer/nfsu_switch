#pragma once
#include "nfsu/lan_protocol.h"
#include "nfsu/race_session.h"
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
    bool advance(const std::array<GamepadFrame,2>&,std::string&);
    const RaceSession& session()const{return *session_;}
    LanRaceFrame snapshot()const;
    const ChaseCameraTable& cameras(unsigned actor=0)const{return cameras_.at(actor);}
    const std::array<std::string,2>& models()const{return models_;}
    const VehicleHull& host_hull()const{return host_hull_;}
private:
    VehicleTrackCourse course_;
    std::array<ChaseCameraTable,2> cameras_;
    std::array<std::string,2> models_;
    VehicleHull host_hull_;
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
    const Entry& at(std::size_t index)const{return entries_[(first_+index)%entries_.size()];}
    std::array<Entry,12> entries_{};
    std::size_t first_=0,count_=0;
    double cursor_=0;
    std::uint64_t underrun_frames_=0;
    bool playing_=false;
};
}
