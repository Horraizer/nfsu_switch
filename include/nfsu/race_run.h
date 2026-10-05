#pragma once
#include "nfsu/race_session.h"
#include "nfsu/race_setup.h"
#include "nfsu/track_slice.h"
#include <set>

namespace nfsu {
constexpr const char* kEvent1InputHash = "D4D5172161A897FD4A746DC58E9C35CE235739658267E56F9E7BF401A9247515";
constexpr const char* kEvent1SequenceHash = "321C957E298D7FC6FD694B6ECED4E679AA17401D9675B635A1DA1F4049CF6C6B";
constexpr const char* kEvent1StepsHash = "8A45CB6686CA31BACF5C5F14AC14A7380486E899F0EAC8A3EDF7316F44CB55E5";
constexpr const char* kPerformance1InputHash = "23F1D49A8FEABAE738728C872B6A1AF77E5C45F6C2FBFD31D2C7B380779B20AF";
constexpr const char* kPerformance1SequenceHash = "C412C5EADD10A2F96782E4A9BAF9BBADE1AB21AE12936ADA7DC22E777B0776CF";
constexpr const char* kPerformance1StepsHash = "1B57E0627E6F514974A6695507CA5E20345EE60A6C2DBCC570D08FDE13E0A4F4";

// Same simulation, residency currency and byte encoding as nfsu_race.
// split simulate/commit lets the platform time the stages independently.
// No rendering, pad polling or wall clock affects the recorded state.
class RaceRun {
public:
    bool prepare_metadata(const std::filesystem::path& root, std::string& error,
                          TrackSliceLoadObserver* observer = nullptr);
    bool set_performance_levels(const VehicleUpgradeLevels& levels, std::string& error);
    bool set_steering(RaceSteering mode, std::string& error);
    // Port free driving: same first track and controls, no lap finish.
    bool set_free_drive(bool enabled, std::string& error);
    // The car every race drives, by car type name; "240SX" until this is
    // called. Refused after metadata or a session exists, like the setters
    // above. The name itself is validated against the retail car type table
    // when start() reads it; a caller that wants the refusal early resolves
    // it through load_car_assets first.
    bool set_car(const std::string& car_type_name, std::string& error);
    bool start(const std::filesystem::path& root, const TrackSlice& track, std::string& error,
               TrackSliceLoadObserver* observer = nullptr);
    bool simulate(const GamepadFrame& pad, std::string& error);
    bool commit(std::string& error);
    const RaceSession& session() const { return *session_; }
    const std::string& car() const noexcept { return car_; }
    const std::string& step_line() const { return step_line_; }
    std::string sequence_hash() const;
    std::string steps_hash() const;
    bool matches_event1_replay() const;
    bool matches_configured_replay() const;
    std::vector<std::string> result_lines() const;
    std::uint64_t persistent_bytes() const { return course_.world.memory.resident_bytes; }
    std::size_t resident_count() const { return resident_.size(); }
private:
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
    bool explicit_performance_ = false;
    bool free_drive_ = false;
    VehicleUpgradeLevels levels_;
    std::string car_ = "240SX";
    RaceSteering steering_ = RaceSteering::baseline;
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
