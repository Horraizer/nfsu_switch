#pragma once

#include "nfsu/gpu_upload.h"
#include "nfsu/render.h"
#include "nfsu/shader_manifest.h"

namespace nfsu {

// std140 payloads for the blocks in every shaders/*.manifest. These are
// values, not GPU allocations. reserve_draw_uniforms places their aligned
// slots in an existing arena; the platform copies the values to those slots.
struct DrawFrameUniform {
    float eye[4]{};
    float right[4]{};
    float up[4]{};
    float forward[4]{};
    float projection[4]{};
    float light[4]{};
    std::uint32_t options[4]{};
};
struct DrawInstanceUniform {
    float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                      0, 0, 1, 0, 0, 0, 0, 1};
};
struct DrawMaterialUniform {
    float fallback[4]{};
    float alpha[4] = {1, 0, 0, 0};
    float environment_min[4]{};
    float environment_range[4]{};
    float environment_color[4] = {0, 0, 0, 1};
};

struct RecordedDraw {
    // Stable diagnostic identity; source group plus source instance, never
    // the position in the sorted list. No source mesh name is required.
    std::string name;
    std::size_t group = 0;
    std::size_t mesh = 0;
    std::size_t instance = kNoHandle;
    // Indices in RecordedCommands::uploads.resources, not separate uploads.
    std::size_t vertex_resource = kNoHandle;
    std::size_t index_resource = kNoHandle;
    std::size_t texture_resource = kNoHandle;
    // Whole mesh vertex buffer, triangle-list u16 indices, base vertex zero.
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    // Static generated manifest; includes the vertex layout and sampler.
    const shader_manifest::Program* program = nullptr;
    // The named manifest variant, with the explicit RenderOptions cull
    // override. All other fixed-function state comes from the manifest.
    shader_manifest::Variant pipeline{};
    DrawInstanceUniform placement;
    DrawMaterialUniform material;
};

struct RecordedCommands {
    // Borrowed source bytes: the scene must remain alive and unchanged.
    // Pass this table directly to UploadLedger::upload_table. Returned
    // handles have the same positions as the resource references above.
    UploadTable uploads;
    DrawFrameUniform frame;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    unsigned char clear_color[4]{};
    std::vector<RecordedDraw> draws;
};

// No deko3d or platform calls. Output is empty on any refusal. Scene and
// list validation precede recording; no visibility selection happens here.
// Nonempty drawable ranges only, one command per placed copy. The car uses
// prepared_draw_order and prepared_mesh_instances, including retail order.
// Track retail state is refused, exactly as the CPU track renderer does.
// Bad car vertex indices are refused (the CPU can skip them, a GPU cannot).
// Limits bound dimensions, source/submitted triangles; raster/pixel limits
// belong to rasterization and do not constrain a command recording.
bool record_car_commands(const PreparedCarScene& scene,
                         const RenderOptions& options,
                         const RenderLimits& limits,
                         std::size_t generated_texture_source,
                         RecordedCommands& commands, std::string& error);
bool record_track_commands(const PreparedTrackScene& scene,
                           const TrackDrawList& list,
                           const RenderOptions& options,
                           const RenderLimits& limits,
                           RecordedCommands& commands, std::string& error);

// One arena allocation, released by the caller after its last-use fence.
// Frame at allocation.offset; draw i's Instance at offset + (1+2*i)*256,
// Material at offset + (2+2*i)*256. Payload sizes are 112, 64 and 80 bytes;
// padding is not read. Failure leaves allocation and arena unchanged.
bool reserve_draw_uniforms(const RecordedCommands& commands, MemoryArena& arena,
                           ArenaAllocation& allocation, std::string& error);

// Check borrowed payloads, manifest identity, ranges and state before a
// hardware backend dereferences a command. No SDK dependency or GPU work.
bool validate_recorded_commands(const RecordedCommands& commands, std::string& error);

} // namespace nfsu
