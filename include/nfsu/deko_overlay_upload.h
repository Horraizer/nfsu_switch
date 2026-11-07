#pragma once
#include <algorithm>
#include <cstdint>
namespace nfsu {
// A single fenced stripe reuses at most1MiB. No second atlas-sized GPU block.
struct DekoOverlayStripe {
    std::uint32_t first_row=0,rows=0,row_pitch=0,bytes=0;
};
inline DekoOverlayStripe deko_overlay_stripe(std::uint32_t width,std::uint32_t height,
                                           std::uint32_t first_row) noexcept {
    if(!width||!height||width>4096||height>4096||first_row>=height)return {};
    const auto rows=std::min(64U,height-first_row);
    return {first_row,rows,width*4,width*4*rows};
}
}
