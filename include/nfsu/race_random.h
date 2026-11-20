#pragma once
#include <cstdint>

namespace nfsu {
// Original 4216B0. A draw returns the old state's remainder.
inline std::uint32_t race_random_index(std::uint32_t& state,unsigned bound) noexcept {
    const auto index=bound?state%bound:0U;
    const auto mixed=state^0x1d872b41U;
    const auto shifted=(mixed>>5)^mixed;
    state=(shifted<<27)^shifted^mixed;
    return index;
}
}
