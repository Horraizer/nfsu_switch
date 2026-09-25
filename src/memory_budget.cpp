#include "nfsu/memory_budget.h"

#include <algorithm>
#include <cstddef>

namespace nfsu {

namespace {

std::uint64_t saturating_multiply(std::uint64_t a, std::uint64_t b) noexcept {
    if (a != 0 && b > UINT64_MAX / a) {
        return UINT64_MAX;
    }
    return a * b;
}

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

bool is_power_of_two(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

// What one role of the placement needs: its requests in table order, and an
// arena big enough for all of them whatever the order does to the gaps.
struct Role {
    std::uint64_t bytes = 0;
    std::uint64_t capacity = 0;
    std::uint64_t max_alignment = 1;
    std::uint32_t count = 0;
};

bool add_request(Role& role, std::uint64_t bytes, std::uint64_t alignment,
                 std::string& error) {
    if (bytes == 0 || !is_power_of_two(alignment) || alignment > kMaxArenaAlignment) {
        error = "a request of " + std::to_string(bytes) + " bytes at alignment " +
                std::to_string(alignment) + " cannot be placed";
        return false;
    }
    // Each placement starts at most alignment - 1 bytes past the end of the
    // one before, so this bounds the extent in any order.
    role.bytes = saturating_add(role.bytes, bytes);
    role.capacity = saturating_add(role.capacity, saturating_add(bytes, alignment - 1U));
    role.max_alignment = std::max(role.max_alignment, alignment);
    ++role.count;
    return true;
}

bool make_arena(const Role& role, MemoryCategory category, MemoryArena& arena,
                std::string& error) {
    if (role.count == 0) {
        return true;
    }
    if (role.capacity > kMaxArenaCapacity || role.count > kMaxArenaAllocations) {
        error = std::to_string(role.count) + " placements of " +
                std::to_string(role.bytes) + " bytes are more than one arena can model";
        return false;
    }
    ArenaConfig config;
    config.category = category;
    config.capacity = role.capacity;
    config.max_alignment = role.max_alignment;
    config.max_allocations = role.count;
    return arena.create(config, error);
}

} // namespace

std::uint64_t car_scene_upload_ceiling(const SceneLimits& limits) noexcept {
    return saturating_add(
        saturating_add(saturating_multiply(limits.max_vertices, sizeof(MeshVertex)),
                       saturating_multiply(limits.max_triangles, sizeof(MeshTriangle))),
        limits.max_texture_bytes);
}

std::uint64_t track_region_upload_ceiling(const TrackSceneLimits& limits) noexcept {
    return saturating_add(
        saturating_add(saturating_multiply(limits.max_vertices, sizeof(TrackVertex)),
                       saturating_multiply(limits.max_triangles, sizeof(MeshTriangle))),
        limits.max_texture_bytes);
}

std::uint64_t track_draw_list_frame_ceiling(const TrackDrawListLimits& limits) noexcept {
    return saturating_multiply(limits.max_entries, sizeof(TrackDrawListEntry));
}

std::uint64_t track_scene_frame_ceiling(const PreparedTrackScene& scene) noexcept {
    return saturating_multiply(scene.cost.resident_entries, sizeof(TrackDrawListEntry));
}

MemoryBudget switch_memory_budget() noexcept {
    MemoryBudget budget;
    const auto at = [](MemoryCategory category) {
        return static_cast<std::size_t>(category);
    };
    budget.capacity[at(MemoryCategory::persistent)] = kPersistentBudgetPlaceholder;
    budget.capacity[at(MemoryCategory::streaming)] =
        kResidentRegionsPlaceholder * kStreamingRegionBudgetPlaceholder;
    budget.capacity[at(MemoryCategory::transient)] = kTransientBudgetPlaceholder;
    budget.capacity[at(MemoryCategory::gpu)] = kGpuBudgetPlaceholder;
    budget.max_alignment[at(MemoryCategory::persistent)] = alignof(std::max_align_t);
    budget.max_alignment[at(MemoryCategory::streaming)] = alignof(std::max_align_t);
    budget.max_alignment[at(MemoryCategory::transient)] = alignof(std::max_align_t);
    budget.max_alignment[at(MemoryCategory::gpu)] = kDekoMemBlockAlignment;
    budget.max_allocations.fill(kArenaAllocationsPlaceholder);
    return budget;
}

bool upload_table_requirement(
    const UploadTable& table,
    const ImageLayoutQuery& image_layout,
    SceneMemoryRequirement& requirement,
    std::string& error) {
    requirement = SceneMemoryRequirement{};
    if (!image_layout) {
        error = "no image layout query";
        return false;
    }
    // Sized first, so each arena is created once and big enough; the layout
    // query is asked again when placing, and must answer the same.
    Role buffers;
    Role images;
    std::uint64_t staging_batch = 0;
    std::uint64_t staging_largest = 0;
    for (const UploadResource& resource : table.resources) {
        std::string refusal;
        if (resource.kind != UploadResourceKind::texture) {
            if (!add_request(buffers, resource.bytes, resource.alignment, refusal)) {
                error = resource.name + ": " + refusal;
                return false;
            }
            continue;
        }
        std::uint64_t bytes = 0;
        std::uint64_t alignment = 0;
        if (!image_layout(resource, bytes, alignment, refusal)) {
            error = resource.name + ": image layout refused: " + refusal;
            return false;
        }
        if (!add_request(images, bytes, alignment, refusal)) {
            error = resource.name + ": " + refusal;
            return false;
        }
        staging_batch = saturating_add(staging_batch, resource.bytes);
        staging_largest = std::max(staging_largest, resource.bytes);
    }

    MemoryArena buffer_arena;
    MemoryArena image_arena;
    if (!make_arena(buffers, MemoryCategory::gpu, buffer_arena, error) ||
        !make_arena(images, MemoryCategory::gpu, image_arena, error)) {
        return false;
    }
    for (const UploadResource& resource : table.resources) {
        const bool texture = resource.kind == UploadResourceKind::texture;
        std::uint64_t bytes = resource.bytes;
        std::uint64_t alignment = resource.alignment;
        std::string refusal;
        if (texture && !image_layout(resource, bytes, alignment, refusal)) {
            error = resource.name + ": image layout refused: " + refusal;
            return false;
        }
        ArenaAllocation placement;
        const ArenaRefusal placed =
            (texture ? image_arena : buffer_arena).allocate(bytes, alignment, placement);
        if (placed != ArenaRefusal::none) {
            error = resource.name + ": " + arena_refusal_name(placed) +
                    "; the layout query answered differently the second time";
            return false;
        }
    }
    requirement.buffer_bytes = buffers.bytes;
    requirement.buffer_extent = buffer_arena.stats().high_water_extent;
    requirement.image_bytes = images.bytes;
    requirement.image_extent = image_arena.stats().high_water_extent;
    requirement.staging_batch_bytes = staging_batch;
    requirement.staging_largest_bytes = staging_largest;
    return true;
}

} // namespace nfsu
