#pragma once
#include "nfsu/race_session.h"
#include <cstdio>

namespace nfsu {
// Bounded diagnostics only. Emits both physics substeps every sixth drive
// tick, up to 2400 rows. No owned strings/containers or per-row allocation.
class HandlingTelemetryLog {
public:
    explicit HandlingTelemetryLog(std::FILE* output = nullptr) : output_(output) {}
    bool write(const RaceSession& session, const GamepadFrame& pad,
               std::uint64_t consumed_us = 0, bool clock_available = false);
    std::size_t rows() const { return rows_; }
    static constexpr std::size_t kMaxRows = 2400;
private:
    std::FILE* output_;
    std::size_t rows_ = 0;
    std::uint64_t drive_ticks_ = 0;
    bool header_ = false;
};
} // namespace nfsu
