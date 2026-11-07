#pragma once
#include "nfsu/stock_view.h"

// Keep logical race controls and existing recordings stable. Swap only the
// physical buttons on live Switch input: A is NOS, L is drag downshift.
inline std::uint32_t switch_race_buttons(std::uint64_t buttons) noexcept {
    using namespace nfsu;
    auto mapped=buttons&~(kNpadButtonA|kNpadButtonL);
    if(buttons&kNpadButtonA)mapped|=kNpadButtonL;
    if(buttons&kNpadButtonL)mapped|=kNpadButtonA;
    return gamepad_buttons_from_npad(mapped);
}
