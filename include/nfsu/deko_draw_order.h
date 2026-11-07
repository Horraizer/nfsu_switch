#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include <vector>

namespace nfsu {

// Keep the selected command sequence intact while submitting depth-writing
// opaque draws before alpha-tested and blended draws. Each partition is stable.
template <class IsOpaque, class Visit>
void deko_visit_opaque_first(std::size_t count, IsOpaque is_opaque, Visit visit) {
    for (unsigned pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < count; ++i)
            if (is_opaque(i) == (pass == 0)) visit(i);
}

// The race renderer's first pass: depth-writing opaque draws. The sky writes
// no depth and follows them, so their depth rejects every sky fragment they
// cover (issue 227). sky_with_opaque is the diagnostic that instead submits
// the sky in this pass at its list position, as the renderer did before.
inline bool deko_draw_in_opaque_pass(bool sky, bool blend, bool alpha_tested,
                                     bool depth_write, bool sky_with_opaque) {
    return sky ? sky_with_opaque : !blend && !alpha_tested && depth_write;
}

// Preserve the source index (uniform address) and order for every cube face.
// View 0 is shared by all faces; views 10..15 belong to one face. Group runs
// once instead of rescanning the entire cube draw list six times per frame.
using DekoCubeRanges = std::array<std::vector<std::pair<std::size_t,std::size_t>>,6>;
template<class View>
void deko_cube_ranges(std::size_t count, View view, DekoCubeRanges& ranges) {
    for (auto& face:ranges) face.clear();
    for (std::size_t first=0;first<count;) {
        const auto tag=view(first);
        auto end=first+1;
        while(end<count && view(end)==tag) ++end;
        if(tag==0) for(auto& face:ranges) face.emplace_back(first,end);
        else if(tag>=10 && tag<16) ranges[tag-10].emplace_back(first,end);
        first=end;
    }
}

// The regular textured shader has no discard. Material alpha tests use the
// fixed-function comparison, while the cutout shader retains the frame's
// diagnostic alpha cutoff. No blend-count assumption enters this selection.
inline bool deko_textured_uses_cutout_shader(bool alpha_tested,
                                            std::uint32_t frame_alpha_cutoff) {
    return alpha_tested || frame_alpha_cutoff != 0;
}

} // namespace nfsu
