#pragma once
#include <cstdint>

namespace nfsu {
enum class SplitLayout { stacked, side_by_side };
struct SplitViewport {
    std::uint32_t x, y, width, height;
};

constexpr SplitViewport split_viewport(std::uint32_t width,std::uint32_t height,
                                       SplitLayout layout,unsigned player) noexcept {
    if(layout==SplitLayout::side_by_side) {
        const auto half=width/2;
        return {player*half,0,player?width-half:half,height};
    }
    const auto half=height/2;
    return {0,player*half,width,player?height-half:half};
}
} // namespace nfsu
