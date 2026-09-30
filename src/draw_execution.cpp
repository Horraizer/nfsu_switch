#include "nfsu/draw_execution.h"
#include "nfsu/memory_budget.h"

namespace nfsu {

bool plan_texture_copy(const UploadResource& r, TextureCopyPlan& plan, std::string& error) {
    plan = {}; error.clear();
    if (r.kind != UploadResourceKind::texture || !r.data) {
        error = r.name + ": texture copy requires a texture payload"; return false;
    }
    TextureUploadShape shape;
    if (!check_texture_upload_format(r.texture.format, r.texture.width,
            r.texture.height, r.bytes, shape, error)) return false;
    if (r.texture.row_bytes != shape.row_bytes || r.texture.bytes != shape.bytes) {
        error = r.name + ": invalid texture shape"; return false;
    }
    // Shape validation bounds the product to kMaxUploadResourceBytes.
    // Adding at most 31 bytes per row remains safe in uint64_t.
    const auto pitch = (shape.row_bytes + 31) & ~std::uint64_t{31};
    const auto bytes = pitch * shape.rows();
    if (pitch > kMaxUploadResourceBytes || bytes > kMaxUploadResourceBytes) {
        error = r.name + ": padded texture copy exceeds block limit"; return false;
    }
    plan.row_pitch_bytes = static_cast<std::uint32_t>(pitch);
    plan.layer_stride_bytes = 0;
    plan.staging_bytes = bytes;
    plan.padded = pitch != shape.row_bytes;
    return true;
}

bool plan_draw_execution(const RecordedCommands& c, DrawExecutionPlan& plan, std::string& error) {
    plan = {}; error.clear();
    if (!c.generation_valid()) { error = "expired command generation"; return false; }
    if (c.clear_color[3] != 255) {
        error = "deko3d executor requires opaque background alpha (255)"; return false;
    }
    if (c.width != 1280 || c.height != 720 || c.draw_count() > 4096 ||
        c.resource_table().resources.size() > 8192) {
        error = "deko3d stock executor: requires 1280x720 and at most 4096 draws and 8192 resources";
        return false;
    }
    const std::uint64_t uniform_bytes = (1 + 2 * c.draw_count()) * kDekoUniformBufferAlignment;
    if (uniform_bytes > kTransientBudgetPlaceholder) {
        error = "draw uniforms exceed the transient budget placeholder"; return false;
    }
    if (!validate_recorded_commands(c, error)) return false;
    DrawExecutionPlan out;
    out.uniform_bytes = static_cast<std::uint32_t>(uniform_bytes);
    out.descriptor_bytes = static_cast<std::uint32_t>((c.resource_table().resources.size() + 2) * kDrawDescriptorBytes);
    out.sampler_offset = static_cast<std::uint32_t>(c.resource_table().resources.size() * kDrawDescriptorBytes);
    out.command_bytes = static_cast<std::uint32_t>(65536 +
        (c.draw_count() + c.resource_table().resources.size()) * kDekoMemBlockAlignment);
    // Ledger's tight copies, optional padded copies, and at most 31 bytes
    // of alignment slack per allocation. Keep the original conservative
    // allowance of 32 bytes per resource, including non-textures.
    out.staging_bytes = c.resource_table().resources.size() * 32;
    for (const auto& r : c.resource_table().resources) {
        if (r.kind != UploadResourceKind::texture) { ++out.buffer_allocations; continue; }
        TextureCopyPlan copy;
        if (!plan_texture_copy(r, copy, error)) return false;
        out.staging_bytes += r.bytes;
        if (copy.padded) out.staging_bytes += copy.staging_bytes + 31;
    }
    if (out.staging_bytes > kMaxUploadResourceBytes) {
        error = "texture staging exceeds block limit"; return false;
    }
    if (!out.buffer_allocations) out.buffer_allocations = 1;
    plan = out;
    return true;
}

bool validate_draw_update(const UploadTable& uploaded, const RecordedCommands& next,
                          std::size_t draw_capacity, std::string& error) {
    error.clear();
    if (!next.generation_valid()) { error = "expired command generation"; return false; }
    if (next.draw_count() > draw_capacity) { error = "updated draws exceed reserved capacity"; return false; }
    if (uploaded.resources.size() != next.resource_table().resources.size()) {
        error = "updated upload table changed size"; return false;
    }
    for (std::size_t i = 0; i < uploaded.resources.size(); ++i) {
        const auto& a = uploaded.resources[i]; const auto& b = next.resource_table().resources[i];
        if (a.kind != b.kind || a.data != b.data || a.bytes != b.bytes ||
            a.element_bytes != b.element_bytes || a.elements != b.elements ||
            a.texture.format != b.texture.format || a.texture.width != b.texture.width ||
            a.texture.height != b.texture.height || a.texture.row_bytes != b.texture.row_bytes) {
            error = "updated payload differs from uploaded resource " + std::to_string(i); return false;
        }
    }
    DrawExecutionPlan plan;
    return plan_draw_execution(next, plan, error);
}

} // namespace nfsu
