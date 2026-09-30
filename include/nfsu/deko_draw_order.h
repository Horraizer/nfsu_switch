#pragma once

#include <cstddef>
#include <cstdint>

namespace nfsu {

// Keep the selected command sequence intact while submitting depth-writing
// opaque draws before alpha-tested and blended draws. Each partition is stable.
template <class IsOpaque, class Visit>
void deko_visit_opaque_first(std::size_t count, IsOpaque is_opaque, Visit visit) {
    for (unsigned pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < count; ++i)
            if (is_opaque(i) == (pass == 0)) visit(i);
}

// The regular textured shader has no discard. Material alpha tests use the
// fixed-function comparison, while the cutout shader retains the frame's
// diagnostic alpha cutoff. No blend-count assumption enters this selection.
inline bool deko_textured_uses_cutout_shader(bool alpha_tested,
                                            std::uint32_t frame_alpha_cutoff) {
    return alpha_tested || frame_alpha_cutoff != 0;
}

} // namespace nfsu
