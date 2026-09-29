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
    // alpha.w carries PreparedInstance::lamp_mode (0..3).
    float alpha[4] = {1, 0, 0, 0};
    float environment_min[4]{};
    float environment_range[4]{};
    float environment_color[4] = {0, 0, 0, 1};
    CarShadingMaterial surface;
};

struct RecordedDraw {
    // Stable diagnostic identity; source group plus source instance, never
    // the position in the sorted list. No source mesh name is required.
    std::string name;
    std::size_t group = 0;
    std::size_t mesh = 0;
    std::size_t instance = kNoHandle;
    // Indices in RecordedCommands::resource_table().resources.
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

// A selected prevalidated definition plus placement/material references.
// Diagnostic names are formed only on demand, outside the render hot path.
struct RecordedDrawView {
    const RecordedDraw& definition;
    std::size_t instance;
    const DrawInstanceUniform& placement;
    const DrawMaterialUniform& material;
    bool track_instance = false;
    std::string diagnostic_name() const;
};

// Immutable resource/definition owner; selection and animated constants may
// change only between frames. A generation invalidates earlier frame views.
class PreparedCommandSource {
public:
    virtual ~PreparedCommandSource() = default;
    virtual const UploadTable& resource_table() const = 0;
    virtual std::size_t selected_draw_count() const = 0;
    virtual RecordedDrawView selected_draw(std::size_t i) const = 0;
    virtual std::uint64_t command_generation() const = 0;
    // Immutable package enumerated once at renderer open. Track selections
    // refer to these definitions and placements by stable IDs.
    virtual std::size_t static_track_draw_count() const = 0;
    virtual std::size_t static_track_instance_count() const = 0;
    virtual const DrawInstanceUniform& static_track_instance(std::size_t i) const = 0;
    virtual std::size_t static_track_definition_count() const = 0;
    virtual const RecordedDraw& static_track_definition(std::size_t i) const = 0;
};

struct RecordedCommands {
private:
    // Borrowed source bytes: the scene must remain alive and unchanged.
    // Pass this table directly to UploadLedger::upload_table. Returned
    // handles have the same positions as the resource references above.
    UploadTable uploads;
public:
    DrawFrameUniform frame;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    unsigned char clear_color[4]{};
private:
    std::vector<RecordedDraw> draws;
public:
    // Construction and owning-reference inspection only. These throw on a
    // borrowed frame, so an empty backing container can never look valid.
    UploadTable& owning_uploads();
    const UploadTable& owning_uploads() const;
    std::vector<RecordedDraw>& owning_draws();
    const std::vector<RecordedDraw>& owning_draws() const;

    // Consumers use these accessors for both owning reference commands and
    // borrowed race frames. The owner must outlive the frame; a race frame
    // expires on the next optimized frame, before any GPU submission.
    const UploadTable& resource_table() const;
    std::size_t draw_count() const;
    RecordedDrawView draw_view(std::size_t i) const;
    bool generation_valid() const;
    bool borrowed() const noexcept { return source_ != nullptr; }
    const PreparedCommandSource* package_identity() const noexcept { return source_; }
    std::size_t static_track_draw_count() const;
    std::size_t static_track_instance_count() const;
    const DrawInstanceUniform& static_track_instance(std::size_t i) const;
    std::size_t static_track_definition_count() const;
    const RecordedDraw& static_track_definition(std::size_t i) const;
private:
    friend class RaceGpuScene;
    const PreparedCommandSource* source_ = nullptr;
    std::uint64_t generation_ = 0;
};

// Camera/viewport only; no resources, draw lists or index validation.
bool record_frame_state(const RenderOptions& options, const RenderLimits& limits,
                        RecordedCommands& out, std::string& error);

// No deko3d or platform calls. Output is empty on any refusal. Scene and
// list validation precede recording; no visibility selection happens here.
// Nonempty drawable ranges only, one command per placed copy. The car uses
// prepared_draw_order and prepared_mesh_instances, including retail order.
// Track retail state selects texture-info alpha/blend state and preserves
// the order of the supplied draw list.
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
// Material at offset + (2+2*i)*256. Payload sizes are 112, 64 and 192 bytes;
// padding is not read. Failure leaves allocation and arena unchanged.
bool reserve_draw_uniforms(const RecordedCommands& commands, MemoryArena& arena,
                           ArenaAllocation& allocation, std::string& error);

// Optional per-thread research/test audit. The caller owns the counter and
// restores the previous pointer before it dies. No counter is enabled by
// the race application; validation still performs all original checks.
struct DrawValidationAudit { std::uint64_t index_visits = 0; };
DrawValidationAudit* set_draw_validation_audit(DrawValidationAudit* audit) noexcept;

// Check borrowed payloads, manifest identity, ranges and state before a
// hardware backend dereferences a command. No SDK dependency or GPU work.
bool validate_recorded_commands(const RecordedCommands& commands, std::string& error);

} // namespace nfsu
