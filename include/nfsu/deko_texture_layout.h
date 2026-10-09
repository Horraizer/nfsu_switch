#pragma once

#include "nfsu/gpu_upload.h"

namespace nfsu {

// Validated RGBA8/BC1/BC2 textures. deko3d 0.5.0
// automatically picks two GOBs for six-to-eight physical rows, while its
// allocation clamps that level to one GOB. Base-level transfers retain the
// unadjusted height and can write into the next image. Use the public custom
// tile flag for this case and mip chains (whose small levels reach it);
// keep the automatic layout for other single-level images.
inline bool deko_texture_needs_one_gob(const TextureUploadShape& shape) noexcept {
    const auto rows = shape.rows();
    return shape.mip_count>1 || (rows >= 6 && rows <= 8);
}

} // namespace nfsu
