#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace nfsu {

using MapPoint = std::array<float, 2>;
// Metadata only. No texture pixels, GPU binding, sampler or display default.
struct MinimapResource {
    bool valid = false;
    std::uint32_t hash = 0;
    unsigned track=0; //Set by the authenticated loader; zero retains legacy synthetic fixtures.
    std::uint32_t width = 0, height = 0;
    std::uint64_t info_offset = 0, payload_offset = 0;
    std::uint32_t payload_bytes = 0;
    std::uint8_t alpha_test = 0, alpha_blend = 0;
    float offset_x = 0, offset_y = 0, world_span = 0;
    std::uint16_t rotation = 0, start_angle = 0, finish_angle = 0;
};
bool load_minimap_resource(const std::filesystem::path& retail,
                           MinimapResource& out, std::string& error, unsigned track=1003);
// 0x00564B10 polynomial, not libm sine. Rounded to float at native stores.
float minimap_scalar_sine(std::uint16_t angle) noexcept;
bool project_minimap_world(const MinimapResource& resource, MapPoint raw_xy,
                           MapPoint& uv) noexcept;

// All values are explicit, sourced by the caller from native style/camera
// evidence. Only the measured branches 1,3,5,6 are supported in this stage.
struct MinimapStyle {
    int index = -1;
    std::uint16_t tilt = 0;
    float distance = 0;
    MapPoint offset{}, extent_add{}, uv_extent{};
    float route_alpha = 0;
    bool centered = false, heading_aware = false, enabled = false;
};
struct MinimapPresentation {
    bool valid = false;
    MapPoint native_center{}, native_half_extent{};
    // Explicit affine transform AFTER native clipping; no inferred Switch fit.
    std::array<float, 6> screen_transform{};
    bool camera_angle_valid = false;
    std::uint16_t map_angle = 0;
};
struct MinimapActor {
    std::int16_t id = 0;
    MapPoint raw_xy{};
    std::uint16_t heading = 0;
    bool raw_gates_valid = false;
    std::uint8_t gate_4d0 = 0, gate_8a9 = 0;
    // Color and kind are native consumer inputs, not guessed by identity.
    std::array<std::uint8_t, 3> color{};
    std::uint32_t kind = 0;
    std::uint32_t texture_hash = 0;
    float size_x = 0, size_y = 0;
};
struct MinimapVertex {
    MapPoint screen{};
    std::array<float, 3> uvq{};
    std::array<std::uint8_t, 4> color{};
};
constexpr std::size_t kMinimapMaxActors = 16;
constexpr std::size_t kMinimapMaxPieces = 128;
struct MinimapPiece {
    std::array<MinimapVertex, 4> vertices{};
    std::int16_t actor_id = 0;
    std::uint32_t kind = 0;
    std::uint32_t texture_hash = 0;
    bool route = false;
};
struct MinimapGeometry {
    MapPoint center{};
    std::array<MapPoint, 4> boundary{};
    std::array<MinimapPiece, kMinimapMaxPieces> pieces{};
    std::size_t count = 0;
};
enum class MinimapResult { ok, invalid_resource, unknown_input,
                           invalid_input, capacity_exceeded };
// Atomic bounded output: failures clear out. Non-finite derived geometry is
// invalid_input, distinct from capacity_exceeded. No simulation, camera filtering,
// eligibility defaults, roster synthesis or HUD rendering side effects.
MinimapResult build_minimap_geometry(const MinimapResource& resource,
    const MinimapStyle& style, const MinimapPresentation& presentation,
    MapPoint view_raw_xy, const MinimapActor* actors, std::size_t actor_count,
    MinimapGeometry& out, std::size_t piece_capacity = kMinimapMaxPieces) noexcept;

} // namespace nfsu
