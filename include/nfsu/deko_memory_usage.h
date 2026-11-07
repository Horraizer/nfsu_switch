#pragma once

#include <cstdint>

namespace nfsu {

// An offscreen renderer has one command slot; an empty scene can have none.
// deko3d's getSize() requires an allocated handle.
template <class Block>
std::uint64_t deko_allocated_block_size(Block& block) noexcept {
    return block ? block.getSize() : 0;
}

} // namespace nfsu
