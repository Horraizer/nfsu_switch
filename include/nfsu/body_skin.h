#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Body-skin pixel compositor, recovered from the retail speed.exe
// (SHA-256 C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7,
// image base 0x00400000). Addresses below are its virtual addresses.
// See docs/BODY_SKIN.md for the full instruction evidence, and
// docs/CAR_SCENE.md for how the inputs are selected.
//
// The game has two composite routines, chosen at 0x005AA262 by the target
// texture object's byte +0x4A: value 8 uses 0x005A8E90, anything else uses
// 0x005A8A50, which itself returns early unless the byte is 0x20. This header
// keeps the two paths separate and calls them "type 8" and "type 32" after
// that byte. It does not name a pixel format for either: the traced code
// never names one, and the two paths disagree about byte-lane order (see
// kSkinMarkerSurface / kSkinMarkerPalette below).
//
// A packed color is treated by the game as four independent byte lanes.
// Lane 0 is the low byte of the little-endian u32, lane 3 is the high byte.
// This API numbers lanes rather than naming channels, because the two paths
// store the same color with lanes 0 and 2 exchanged. The caller at
// 0x005AA134..0x005AA19F builds a color from the part attributes named
// "GLOSS", "BLUE", "GREEN" and "RED", shifted so that lane 0 is RED, lane 1
// GREEN, lane 2 BLUE and lane 3 GLOSS. That is the order every field of
// BodySkinRequest and BodySkinLayer uses, and the order the type-8 path
// writes into its palette. The type-32 path exchanges lanes 0 and 2 first,
// so its surface holds (GLOSS<<24) | (RED<<16) | (GREEN<<8) | BLUE. Nothing
// proves a pixel-format name, and nothing proves that GLOSS is an alpha
// channel.

// ---------------------------------------------------------------------------
// Shared arithmetic
// ---------------------------------------------------------------------------

// 0x006CC970, the float32 multiplied into every 0..255 lane value to make a
// blend weight, and the exclusive lower bound a weight must pass at
// 0x005A8681 to contribute at all.
std::uint32_t skin_weight_scale_bits() noexcept;
float skin_weight_scale() noexcept;

// 0x006CC7CC, the float32 the mask value is multiplied by at 0x005A8D8C.
float skin_mask_scale() noexcept;

// Exchange byte lane 0 and byte lane 2, keeping lanes 1 and 3.
// 0x005A8B7E..0x005A8C07 applies this to the base and the four special
// colors, and 0x005A8C53..0x005A8CC3 to a layer's four remap colors, before
// the type-32 path uses them. The type-8 path never applies it.
constexpr std::uint32_t skin_swap_lane02(std::uint32_t value) noexcept {
    return (value & 0xFF00FF00U) | ((value & 0x000000FFU) << 16U) |
           ((value >> 16U) & 0x000000FFU);
}

// speed.exe 0x005A8640, the only arithmetic both paths share.
//
//   acc[k] = sum over i of (int)(lane_k(colors[i]) * weights[i]), k = 0,1,2
//   acc[3] = the same sum, or the maximum of ((int)(lane_3 * w) & 0xFF)
//            when lane3_max is true
//   result lane k = min(acc[k], 255)
//
// A color whose weight does not compare greater than skin_weight_scale() is
// skipped entirely (0x005A8681, 0x005A871E, 0x005A87C8, 0x005A886E,
// 0x005A8947). Each product is truncated toward zero, because the callee at
// 0x00674898 converts with truncation, not with the FPU rounding mode. Only
// the upper clamp exists; the game never clamps at zero.
//
// The accumulators are 32 bits wide and wrap, as the game's do
// (0x005A86A6). The conversion at 0x00674898 is 64-bit and only its low
// dword is read, so a product past 2^31 wraps too. Neither composite path
// can build a weight that reaches either case; this helper is exported, so
// it reproduces them anyway. A product past 2^63 yields zero, the low dword
// of the x87 integer indefinite the game would store. A NaN weight is
// skipped by the unordered comparison before conversion.
//
// Returns 0 and leaves `error` set when count exceeds kSkinMaxBlendColors.
bool skin_blend(
    const std::uint32_t* colors,
    const float* weights,
    std::size_t count,
    bool lane3_max,
    std::uint32_t& result,
    std::string& error);

// The two call sites use 3 and 2 colors. The cap only keeps the helper
// bounded; nothing in the game limits it.
constexpr std::size_t kSkinMaxBlendColors = 16;

// ---------------------------------------------------------------------------
// Target markers
// ---------------------------------------------------------------------------

// Both paths look for four fixed colors in the untouched target and remember
// where they were, so that the four special colors can be written back after
// compositing. The two paths spell the same four colors with byte lanes 0 and
// 2 exchanged, which is why this API keeps them apart.
//
// Surface order, compared against target pixels at 0x005A8AE5..0x005A8AFD:
//   {0xBFFF0000, 0xBF00FF00, 0xBF0000FF, 0xBFFF00FF}
// Palette order, compared against target palette entries at
// 0x005A8F8D..0x005A8FA5:
//   {0xBF0000FF, 0xBF00FF00, 0xBFFF0000, 0xBFFF00FF}
// Marker k in one list is skin_swap_lane02 of marker k in the other, so both
// lists name the same four logical colors in the same order.
extern const std::array<std::uint32_t, 4> kSkinMarkerSurface;
extern const std::array<std::uint32_t, 4> kSkinMarkerPalette;

// 0x00733E28 holds 4 lists of 16 pixel indices, 0x00733F50 their 4 counts and
// 0x00733F4C a one-shot valid flag. The game fills them on the first
// composite of the process and never checks the 16-entry bound; this
// reference refuses to overflow instead. A caller-supplied valid cache is
// also rejected when a count exceeds 16 or a recorded pixel lies outside
// the current target.
constexpr std::size_t kSkinMarkerSlots = 16;

struct SkinMarkerCache {
    // Pixel indices, not byte offsets (0x005A8B3E shifts the byte offset
    // right by 2 in the type-32 path; the type-8 path counts pixels).
    std::array<std::array<std::uint32_t, kSkinMarkerSlots>, 4> indices{};
    std::array<std::uint32_t, 4> counts{};
    bool valid = false;
};

// ---------------------------------------------------------------------------
// Parameter block
// ---------------------------------------------------------------------------

// A borrowed, counted range. The game passes bare pointers and trusts that
// every surface has the target's dimensions; carrying the count lets this
// reference check that instead.
template <typename T>
struct SkinView {
    const T* data = nullptr;
    std::size_t size = 0;
};

using SkinView32 = SkinView<std::uint32_t>;
using SkinView8 = SkinView<std::uint8_t>;

// A texture object's palette is 256 entries wide, because an 8-bit index
// addresses it directly (0x005A9161, 0x005A916E) and the marker search walks
// exactly 0x100 of them (0x005A8FF5).
using SkinPalette = std::array<std::uint32_t, 256>;

// The caller at 0x005A9CE0 builds an array of 8 records of 0x34 bytes at
// parameter block +0x18 and stores the used count at +0x1B8. Field offsets
// are given per member.
struct BodySkinLayer {
    // +0x00 holds the source texture's name hash (0x005A9F53). Because the
    // caller compacts the array, it is non-zero on every record it counts,
    // and both paths only test it against zero (0x005A8C46, 0x005A9145),
    // so a bool models it. The palette merge at 0x005A9360 does not
    // re-check it, so an absent record must also have palette_size == 0;
    // the type-8 entry point validates that instead of reproducing the
    // divergence.
    bool present = false;

    // +0x0C, the source pixels, and +0x14, the mask pixels. Both are read
    // linearly, one element per target pixel, with no pitch. The type-32
    // path reads 4 bytes per element, the type-8 path 1 byte. A view shorter
    // than the target is refused.
    SkinView32 source32;
    SkinView32 mask32;
    SkinView8 source8;
    SkinView8 mask8;

    // +0x10 and +0x18, used by the type-8 path only, and only written by
    // the caller when the target's type byte is not 0x20 (0x005AA054,
    // 0x005AA08B). Only the first palette_size entries of source_palette
    // are merged into the target palette, but any of the 256 can be
    // reached by a source index.
    const SkinPalette* source_palette = nullptr;
    const SkinPalette* mask_palette = nullptr;

    // +0x1C, the part's NUMCOLOURS: how many palette entries this layer
    // contributes (0x005A9F56, 0x005A92F8, 0x005A9428). The caller defaults
    // it to 0x10 for the hood/spoiler and doorline records (0x005A9FD4) and
    // aborts the whole composite when a vinyl layer has none
    // (0x005A9F93..0x005A9FA4). Unused by the type-32 path.
    std::uint32_t palette_size = 0;

    // +0x20, the part's REMAP. Only its being non-zero matters: the paths
    // do nothing but `test` it (0x005A8CF8, 0x005A917A). The caller looks
    // it up for the four vinyl-layer sources only
    // (0x005AA0AC..0x005AA0BA), so the hood/spoiler vinyl and the doorline
    // never remap.
    bool remap = false;

    // +0x24, +0x28, +0x2C, +0x30. Only the first three ever reach a blend:
    // all three remap call sites pass a count of 3 (0x005A8D3B, 0x005A91B6,
    // 0x005A93F5). The fourth is still read and rewritten by the type-32
    // lane swap (0x005A8CA9..0x005A8CC3) and is untouched by the type-8
    // path. When the layer has no attribute object the caller substitutes
    // 0xFF << (8 * k) for color k (0x005AA1A7).
    std::array<std::uint32_t, 4> remap_colors{};
};

// The caller's array is 8 records long (0x005AA249 copies 0x1A0 bytes, and
// the unlock loop at 0x005AA28A runs 8 times).
constexpr std::size_t kSkinMaxLayers = 8;

// The caller appends a record per source that resolved, in the fixed order
// hood or spoiler vinyl, VINYL_LAYER0..3, doorline, and passes how many it
// filled (0x005A9F00 dispatches on the source index; the five failure
// paths skip the record cursor at 0x005AA1D4). `layers` is that compacted
// list, composited front to back.
struct BodySkinRequest {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // +0x04 and +0x08..+0x14, in the caller's lane order. The type-32 path
    // applies skin_swap_lane02 to all five; the type-8 path stores them into
    // palette entries 0..4 unchanged. The base color is the BASE_PAINT
    // part's; the four special colors are PAINT_ENGINE, PAINT_SPOILER,
    // PAINT_BRAKE and PAINT_EXHAUST in that order (slots 37..40, read at
    // 0x005A9DE0), each falling back to the base color when its slot is
    // empty. Special color k replaces the pixels carrying marker k.
    std::uint32_t base_color = 0;
    std::array<std::uint32_t, 4> special_colors{};
    std::vector<BodySkinLayer> layers;
};

// The type-8 side table of partially blended pixels is a fixed 2 MiB
// allocation of 8-byte records (0x005A8F06, 0x005A92AC), so the path is only
// sound for at most this many pixels. 512 x 512 reaches it exactly.
constexpr std::size_t kSkinMaxType8Pixels = 262144;

// A generous cap for the type-32 path, which the game does not bound at all.
constexpr std::size_t kSkinMaxType32Pixels = 4194304;

// ---------------------------------------------------------------------------
// Type 32 (target byte +0x4A == 0x20), speed.exe 0x005A8A50
// ---------------------------------------------------------------------------

struct BodySkin32 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // width * height packed colors in the target's lane order.
    std::vector<std::uint32_t> pixels;
};

// `target_pixels` must hold at least width * height entries of the
// untouched target when markers.valid is false, because 0x005A8AC9 scans it
// before 0x005A8C26 overwrites it. It is ignored, and may be empty, once
// the cache is valid. A target with a zero dimension is not an error: every
// loop in the game is entry-guarded, so it composites nothing and succeeds.
// Fails without touching `out` on any bound violation.
bool composite_body_skin_32(
    const BodySkinRequest& request,
    const std::vector<std::uint32_t>& target_pixels,
    SkinMarkerCache& markers,
    BodySkin32& out,
    std::string& error);

// ---------------------------------------------------------------------------
// Type 8 (target byte +0x4A == 8), speed.exe 0x005A8E90
// ---------------------------------------------------------------------------

struct BodySkin8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // width * height palette indices.
    std::vector<std::uint8_t> indices;
    // The composited palette: entry 0 is the base color, 1..4 the four
    // special colors, then every layer's palette in layer order, then one
    // entry per partially blended pixel. Entries at or above palette_used
    // are zero here; the game composites into the target's own palette and
    // leaves whatever was there before them.
    SkinPalette palette{};
    // First palette entry that is still unused.
    std::uint32_t palette_used = 5;
    // How many pixels needed their own palette entry.
    std::uint32_t blended_pixels = 0;
};

// `target_indices` must hold at least width * height entries of the
// untouched target when markers.valid is false; `target_palette` must then
// hold its 256 entries, because 0x005A8FC0 searches them for the four
// markers. Both are ignored once the cache is valid.
//
// Fails, without touching `out`, when the composited palette would not fit:
// when 5 + sum(palette_size) exceeds 256, or when the partially blended
// pixels do not fit in the entries left below index 255. A merged palette
// that ends exactly at 256 is accepted, because 0x005A9446 leaves before
// the stage that reserves index 255 whenever nothing was blended. When the
// blended pixels do not fit, the game reduces them with the quantizer at
// 0x005A7B70/0x005A81B0/0x005A7BF0 and re-maps each pixel with 0x005A7D60;
// that quantizer is not implemented here and no result is guessed for it.
// See docs/BODY_SKIN.md.
bool composite_body_skin_8(
    const BodySkinRequest& request,
    const std::vector<std::uint8_t>& target_indices,
    const SkinPalette& target_palette,
    SkinMarkerCache& markers,
    BodySkin8& out,
    std::string& error);

} // namespace nfsu
