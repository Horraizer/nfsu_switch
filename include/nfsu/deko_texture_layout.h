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

// Match the custom one-GOB path above when bounding resident image memory.
// The general bound rounds every mip to128 rows; that is deliberately loose
// for unknown tile modes, but greatly overstates these known one-GOB tails.
// Runtime allocations still use SDK sizes and validate against this bound.
inline bool deko_texture_image_bound(const UploadResource& texture,
    std::uint64_t& bytes,std::uint64_t& alignment,std::string& error) {
    if(!bounded_image_layout(texture,bytes,alignment,error))return false;
    if(!deko_texture_needs_one_gob(texture.texture))return true;
    bytes=0;
    for(std::uint32_t level=0;level<texture.texture.mip_count;++level) {
        TextureUploadShape item;std::uint64_t offset=0;
        if(!texture_upload_mip(texture.texture,level,item,offset,error)) {
            bytes=alignment=0;return false;
        }
        bytes+=((item.row_bytes+63)&~UINT64_C(63))*((item.rows()+7)&~UINT64_C(7));
    }
    return true;
}

} // namespace nfsu
