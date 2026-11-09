#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace nfsu {

inline constexpr double kFpsGraphMaxMs = 1000.0 / 15;
inline constexpr std::array<double,2> kFpsGraphGuides{{1000.0 / 60,1000.0 / 30}};
inline constexpr double kFpsGraphLateMs = 25;

// Completed presentation intervals, oldest to newest; no allocation/readback.
struct RaceFpsHistory {
    std::array<double,90> intervals{};
    std::size_t count = 0;

    void add(double milliseconds) noexcept {
        if (!std::isfinite(milliseconds) || milliseconds <= 0) return;
        if (count == intervals.size()) {
            std::move(intervals.begin()+1, intervals.end(), intervals.begin());
            --count;
        }
        intervals[count++] = milliseconds;
    }
    double mean_ms() const noexcept {
        double sum=0;
        for (std::size_t i=0;i<count;++i) sum+=intervals[i];
        return count ? sum/count : 0;
    }
    void format(char* label, std::size_t size) const noexcept {
        if(count)std::snprintf(label,size,"%.1f FPS  %.1f ms",1000/mean_ms(),intervals[count-1]);
        else std::snprintf(label,size,"FPS ...");
    }
};

} // namespace nfsu
