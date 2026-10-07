#include "nfsu/race_run.h"
#include "nfsu/race_events.h"
#include <algorithm>
#include <limits>

namespace nfsu {
namespace {
std::filesystem::path asset(const std::filesystem::path& root, const TrackSlice& track, const char* name) {
    for (const auto& file : track.assets.files)
        if (file.status == AssetStatus::ok && asset_names_match(file.expected.path, name)) return root/file.found_as;
    return {};
}
}
bool RaceRun::set_performance_levels(const VehicleUpgradeLevels& levels, std::string& error) {
    if (metadata_ready_ || session_) { error = "performance setup must precede metadata/start"; return false; }
    // Validate levels and flags through the same application path before
    // committing configuration, without any retail file access.
    std::array<unsigned char,kVehicleRecordSize> original{};
    encode_vehicle_handling(stock_240sx_handling(),original.data(),original.size());
    encode_vehicle_tuning(stock_240sx_tuning(),original.data(),original.size());
    VehicleSpec checked;
    if (!make_race_vehicle_spec(original.data(),original.size(),levels,checked,error)) return false;
    levels_ = levels; explicit_performance_ = true; return true;
}
bool RaceRun::set_free_drive(bool enabled, std::string& error) {
    if (metadata_ready_ || session_) { error = "driving mode must precede metadata/start"; return false; }
    free_drive_ = enabled;
    return true;
}
bool RaceRun::set_steering(RaceSteering mode, std::string& error) {
    if (metadata_ready_ || session_) { error="steering must precede metadata/start"; return false; }
    if (mode != RaceSteering::baseline && mode != RaceSteering::recovered_predicate0 &&
        mode != RaceSteering::recovered_predicate1) { error="invalid steering mode"; return false; }
    steering_=mode; return true;
}
bool RaceRun::set_car(const std::string& car_type_name, std::string& error) {
    if (metadata_ready_ || session_) { error="car selection must precede metadata/start"; return false; }
    if (car_type_name.empty()) { error="car selection needs a car type name"; return false; }
    car_=car_type_name; return true;
}
bool RaceRun::read_metadata(const std::filesystem::path& front, const std::filesystem::path& global,
                            std::string& error, TrackSliceLoadObserver* observer) {
    {
        std::vector<unsigned char> bytes;
        const auto events = load_race_event_table(front, bytes);
        if (observer) observer->checkpoint("race event table");
        const auto tracks = load_track_info_table(global, bytes);
        if (observer) observer->checkpoint("race track info");
        const auto* event = find_race_event(events, 1);
        if (!event || event->races.size() != 1) { error = "event 1 missing or not one race"; return false; }
        const auto& slot = event->races[0];
        const auto* info = find_track_info(tracks, slot.track);
        if (!info || slot.track != 1003 || slot.laps != 2 || slot.direction != 0) {
            error = "event 1 must be forward track 1003, two laps"; return false;
        }
        laps_ = free_drive_ ? 0 : slot.laps;
        direction_ = slot.direction;
        lap_skip_ = info->lap_skip;
    }
    if (observer) observer->checkpoint("race metadata released");
    metadata_ready_ = true;
    return true;
}
bool RaceRun::prepare_metadata(const std::filesystem::path& root, std::string& error,
                               TrackSliceLoadObserver* observer) {
    if (metadata_ready_ || session_) { error = "race metadata already prepared"; return false; }
    return read_metadata(root / "Frontend/FRONTB.LZC", root / "Global/GLOBALB.BUN", error, observer);
}
bool RaceRun::start(const std::filesystem::path& root, const TrackSlice& track, std::string& error,
                    TrackSliceLoadObserver* observer) {
    if (session_ || track.assets.verdict != AssetVerdict::complete) {
        error = "race requires a fresh session and a complete first-lap asset check"; return false;
    }
    if (!metadata_ready_ && !read_metadata(asset(root, track, "Frontend/FRONTB.LZC"),
                                             asset(root, track, "Global/GLOBALB.BUN"), error, observer)) return false;
    const auto trackb = asset(root, track, "Tracks/TRACKB1003.lzc");
    if (!load_vehicle_track_course(trackb, laps_, direction_, lap_skip_, 0, course_, error)) return false;
    if (observer) observer->checkpoint("race course prepared");
    {
        std::vector<unsigned char> bytes;
        zones_ = load_track_zones(trackb, bytes);
        table_ = parse_track_stream_table(bytes.data(), bytes.size());
    }
    if (observer) observer->checkpoint("race zones prepared");
    TrackStreamSelection selected;
    if (!select_track_stream_sections(table_, zones_, 1003, TrackStreamSet::permanent, selected, error)) return false;
    prepared_.insert(selected.records.begin(), selected.records.end());
    if (prepared_.size() != track.sections) { error = "prepared track and race residency differ"; return false; }
    TrackResidencyOptions options; options.budget_bytes = track_stream_pool_bytes(table_);
    std::vector<TrackResidencyStep> startup;
    if (!residency_.start(table_, zones_, 1003, track_residency_table_costs(table_), options, startup, error)) return false;
    if (observer) observer->checkpoint("race residency prepared");
    const std::string version = "NFSU_RACE_STATE 1\n";
    sequence_.update(version.data(), version.size());
    for (const auto& step : startup) {
        if (!apply(step, error)) return false;
        const auto line = track_residency_step_line(table_, step) + "\n";
        sequence_.update(line.data(), line.size());
    }
    if (resident_ != prepared_ || bytes_read_ != track.input_bytes) {
        error = "race startup differs from the uploaded permanent set"; return false;
    }
    VehicleSpec spec = stock_240sx_vehicle_spec();
    // The default 240SX keeps the built-in spec, so the pinned baseline
    // replay is untouched; any other car - and any explicit performance
    // levels - decode the selected car's own record from the retail table.
    if ((explicit_performance_ || car_ != "240SX") && !load_race_vehicle_spec(
            asset(root,track,"Global/GLOBALB.BUN"),car_,levels_,spec,error)) return false;
    session_ = std::make_unique<RaceSession>(course_,spec,steering_);
    if (observer) observer->checkpoint("race session constructed");
    const auto started = session_->start(error);
    if (observer) observer->checkpoint("race session started");
    return started;
}
bool RaceRun::apply(const TrackResidencyStep& step, std::string& error) {
    for (auto r : step.releases) { resident_.erase(r); ++releases_; }
    for (auto r : step.loads) {
        if (!prepared_.count(r) || r >= table_.records.size()) {
            error = "race requested a section outside the uploaded event-1 set"; return false;
        }
        resident_.insert(r); ++loads_;
        bytes_read_ += table_.records[r].size;
    }
    return true;
}
bool RaceRun::simulate(const GamepadFrame& pad, std::string& error) {
    if (!session_ || pending_) { error = "race simulation/commit order violated"; return false; }
    if (!session_->advance(pad, error)) return false;
    pending_ = true; return true;
}
bool RaceRun::commit(std::string& error) {
    if (!session_ || !pending_) { error = "race commit without simulation"; return false; }
    const auto& car = session_->car();
    TrackResidencySample sample;
    sample.position[0] = float(car.position.x); sample.position[1] = float(car.position.y);
    sample.velocity[0] = float(car.velocity.x); sample.velocity[1] = float(car.velocity.y);
    TrackResidencyStep step;
    if (!residency_.advance(sample, step, error) || !apply(step, error)) return false;
    // This event's recovered permanent set stays resident. Do not claim
    // an unsupported transition happened while drawing stale resources.
    if (resident_ != prepared_) { error = "race residency left the fixed uploaded set"; return false; }
    auto record = session_->record();
    const auto line = track_residency_step_line(table_, step) + "\n";
    record.insert(record.end(), line.begin(), line.end());
    for (auto index : resident_) {
        const auto r = static_cast<std::uint32_t>(index);
        for (unsigned k = 0; k < 4; ++k) record.push_back(static_cast<std::uint8_t>(r >> (8*k)));
    }
    const auto digest = sha256(record.data(), record.size());
    sequence_.update(digest.data(), digest.size());
    step_line_ = std::to_string(session_->timing().step) + " " + sha256_hex(digest) + "\n";
    steps_.update(step_line_.data(), step_line_.size());
    pending_ = false; return true;
}
std::string RaceRun::sequence_hash() const { auto copy = sequence_; return sha256_hex(copy.finish()); }
std::string RaceRun::steps_hash() const { auto copy = steps_; return sha256_hex(copy.finish()); }
bool RaceRun::matches_event1_replay() const {
    if (steering_ != RaceSteering::baseline) return false;
    if (!session_ || pending_) return false;
    // The pin is the 240SX player-only replay; no other car may claim it.
    if (car_ != "240SX") return false;
    if (explicit_performance_ && !performance_levels_equal(levels_,{})) return false;
    const auto& timing = session_->timing(); const auto& r = timing.racers[0];
    return timing.outcome == RaceOutcome::finished && timing.step == 48332 &&
        session_->telemetry().resets == 0 && r.highest_lap == 2 &&
        r.lap_ticks[1] == 1615960 && r.lap_ticks[2] == 1611360 && r.finish_ticks == 3227320 &&
        sequence_hash() == kEvent1SequenceHash && steps_hash() == kEvent1StepsHash;
}
bool RaceRun::matches_configured_replay() const {
    if (steering_ != RaceSteering::baseline) return false;
    // The performance replay is as 240SX as the baseline one.
    if (car_ != "240SX") return false;
    if (!explicit_performance_ || performance_levels_equal(levels_,{})) return matches_event1_replay();
    if (!session_ || pending_ || !performance_levels_equal(levels_,performance1_levels())) return false;
    const auto& timing=session_->timing(); const auto& r=timing.racers[0];
    return timing.outcome==RaceOutcome::finished && timing.step==48305 &&
        session_->telemetry().resets==0 && r.highest_lap==2 &&
        r.lap_ticks[1]==1614600 && r.lap_ticks[2]==1610880 && r.finish_ticks==3225480 &&
        sequence_hash()==kPerformance1SequenceHash && steps_hash()==kPerformance1StepsHash;
}
std::vector<std::string> RaceRun::result_lines() const {
    if (!session_) return {"RACE NOT STARTED"};
    const auto& t = session_->timing(); const auto& r = t.racers[0];
    std::vector<std::string> lines = {"event 1 track 1003 laps " + std::to_string(r.highest_lap) +
        " outcome " + race_outcome_name(t.outcome) + " steps " + std::to_string(t.step) +
        " resets " + std::to_string(session_->telemetry().resets)};
    for (int i = 1; i <= r.highest_lap && i < kRaceLapSlots; ++i)
        lines.push_back("lap " + std::to_string(i) + " ticks " + std::to_string(r.lap_ticks[i]) +
                        " time " + format_race_ticks(r.lap_ticks[i]));
    lines.push_back("total ticks " + std::to_string(r.finish_ticks) + " time " + format_race_ticks(r.finish_ticks));
    lines.push_back("state-sha256 " + sequence_hash());
    lines.push_back("steps-sha256 " + steps_hash());
    lines.push_back("stream loads " + std::to_string(loads_) + " releases " + std::to_string(releases_) +
                    " bytes-read " + std::to_string(bytes_read_));
    return lines;
}
bool RaceReplay::load(const std::string& text, std::string& error, bool performance1) {
    index_ = used_ = tick_ = 0; runs_.clear();
    if (sha256_hex(sha256(text.data(), text.size())) != (performance1 ? kPerformance1InputHash : kEvent1InputHash)) {
        error = "event1.txt does not match the pinned desktop recording"; return false;
    }
    return parse_race_input(text, runs_, error);
}
bool RaceReplay::next(std::uint64_t tick, GamepadFrame& pad, std::string& error) {
    if (tick != tick_ || exhausted()) { error = "replay exhausted or tick out of order"; return false; }
    pad = race_input_frame(runs_[index_], tick);
    if (++used_ == runs_[index_].ticks) { ++index_; used_ = 0; }
    ++tick_; return true;
}
void RaceTickDebt::add(std::uint64_t us) {
    const auto room = std::numeric_limits<std::uint64_t>::max() - units_;
    units_ += us > room/60 ? room : us*60;
}
bool RaceTickDebt::take() {
    if (units_ < 1000000) return false;
    units_ -= 1000000; return true;
}
} // namespace nfsu
