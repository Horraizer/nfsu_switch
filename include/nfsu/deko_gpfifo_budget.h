#pragma once

#include <cstdint>
#include <cstddef>

namespace nfsu {
// libnx allocates 0x800 GPFIFO entries. Each deko3d callList splits the
// caller and submits the child, so budget two entries per child plus 16 for
// the root, fencing and presentation. This is a conservative frame bound.
constexpr std::uint32_t kDekoGpfifoRingEntries = 2048;
constexpr std::uint32_t kDekoTrackChunkDraws = 64;
constexpr std::uint32_t kDekoPreparedDrawCapacity = 8192;
// Persistent template counts are independent of this frame's visible draws.
constexpr std::size_t kDekoStaticInstanceCapacity = 65536;
// Each template has one unique group ID in the existing sparse slot table.
constexpr std::size_t kDekoStaticGroupCapacity = 65536;
constexpr bool deko_static_package_fits(std::size_t instances,std::size_t definitions,
                                        std::size_t dynamic,std::size_t capacity) {
    return instances<=kDekoStaticInstanceCapacity && definitions<=kDekoStaticGroupCapacity && dynamic<=capacity;
}
constexpr std::uint32_t deko_track_list_calls(std::uint32_t track_draws) {
    return (track_draws + kDekoTrackChunkDraws - 1) / kDekoTrackChunkDraws;
}
constexpr std::uint32_t deko_frame_gpfifo_bound(std::uint32_t list_calls) {
    return 16 + 2 * list_calls;
}
static_assert(deko_frame_gpfifo_bound(deko_track_list_calls(kDekoPreparedDrawCapacity)) < kDekoGpfifoRingEntries);
} // namespace nfsu
