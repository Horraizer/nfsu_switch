#pragma once
#include <algorithm>
#include <cstdint>

namespace nfsu {
// Observed allocator snapshots, never a claim to intercept every allocation.
// The caller supplies real in-use/extent bytes; no container-size estimate.
struct HeapUsage {
    std::uint64_t before_load = 0, after_load = 0, current = 0;
    std::uint64_t load_peak = 0, peak = 0, extent = 0, extent_peak = 0;
    bool loading = true;
    void begin(std::uint64_t used, std::uint64_t arena) {
        *this = {};
        before_load = used;
        observe(used, arena);
    }
    void observe(std::uint64_t used, std::uint64_t arena) {
        current = used; extent = arena;
        peak = std::max(peak, used);
        extent_peak = std::max(extent_peak, arena);
        if (loading) load_peak = std::max(load_peak, used);
    }
    void finish_load(std::uint64_t used, std::uint64_t arena) {
        observe(used, arena);
        after_load = used;
        loading = false;
    }
    std::uint64_t load_delta() const {
        return load_peak > before_load ? load_peak - before_load : 0;
    }
    std::uint64_t load_overrun(std::uint64_t budget) const {
        const auto delta = load_delta();
        return delta > budget ? delta - budget : 0;
    }
};
} // namespace nfsu