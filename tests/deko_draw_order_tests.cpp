#include "nfsu/deko_draw_order.h"
#include "nfsu/deko_gpfifo_budget.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    if (nfsu::deko_textured_uses_cutout_shader(false, 0) ||
        !nfsu::deko_textured_uses_cutout_shader(true, 0) ||
        !nfsu::deko_textured_uses_cutout_shader(false, 17) ||
        !nfsu::deko_textured_uses_cutout_shader(true, 17)) {
        std::cerr << "textured shader selection lost an alpha path\n";
        return 1;
    }
    constexpr std::array<bool, 8> opaque{{false, true, false, true, true, false, true, false}};
    std::vector<std::size_t> order;
    nfsu::deko_visit_opaque_first(opaque.size(),
        [&](std::size_t i) { return opaque[i]; },
        [&](std::size_t i) { order.push_back(i); });
    if (order != std::vector<std::size_t>{1, 3, 4, 6, 0, 2, 5, 7}) {
        std::cerr << "opaque-first order lost or permuted a selected draw\n";
        return 1;
    }
    for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{64}, std::size_t{65}, std::size_t{2047}}) {
        order.clear();
        nfsu::deko_visit_opaque_first(count,
            [](std::size_t i) { return i % 3 == 0; },
            [&](std::size_t i) { order.push_back(i); });
        if (order.size() != count) return 1;
        std::vector<bool> seen(count);
        bool nonopaque = false;
        for (std::size_t i : order) {
            if (i >= count || seen[i] || (nonopaque && i % 3 == 0)) return 1;
            seen[i] = true;
            if (i % 3 != 0) nonopaque = true;
        }
        const auto chunks = (count + nfsu::kDekoTrackChunkDraws - 1) / nfsu::kDekoTrackChunkDraws;
        if (chunks != nfsu::deko_track_list_calls(static_cast<std::uint32_t>(count))) return 1;
    }
    return 0;
}
