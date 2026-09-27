#pragma once

#include "nfsu/draw_commands.h"

namespace nfsu {

// SDK-free fields for one depth-1 RGBA8/BC1/BC2 buffer-to-image copy. Both pitch
// and layer stride are BYTES, never texels or rows. layer_stride_bytes is
// zero because this executor copies no second layer. Padding is optional
// for deko3d's copy engine; this executor retains 32-byte padded rows.
struct TextureCopyPlan {
    std::uint32_t row_pitch_bytes = 0;
    std::uint32_t layer_stride_bytes = 0;
    std::uint64_t staging_bytes = 0;
    bool padded = false;
};
bool plan_texture_copy(const UploadResource& resource, TextureCopyPlan& plan,
                       std::string& error);

// Neutral size/limit checks before device creation. Image layout and the
// sum of device allocations still require the backend's real SDK query.
constexpr std::uint32_t kDrawDescriptorBytes = 32;
struct DrawExecutionPlan {
    std::uint64_t staging_bytes = 0;
    std::uint32_t uniform_bytes = 0;
    std::uint32_t descriptor_bytes = 0;
    std::uint32_t sampler_offset = 0;
    std::uint32_t command_bytes = 0;
};
// Refusal leaves plan zeroed. Borrowed payloads have the same lifetime as
// validate_recorded_commands requires. Supports the stock 1280x720 view.
bool plan_draw_execution(const RecordedCommands& commands, DrawExecutionPlan& plan,
                         std::string& error);

// An update may change frame/draw uniforms and selection, but cannot replace
// uploaded payloads. Caller keeps both payload owners and next commands alive.
bool validate_draw_update(const UploadTable& uploaded, const RecordedCommands& next,
                          std::size_t draw_capacity, std::string& error);

} // namespace nfsu
