#include "nfsu/deko_draw_order.h"
#include "nfsu/deko_gpfifo_budget.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    // Whole-track template count is independent of selected per-frame draws.
    if (!nfsu::deko_static_package_fits(65536,65536,8192,8192) ||
        !nfsu::deko_static_package_fits(1,4097,1,1) ||
        nfsu::deko_static_package_fits(65537,1,1,1) ||
        nfsu::deko_static_package_fits(1,65537,1,1) ||
        nfsu::deko_static_package_fits(1,1,2,1)) {
        std::cerr << "static package bounds confused persistent templates and visible draws\n";
        return 1;
    }
    if (nfsu::deko_textured_uses_cutout_shader(false, 0) ||
        !nfsu::deko_textured_uses_cutout_shader(true, 0) ||
        !nfsu::deko_textured_uses_cutout_shader(false, 17) ||
        !nfsu::deko_textured_uses_cutout_shader(true, 17)) {
        std::cerr << "textured shader selection lost an alpha path\n";
        return 1;
    }
    // Arguments: sky, blend, alpha tested, depth write, sky with opaque.
    if (!nfsu::deko_draw_in_opaque_pass(false, false, false, true, false) ||
        nfsu::deko_draw_in_opaque_pass(false, true, false, true, false) ||
        nfsu::deko_draw_in_opaque_pass(false, false, true, true, false) ||
        nfsu::deko_draw_in_opaque_pass(false, false, false, false, false) ||
        nfsu::deko_draw_in_opaque_pass(true, false, false, false, false) ||
        nfsu::deko_draw_in_opaque_pass(true, false, false, true, false) ||
        !nfsu::deko_draw_in_opaque_pass(true, true, false, false, true) ||
        !nfsu::deko_draw_in_opaque_pass(false, false, false, true, true) ||
        nfsu::deko_draw_in_opaque_pass(false, true, false, false, true)) {
        std::cerr << "opaque pass admitted or lost a draw\n";
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
    nfsu::DekoCubeRanges ranges;
    for (const std::vector<unsigned> tags : {
            std::vector<unsigned>{}, {0}, {10,10,11,12,12,13,14,15},
            {10,0,11,9,15,0,10,99,14}, {15,14,13,12,11,10,0}}) {
        nfsu::deko_cube_ranges(tags.size(),[&](std::size_t i){return tags[i];},ranges);
        for(unsigned face=0;face<6;++face) {
            std::vector<std::size_t> reference,selected;
            for(std::size_t i=0;i<tags.size();++i)
                if(!tags[i] || tags[i]==face+10) reference.push_back(i);
            for(const auto& range:ranges[face])
                for(auto i=range.first;i<range.second;++i) selected.push_back(i);
            if(selected!=reference) {
                std::cerr<<"cube range changed shared/private draw order or uniform index\n";
                return 1;
            }
        }
    }
    // Six grouped race faces: one tag scan rather than six full-list scans.
    std::size_t lookups=0;
    nfsu::deko_cube_ranges(3072,[&](std::size_t i){++lookups;return 10+i/512;},ranges);
    if(lookups>3072+6) return 1;
    for(unsigned face=0;face<6;++face)
        if(ranges[face]!=std::vector<std::pair<std::size_t,std::size_t>>{{face*512,(face+1)*512}}) return 1;
    std::cout<<"PASS cube schedule matches six-pass reference; tag lookups="<<lookups<<" old=18432\n";
    return 0;
}
