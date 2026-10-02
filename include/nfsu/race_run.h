#pragma once
#include "nfsu/race_session.h"
#include "nfsu/race_setup.h"
#include "nfsu/track_slice.h"
#include <set>

namespace nfsu {
constexpr const char* kEvent1InputHash = "CED29AB7D90F0861AFE2C0B604D0EDDFB4F769BEB067944F8E9C7DB0B892E0AC";
constexpr const char* kEvent1SequenceHash = "79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9";
constexpr const char* kEvent1StepsHash = "E4D4D6E955D59F7FF11C3318BC455D08B7903CA46BBDE8E1FC3F2AC32AEA54E5";
constexpr const char* kPerformance1InputHash = "BB6CE84A16C782662E43C84C02FB5BC57B4B6290E58726438A66E5E195D56598";
constexpr const char* kPerformance1SequenceHash = "2213D69411DEE904BB6AFACE0DDC73502A0DE5E99B75DAA16F8F9FBDBB49DA86";
constexpr const char* kPerformance1StepsHash = "9A8E8C79BD5573A413D2F5C60E7C9521949F4B40225FE0EF18A796637ED302EF";

// Same simulation, residency currency and byte encoding as nfsu_race.
// split simulate/commit lets the platform time the stages independently.
// No rendering, pad polling or wall clock affects the recorded state.
class RaceRun {
public:
    bool prepare_metadata(const std::filesystem::path& root, std::string& error,
                          TrackSliceLoadObserver* observer = nullptr);
    bool set_performance_levels(const VehicleUpgradeLevels& levels, std::string& error);
    bool set_steering(RaceSteering mode, std::string& error);
    bool start(const std::filesystem::path& root, const TrackSlice& track, std::string& error,
               TrackSliceLoadObserver* observer = nullptr);
    bool simulate(const GamepadFrame& pad, std::string& error);
    bool commit(std::string& error);
    const RaceSession& session() const { return *session_; }
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
    VehicleUpgradeLevels levels_;
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
