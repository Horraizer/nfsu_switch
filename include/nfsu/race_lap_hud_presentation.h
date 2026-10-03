#pragma once

#include "nfsu/race_lap_hud.h"
#include <vector>

namespace nfsu {

struct RaceLapHudPermissions {
    bool known = false;
    std::uint32_t flags = 0;
};
// Copies only the recovered lap option. Other profile options are separate inputs.
RaceLapHudPermissions copy_lap_hud_permission(RaceLapHudPermissions settings,
    bool profile_known, unsigned char profile_option) noexcept;
bool toggle_lap_hud_permission(RaceLapHudPermissions& settings,
    RaceLapHudPermissions& view, unsigned char& profile_option) noexcept;
struct RaceLapHudVisibility {
    bool known = false, enabled = false;
    std::uint32_t component = 0;
};
// The caller establishes the non-permission mask predicates/lifecycle. This
// adapter does not interpret an arbitrary stage as a visible event0 HUD.
RaceLapHudVisibility lap_hud_visibility(RaceLapHudPermissions view,
    bool predicates_known, bool lap_path_allowed) noexcept;

struct RaceLapHudKerning {
    std::uint16_t previous = 0;
    int adjustment = 0;
};
struct RaceLapHudGlyph {
    std::uint16_t code = 0, x = 0, y = 0;
    unsigned char width = 0, height = 0;
    int bearing_x = 0, bearing_y = 0, advance = 0;
    std::vector<RaceLapHudKerning> kerning;
};
struct RaceLapHudFont {
    bool known = false;
    std::uint32_t texture_hash = 0;
    float line_height = 0, baseline = 0, line_spacing_scale = 0;
    std::vector<RaceLapHudGlyph> glyphs;
};
// Decode the explicit FNTF glyph records. Runtime metrics/texture binding are
// supplied after font loading/override resolution, never inferred from a name.
bool decode_lap_hud_fntf(const unsigned char* bytes, std::size_t size,
    std::uint32_t texture_hash, float line_height, float baseline,
    float line_spacing_scale, RaceLapHudFont& font);

struct RaceLapHudTexture {
    bool known = false;
    std::uint32_t hash = 0;
    unsigned width = 0, height = 0;
};
struct RaceLapHudTransform {
    bool known = false;
    float x = 0, y = 0, depth = 0, sx = 0, sy = 0;
    // Raw FEng SA channels, before parent multiplication and halving.
    std::array<unsigned, 4> bgra{};
    // Explicit SA quaternion. Only the recovered no-rotation case is supported.
    std::array<float, 4> rotation{};
};
struct RaceLapHudParent {
    bool known = false;
    // Explicit effective column-major affine 2D matrix, including depth.
    std::array<float, 16> matrix{};
    std::array<unsigned, 4> bgra{};
};
struct RaceLapHudGeometryInput {
    RaceLapHudTransform sa{};
    RaceLapHudParent parent{};
    RaceLapHudTexture texture{};
    bool visibility_known = false, visible = false;
    bool layout_known = false;
    std::uint32_t sj = 0, sl = 0, sw = 0;
    // Explicit sprite SA UV bounds. Text uses FNTF atlas coordinates.
    std::array<float, 4> uv{};
};
struct RaceLapHudVertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{}, inset_uv{};
    std::uint32_t color = 0; // Packed submission ARGB, not pixel opacity.
};
struct RaceLapHudQuad { std::array<RaceLapHudVertex, 4> vertices{}; };
struct RaceLapHudGeometry {
    bool known = false;
    bool presentation_ready = false; // Logical output never opens production.
    unsigned bucket = 0;
    std::vector<RaceLapHudQuad> quads;
};
// Bounded single-line ASCII English layout: Sl=Sw=0 and baseline=0.
// Unsupported layout, missing glyph/texture/parent/visibility clears output.
// An absent padding space is skipped, as in the recovered LCD numeric case.
// Control markers, unproved Sj bits and active nonzero kerning are refused.
bool build_lap_hud_text_geometry(const RaceLapHudGeometryInput& input,
    const RaceLapHudFont& font, const char* text, RaceLapHudGeometry& output);
bool build_lap_hud_backing_geometry(const RaceLapHudGeometryInput& input,
    RaceLapHudGeometry& output);
// Descending clamped/truncated SA+parent buckets; stable ties preserve the
// caller's established traversal. Unknown inputs cannot be silently dropped.
bool order_lap_hud_geometry(const std::vector<RaceLapHudGeometry>& objects,
    std::vector<std::size_t>& order);

} // namespace nfsu
