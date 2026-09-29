#pragma once

#include <array>
#include <cstdint>

namespace nfsu {

struct RaceFrameSample {
    double frame_ms = 0;
    double simulation_ms = 0;
    double residency_ms = 0;
    double record_ms = 0;
    double update_ms = 0;
    double present_cpu_ms = 0;
    double acquire_ms = 0;
    double fence_ms = 0;
    double command_ms = 0;
    double submit_ms = 0;
    double draw_ms = 0;
    double transfer_ms = 0;
    double panel_ms = 0;
    unsigned catch_up = 0;
};

// Fixed-size, allocation-free accumulation. Bins are 0.25 ms wide through
// 100 ms; the last bin holds every larger sample. Percentiles use the upper
// bin edge, or report overflow to the caller.
class RaceFrameStats {
    static constexpr unsigned kBins = 400;
    std::array<std::uint64_t, kBins + 1> histogram_{};
    RaceFrameSample sum_{};
    std::uint64_t count_ = 0;
    std::uint64_t over_budget_ = 0;
    double max_ms_ = 0;
    unsigned catch_up_max_ = 0;
public:
    static constexpr double kBudgetMs = 1000.0 / 60.0;
    // Half a refresh interval separates normal timer jitter from a frame
    // that plausibly occupied a second display interval.
    static constexpr double kOverThresholdMs = 1.5 * kBudgetMs;
    static constexpr double kBinWidthMs = 0.25;
    static constexpr unsigned kOverflowBin = kBins;

    void reset() { *this = {}; }
    void add(const RaceFrameSample& sample) {
        if (!(sample.frame_ms >= 0)) return;
        const unsigned bin = sample.frame_ms >= 100.0
            ? kOverflowBin : static_cast<unsigned>(sample.frame_ms / kBinWidthMs);
        ++histogram_[bin];
        ++count_;
        if (sample.frame_ms > kOverThresholdMs) ++over_budget_;
        if (sample.frame_ms > max_ms_) max_ms_ = sample.frame_ms;
        if (sample.catch_up > catch_up_max_) catch_up_max_ = sample.catch_up;
        sum_.frame_ms += sample.frame_ms;
        sum_.simulation_ms += sample.simulation_ms;
        sum_.residency_ms += sample.residency_ms;
        sum_.record_ms += sample.record_ms;
        sum_.update_ms += sample.update_ms;
        sum_.present_cpu_ms += sample.present_cpu_ms;
        sum_.acquire_ms += sample.acquire_ms;
        sum_.fence_ms += sample.fence_ms;
        sum_.command_ms += sample.command_ms;
        sum_.submit_ms += sample.submit_ms;
        sum_.draw_ms += sample.draw_ms;
        sum_.transfer_ms += sample.transfer_ms;
        sum_.panel_ms += sample.panel_ms;
    }
    std::uint64_t count() const { return count_; }
    std::uint64_t over_budget() const { return over_budget_; }
    double max_ms() const { return max_ms_; }
    unsigned catch_up_max() const { return catch_up_max_; }
    RaceFrameSample mean() const {
        if (!count_) return {};
        const double scale = 1.0 / static_cast<double>(count_);
        RaceFrameSample out = sum_;
        out.frame_ms *= scale;
        out.simulation_ms *= scale;
        out.residency_ms *= scale;
        out.record_ms *= scale;
        out.update_ms *= scale;
        out.present_cpu_ms *= scale;
        out.acquire_ms *= scale;
        out.fence_ms *= scale;
        out.command_ms *= scale;
        out.submit_ms *= scale;
        out.draw_ms *= scale;
        out.transfer_ms *= scale;
        out.panel_ms *= scale;
        return out;
    }
    unsigned percentile_bin(unsigned percent) const {
        if (!count_ || percent == 0 || percent > 100) return kOverflowBin;
        const std::uint64_t rank = (count_/100)*percent + ((count_%100)*percent+99)/100;
        std::uint64_t seen = 0;
        for (unsigned bin = 0; bin <= kOverflowBin; ++bin) {
            seen += histogram_[bin];
            if (seen >= rank) return bin;
        }
        return kOverflowBin;
    }
};

} // namespace nfsu
