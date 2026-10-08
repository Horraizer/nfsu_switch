#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nfsu {

// Fixed storage for profiling pages. Samples are collected during play;
// strings are built only after the timed run has stopped.
class ProfileDistribution {
    static constexpr unsigned kBins = 400;
    std::array<std::uint64_t, kBins + 1> bins_{};
    std::uint64_t count_ = 0;
    double sum_ = 0;
    double max_ = 0;
public:
    static constexpr double kBinWidthMs = 0.25;
    static constexpr unsigned kOverflowBin = kBins;
    void reset() { *this = {}; }
    void add(double ms) {
        if (!(ms >= 0) || ms > 1000000.0) return;
        const unsigned bin = ms >= 100.0 ? kOverflowBin : static_cast<unsigned>(ms / kBinWidthMs);
        ++bins_[bin]; ++count_; sum_ += ms;
        if (ms > max_) max_ = ms;
    }
    std::uint64_t count() const { return count_; }
    double mean() const { return count_ ? sum_ / static_cast<double>(count_) : 0; }
    double max() const { return max_; }
    unsigned percentile_bin(unsigned percent) const {
        if (!count_ || percent == 0 || percent > 100) return kOverflowBin;
        const std::uint64_t rank = (count_/100)*percent + ((count_%100)*percent+99)/100;
        std::uint64_t seen = 0;
        for (unsigned bin = 0; bin <= kOverflowBin; ++bin) {
            seen += bins_[bin];
            if (seen >= rank) return bin;
        }
        return kOverflowBin;
    }
};

enum class FrameProfileStage : std::size_t {
    simulation, simulation_tick, residency, record_total,
    cull, list, car, track, combine,
    update_total, update_validation, uniform_write,
    present_total, acquire, fence, command_record, finish_list, submit, present_other,
    mallinfo, parent_sum, other, road_reflections, race_visuals, count
};

struct FrameProfileIdentity {
    std::uint64_t tick = 0;
    std::uint64_t debt = 0;
    std::uint64_t triangles = 0;
    std::uint32_t track_draws = 0;
    std::uint32_t car_draws = 0;
};

class FrameProfileStats {
public:
    static constexpr std::size_t kStages = static_cast<std::size_t>(FrameProfileStage::count);
    using Values = std::array<double, kStages>;
private:
    std::array<ProfileDistribution, kStages> stages_{};
    FrameProfileIdentity worst_{};
    double worst_ms_ = 0;
    std::uint64_t count_ = 0;
    std::uint32_t catch_up_max_ = 0;
    std::uint64_t debt_max_ = 0;
public:
    void reset() { *this = {}; }
    void add_simulation_tick(double ms) {
        stages_[static_cast<std::size_t>(FrameProfileStage::simulation_tick)].add(ms);
    }
    void add_frame(const Values& values, double frame_ms, FrameProfileIdentity identity,
                   std::uint32_t catch_up) {
        for (std::size_t i = 0; i < kStages; ++i) {
            if (i != static_cast<std::size_t>(FrameProfileStage::simulation_tick))
                stages_[i].add(values[i]);
        }
        ++count_;
        if (frame_ms > worst_ms_) { worst_ms_ = frame_ms; worst_ = identity; }
        if (catch_up > catch_up_max_) catch_up_max_ = catch_up;
        if (identity.debt > debt_max_) debt_max_ = identity.debt;
    }
    const ProfileDistribution& stage(FrameProfileStage stage) const {
        return stages_[static_cast<std::size_t>(stage)];
    }
    std::uint64_t count() const { return count_; }
    const FrameProfileIdentity& worst() const { return worst_; }
    double worst_ms() const { return worst_ms_; }
    std::uint32_t catch_up_max() const { return catch_up_max_; }
    std::uint64_t debt_max() const { return debt_max_; }
};

} // namespace nfsu
