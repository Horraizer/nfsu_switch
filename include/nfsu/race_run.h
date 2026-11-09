#pragma once
#include "nfsu/race_session.h"
#include "nfsu/race_events.h"
#include "nfsu/race_setup.h"
#include "nfsu/intro_ai.h"
#include "nfsu/race_crash_road.h"
#include "nfsu/track_slice.h"
#include <optional>
#include <set>

namespace nfsu {
// Native player callbacks/runtime stock setup, shared .9 s ordinary start.
// Host diagnostic input includes an explicit early tyre-smoke/braking witness.
// CLI and RaceRun independently consume the full two-lap input without reset.
constexpr const char* kEvent1InputHash = "6E9CE2689BA9A33E4C2D8E61795E7BDB677B6AAC45E2812B0A57FD7F30FCA66F";
constexpr const char* kEvent1SequenceHash = "70D6FA38CCAED185EA55C4482E249C163152B05885E055BF1B7CB965BDAE06AD";
constexpr const char* kEvent1StepsHash = "CF08E75406E4AA051BE0297E8D2E85D0F52764E8DFC01A42ADB82946671CC001";
constexpr const char* kPerformance1InputHash = "E06FCA77A8AB5042A8BB499D3D0351A4913524CB93781EABAC5299AA17DFE4B0";
constexpr const char* kPerformance1SequenceHash = "C0206DDFDA25C9A6D12256DBEA186898E7F511944E975E5E89BBB621B420A97C";
constexpr const char* kPerformance1StepsHash = "A18E0A4EAB3D038176E1EC972782A799C81E9E44F6A99935F64836FAA6D487F9";

// Authored 34035 timing, shared by console and desktop presentation.
bool load_race_start_timing(const std::filesystem::path&,unsigned event,float& countdown_step,
                            float& cinematic_seconds,std::string& error,unsigned race_index=0);

// Same simulation, residency currency and byte encoding as nfsu_race.
// split simulate/commit lets the platform time the stages independently.
// No rendering, pad polling or wall clock affects the recorded state.
class RaceRun {
public:
    std::uint32_t track_number() const noexcept { return track_number_; }
    bool set_event(std::uint32_t event, std::string& error,unsigned race_index=0);
    bool set_difficulty(unsigned difficulty,std::string& error);
    bool set_crash_camera(unsigned intensity,std::string& error);
    bool set_start_clock(unsigned release_frame,unsigned go_frame,std::string& error);
    bool prepare_metadata(const std::filesystem::path& root, std::string& error,
                          TrackSliceLoadObserver* observer = nullptr);
    bool set_performance_levels(const VehicleUpgradeLevels& levels, std::string& error);
    bool set_steering(RaceSteering mode, std::string& error);
    // Port free driving: same first track and controls, no lap finish.
    bool set_free_drive(bool enabled, std::string& error);
    bool set_quick_race(const QuickRaceOptions&,std::string& error);
    // The car every race drives, by car type name; "240SX" until this is
    // called. Refused after metadata or a session exists, like the setters
    // above. The name itself is validated against the retail car type table
    // when start() reads it; a caller that wants the refusal early resolves
    // it through load_car_assets first.
    bool set_car(const std::string& car_type_name, std::string& error);
    bool set_intro_grid(const VehiclePose&,const VehicleHull&,
                        const std::vector<RaceRivalSetup>&,const std::array<int,3>& contexts,std::string& error,
                        unsigned player_grid=1);
    bool set_intro_traffic(const std::vector<RaceTrafficSetup>&,std::string&);
    bool start(const std::filesystem::path& root, const TrackSlice& track, std::string& error,
               TrackSliceLoadObserver* observer = nullptr, const std::string& player_name = {});
    bool simulate(const GamepadFrame& pad, std::string& error,
                  RaceSimulationProfile* profile=nullptr,TimingService* timer=nullptr);
    bool commit(std::string& error);
    bool skip_start_cinematic();
    bool arm_finish_probe(std::string& error);
    const RaceSession& session() const { return *session_; }
    void consume_drift_notice() noexcept {session_->consume_drift_notice();}
    std::uint32_t* audio_world_random()noexcept{return intro_ai_.world_random();}
    const std::string& car() const noexcept { return car_; }
    const std::string& step_line() const { return step_line_; }
    std::string sequence_hash() const;
    std::string steps_hash() const;
    std::uint32_t event_number() const { return event_number_; }
    bool matches_event1_replay() const;
    bool matches_configured_replay() const;
    std::vector<std::string> result_lines() const;
    void set_recovery_trace(bool value) {intro_ai_.set_recovery_trace(value);}
    std::string difficulty_line() const {return intro_ai_.difficulty_line();}
    std::uint64_t persistent_bytes() const { return course_.world.memory.resident_bytes; }
    std::size_t resident_count() const { return resident_.size(); }
private:
    std::uint32_t event_number_ = 1, track_number_ = 1003;
    unsigned race_index_=0;
    RaceType race_type_=RaceType::circuit;
    VehicleTrackCourse course_;
    TrackStreamTable table_;
    TrackZoneReport zones_;
    TrackResidencySession residency_;
    std::unique_ptr<RaceSession> session_;
    std::set<std::size_t> resident_, prepared_;
    Sha256 sequence_, steps_;
    std::string step_line_;
    std::uint64_t loads_ = 0, releases_ = 0, bytes_read_ = 0;
    std::int8_t laps_ = 0, direction_ = 0, lap_skip_ = 0;
    bool metadata_ready_ = false;
    bool pending_ = false;
    bool authored_start_=false;
    float authored_countdown_=0,authored_cinematic_=0;
    bool explicit_performance_ = false;
    bool free_drive_ = false;
    std::optional<QuickRaceOptions> quick_race_;
    bool traffic_configured_ = false;
    VehicleUpgradeLevels levels_;
    unsigned difficulty_=1;
    float catchup_weight_=0;
    float rival_throttle_cap_=1;
    unsigned catchup_traffic_selector_=0;
    std::int32_t time_limit_ticks_=0;
    std::string car_ = "240SX";
    RaceSteering steering_ = RaceSteering::baseline;
    std::vector<RaceRivalSetup> rivals_;
    unsigned player_grid_=1;
    std::array<int,3> rival_contexts_{};
    IntroAiPlanner intro_ai_;
    RaceCrashRoad crash_road_;
    unsigned crash_intensity_=0;
    DriveControls drag_controls_;
    std::vector<RaceTrafficSetup> traffic_;
    VehicleHull player_hull_;
    VehiclePose player_grid_pose_;
    bool read_metadata(const std::filesystem::path& front, const std::filesystem::path& global,
                       std::string& error, TrackSliceLoadObserver* observer);
    bool apply(const TrackResidencyStep& step, std::string& error);
};

class RaceReplay {
public:
    bool load(const std::string& text, std::string& error, bool performance1 = false);
    bool next(std::uint64_t tick, GamepadFrame& pad, std::string& error);
    bool exhausted() const { return index_ == runs_.size(); }
private:
    std::vector<RaceInputRun> runs_;
    std::size_t index_ = 0, used_ = 0;
    std::uint64_t tick_ = 0;
};

// Clock debt is never discarded during normal frames: at most eight fixed
// steps are consumed per display, leaving excess debt for the next one.
// Suspended applications reset explicitly; a frame rate is never a dt.
class RaceTickDebt {
public:
    void add(std::uint64_t microseconds);
    bool take();
    void reset() { units_ = 0; }
    std::uint64_t debt_ticks() const { return units_ / 1000000; }
private:
    std::uint64_t units_ = 0;
};
} // namespace nfsu
