#pragma once

#include <cstddef>

namespace nfsu {

// Keep the selected command sequence intact while submitting depth-writing
// opaque draws before alpha-tested and blended draws. Each partition is stable.
template <class IsOpaque, class Visit>
void deko_visit_opaque_first(std::size_t count, IsOpaque is_opaque, Visit visit) {
    for (unsigned pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < count; ++i)
            if (is_opaque(i) == (pass == 0)) visit(i);
}

} // namespace nfsu
