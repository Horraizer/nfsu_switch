#pragma once

#include <cstdint>

namespace nfsu {
// libnx allocates 0x800 GPFIFO entries. Each deko3d callList splits the
// caller and submits the child, so budget two entries per child plus 16 for
// the root, fencing and presentation. This is a conservative frame bound.
constexpr std::uint32_t kDekoGpfifoRingEntries = 2048;
constexpr std::uint32_t kDekoTrackChunkDraws = 64;
constexpr std::uint32_t deko_track_list_calls(std::uint32_t track_draws) {
    return (track_draws + kDekoTrackChunkDraws - 1) / kDekoTrackChunkDraws;
}
constexpr std::uint32_t deko_frame_gpfifo_bound(std::uint32_t list_calls) {
    return 16 + 2 * list_calls;
}
static_assert(deko_frame_gpfifo_bound(deko_track_list_calls(2047)) < kDekoGpfifoRingEntries);
} // namespace nfsu
