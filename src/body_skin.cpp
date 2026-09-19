#include "nfsu/body_skin.h"

#include <cstring>

namespace nfsu {

namespace {

// Bit patterns of the float32 constants the compositor loads, so that the
// reference does not depend on how a compiler rounds a decimal literal.
constexpr std::uint32_t kWeightScaleBits = 0x3B808081U; // 0x006CC970
constexpr std::uint32_t kMaskScaleBits = 0x3C000000U;   // 0x006CC7CC
constexpr std::uint32_t kOneBits = 0x3F800000U;         // 0x006CC7BC

float bits_to_float(std::uint32_t bits) noexcept {
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 0x00674898 converts with truncation toward zero, not with the current FPU
// rounding mode. 0x006748A7 stores a 64-bit result and the caller reads only
// its low dword (0x006748B3), so the conversion wraps rather than saturating.
// A value the 64-bit conversion cannot hold, and a NaN, make x87 store the
// integer indefinite, whose low dword is zero.
//
// Every product the two composite paths produce is a 0..255 lane value times
// a float32, which double represents exactly, so double reproduces the x87
// result bit for bit.
std::uint32_t truncate_to_u32(double value) noexcept {
    if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

std::uint32_t lane(std::uint32_t color, unsigned int index) noexcept {
    return (color >> (8U * index)) & 0xFFU;
}

std::uint32_t clamp_lane(std::uint32_t value) noexcept {
    // 0x005A89FB..0x005A8A38 compares the accumulator with 255 using `jg`,
    // a signed compare, and otherwise stores its low byte. An accumulator
    // with its top bit set is negative to that compare, so it takes the
    // store-the-low-byte path.
    if (value <= 0x7FFFFFFFU) {
        return (value > 255U) ? 255U : value;
    }
    return value & 0xFFU;
}

// The weight a 0..255 lane value becomes at 0x005A8D1A and its siblings.
float lane_weight(std::uint32_t value) noexcept {
    const double scale = static_cast<double>(bits_to_float(kWeightScaleBits));
    return static_cast<float>(static_cast<double>(value) * scale);
}

struct MaskWeights {
    float source = 0.0F;
    float destination = 0.0F;
};

// The two paths build the pair differently, although no mask value can tell
// them apart: the branch only runs for 1..127, where t is already inside
// [1/128, 127/128].
//
// 0x005A8D92..0x005A8DD4 forms 1-t from the unclamped t, then clamps t above
// at 1.0 and 1-t below at 0.0, separately.
MaskWeights mask_weights_surface(std::uint32_t mask) noexcept {
    const double scale = static_cast<double>(bits_to_float(kMaskScaleBits));
    const float one = bits_to_float(kOneBits);
    MaskWeights weights;
    weights.source = static_cast<float>(static_cast<double>(mask) * scale);
    weights.destination =
        static_cast<float>(static_cast<double>(one) - static_cast<double>(weights.source));
    if (!(weights.source <= one)) {
        weights.source = one;
    }
    if (!(weights.destination >= 0.0F)) {
        weights.destination = 0.0F;
    }
    return weights;
}

// 0x005A923C..0x005A926F clamps t above first and then forms 1-t from the
// clamped value. There is no lower clamp: 0x005A8E90 contains no compare
// against 0x006CC7A4.
MaskWeights mask_weights_palette(std::uint32_t mask) noexcept {
    const double scale = static_cast<double>(bits_to_float(kMaskScaleBits));
    const float one = bits_to_float(kOneBits);
    MaskWeights weights;
    weights.source = static_cast<float>(static_cast<double>(mask) * scale);
    if (!(weights.source <= one)) {
        weights.source = one;
    }
    weights.destination =
        static_cast<float>(static_cast<double>(one) - static_cast<double>(weights.source));
    return weights;
}

bool blend_unchecked(
    const std::uint32_t* colors,
    const float* weights,
    std::size_t count,
    bool lane3_max,
    std::uint32_t& result) noexcept {
    const float epsilon = bits_to_float(kWeightScaleBits);
    // 0x005A86A6 and its siblings are 32-bit adds that wrap.
    std::uint32_t accumulator[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < count; ++i) {
        const float weight = weights[i];
        // 0x005A8689 skips the color when the compare says less, equal or
        // unordered, so only a strictly greater weight contributes.
        if (!(weight > epsilon)) {
            continue;
        }
        const double w = static_cast<double>(weight);
        const std::uint32_t color = colors[i];
        accumulator[2] += truncate_to_u32(static_cast<double>(lane(color, 2)) * w);
        accumulator[1] += truncate_to_u32(static_cast<double>(lane(color, 1)) * w);
        accumulator[0] += truncate_to_u32(static_cast<double>(lane(color, 0)) * w);
        const std::uint32_t alpha = truncate_to_u32(static_cast<double>(lane(color, 3)) * w);
        if (lane3_max) {
            // 0x005A86FB masks the product to 8 bits before the compare, so
            // the running maximum is always a 0..255 value.
            const std::uint32_t masked = alpha & 0xFFU;
            if (masked > accumulator[3]) {
                accumulator[3] = masked;
            }
        } else {
            accumulator[3] += alpha;
        }
    }
    result = clamp_lane(accumulator[0]) | (clamp_lane(accumulator[1]) << 8U) |
             (clamp_lane(accumulator[2]) << 16U) | (clamp_lane(accumulator[3]) << 24U);
    return true;
}

// The remap weights of one source color. The type-32 path reads the source
// lanes 2, 1, 0 in that order (0x005A8D04..0x005A8D33); the type-8 path reads
// a palette entry's lanes 0, 1, 2 in that order (0x005A918A..0x005A91D7).
// Both therefore feed the same logical triple, because the two lane orders
// are each other's skin_swap_lane02.
struct RemapWeights {
    float values[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

RemapWeights surface_remap_weights(std::uint32_t color) noexcept {
    RemapWeights weights;
    weights.values[0] = lane_weight(lane(color, 2));
    weights.values[1] = lane_weight(lane(color, 1));
    weights.values[2] = lane_weight(lane(color, 0));
    return weights;
}

RemapWeights palette_remap_weights(std::uint32_t color) noexcept {
    RemapWeights weights;
    weights.values[0] = lane_weight(lane(color, 0));
    weights.values[1] = lane_weight(lane(color, 1));
    weights.values[2] = lane_weight(lane(color, 2));
    return weights;
}

bool checked_pixel_count(
    const BodySkinRequest& request,
    std::size_t limit,
    std::size_t& count,
    std::string& error) {
    // Every loop in both routines is entry-guarded (0x005A8AE3, 0x005A8C0D,
    // 0x005A9044, 0x005A90FB), so an empty target composites nothing and
    // still succeeds.
    const std::uint64_t total =
        static_cast<std::uint64_t>(request.width) * static_cast<std::uint64_t>(request.height);
    if (total > static_cast<std::uint64_t>(limit)) {
        error = "target pixel count exceeds the supported bound";
        return false;
    }
    count = static_cast<std::size_t>(total);
    return true;
}

bool check_layers(const BodySkinRequest& request, std::string& error) {
    if (request.layers.size() > kSkinMaxLayers) {
        error = "more layers than the 8 the game's array holds";
        return false;
    }
    return true;
}

} // namespace

const std::array<std::uint32_t, 4> kSkinMarkerSurface = {
    0xBFFF0000U, 0xBF00FF00U, 0xBF0000FFU, 0xBFFF00FFU};

const std::array<std::uint32_t, 4> kSkinMarkerPalette = {
    0xBF0000FFU, 0xBF00FF00U, 0xBFFF0000U, 0xBFFF00FFU};

std::uint32_t skin_weight_scale_bits() noexcept {
    return kWeightScaleBits;
}

float skin_weight_scale() noexcept {
    return bits_to_float(kWeightScaleBits);
}

float skin_mask_scale() noexcept {
    return bits_to_float(kMaskScaleBits);
}

bool skin_blend(
    const std::uint32_t* colors,
    const float* weights,
    std::size_t count,
    bool lane3_max,
    std::uint32_t& result,
    std::string& error) {
    result = 0;
    if (count > kSkinMaxBlendColors) {
        error = "blend color count exceeds the supported bound";
        return false;
    }
    if (count != 0 && (colors == nullptr || weights == nullptr)) {
        error = "blend called without colors or weights";
        return false;
    }
    return blend_unchecked(colors, weights, count, lane3_max, result);
}

bool composite_body_skin_32(
    const BodySkinRequest& request,
    const std::vector<std::uint32_t>& target_pixels,
    SkinMarkerCache& markers,
    BodySkin32& out,
    std::string& error) {
    error.clear();
    std::size_t count = 0;
    if (!checked_pixel_count(request, kSkinMaxType32Pixels, count, error)) {
        return false;
    }
    if (!check_layers(request, error)) {
        return false;
    }
    if (!markers.valid && target_pixels.size() < count) {
        error = "marker scan needs the untouched target pixels";
        return false;
    }
    for (const BodySkinLayer& layer : request.layers) {
        if (!layer.present) {
            continue;
        }
        if (layer.source32.data == nullptr || layer.mask32.data == nullptr) {
            error = "a present layer has no 32-bit source or mask";
            return false;
        }
        if (layer.source32.size < count || layer.mask32.size < count) {
            error = "a present layer's source or mask is smaller than the target";
            return false;
        }
    }

    // 0x005A8AC9..0x005A8B74, once per process, on the untouched target.
    SkinMarkerCache scanned = markers;
    if (!scanned.valid) {
        scanned = SkinMarkerCache();
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t k = 0; k < 4; ++k) {
                if (target_pixels[i] != kSkinMarkerSurface[k]) {
                    continue;
                }
                if (scanned.counts[k] >= kSkinMarkerSlots) {
                    error = "more than 16 pixels carry one target marker";
                    return false;
                }
                scanned.indices[k][scanned.counts[k]] = static_cast<std::uint32_t>(i);
                ++scanned.counts[k];
                break;
            }
        }
        scanned.valid = true;
    }

    // 0x005A8B7E..0x005A8C07 and 0x005A8C19..0x005A8C26.
    std::array<std::uint32_t, 4> special{};
    for (std::size_t k = 0; k < 4; ++k) {
        special[k] = skin_swap_lane02(request.special_colors[k]);
    }
    std::vector<std::uint32_t> pixels(count, skin_swap_lane02(request.base_color));

    for (const BodySkinLayer& layer : request.layers) {
        if (!layer.present) {
            continue;
        }
        // 0x005A8C53..0x005A8CC3, done once per layer before its pixels.
        std::array<std::uint32_t, 4> remap{};
        for (std::size_t k = 0; k < 4; ++k) {
            remap[k] = layer.remap ? skin_swap_lane02(layer.remap_colors[k])
                                   : layer.remap_colors[k];
        }
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t source = layer.source32.data[i];
            const std::uint32_t mask = layer.mask32.data[i] & 0xFFU;
            if (layer.remap && mask > 0) {
                const RemapWeights weights = surface_remap_weights(source);
                blend_unchecked(remap.data(), weights.values, 3, true, source);
            }
            if (mask > 0x7FU) {
                pixels[i] = source;
            } else if (mask > 0) {
                const MaskWeights weights = mask_weights_surface(mask);
                const std::uint32_t pair[2] = {source, pixels[i]};
                const float pair_weights[2] = {weights.source, weights.destination};
                blend_unchecked(pair, pair_weights, 2, false, pixels[i]);
            }
        }
    }

    // 0x005A8E24..0x005A8E6E.
    for (std::size_t k = 0; k < 4; ++k) {
        for (std::uint32_t j = 0; j < scanned.counts[k]; ++j) {
            const std::uint32_t index = scanned.indices[k][j];
            if (index >= count) {
                error = "cached marker pixel is outside the target";
                return false;
            }
            pixels[index] = special[k];
        }
    }

    markers = scanned;
    out.width = request.width;
    out.height = request.height;
    out.pixels = std::move(pixels);
    return true;
}

bool composite_body_skin_8(
    const BodySkinRequest& request,
    const std::vector<std::uint8_t>& target_indices,
    const SkinPalette& target_palette,
    SkinMarkerCache& markers,
    BodySkin8& out,
    std::string& error) {
    error.clear();
    std::size_t count = 0;
    if (!checked_pixel_count(request, kSkinMaxType8Pixels, count, error)) {
        return false;
    }
    if (!check_layers(request, error)) {
        return false;
    }
    if (!markers.valid && target_indices.size() < count) {
        error = "marker scan needs the untouched target indices";
        return false;
    }

    // The game reads a layer's palette fields in the merge at 0x005A9360
    // without re-checking +0x00, so an absent layer that still declares
    // entries would desynchronise the per-pixel index from the merged
    // palette. Refuse that input instead of reproducing it.
    std::uint64_t merged = 5;
    for (const BodySkinLayer& layer : request.layers) {
        if (!layer.present) {
            if (layer.palette_size != 0) {
                error = "an absent layer declares palette entries";
                return false;
            }
            continue;
        }
        if (layer.source8.data == nullptr || layer.mask8.data == nullptr) {
            error = "a present layer has no 8-bit source or mask";
            return false;
        }
        if (layer.source8.size < count || layer.mask8.size < count) {
            error = "a present layer's source or mask is smaller than the target";
            return false;
        }
        if (layer.source_palette == nullptr || layer.mask_palette == nullptr) {
            error = "a present layer has no source or mask palette";
            return false;
        }
        if (layer.palette_size > 256) {
            error = "a layer declares more than 256 palette entries";
            return false;
        }
        merged += layer.palette_size;
    }
    // The merge writes pal[5 .. 5+sum-1] with no bound test, so anything
    // past 256 would run off the palette. Ending exactly at 256 is fine:
    // 0x005A9446 leaves before the entry that reserves index 255 whenever
    // nothing was blended.
    if (merged > 256) {
        error = "merged layer palettes do not fit in 256 entries";
        return false;
    }

    std::vector<std::uint8_t> indices(count, 0);
    SkinMarkerCache scanned = markers;
    if (!scanned.valid) {
        // 0x005A8FC0..0x005A9030 finds each marker's palette index.
        std::array<int, 4> marker_index = {-1, -1, -1, -1};
        for (std::size_t k = 0; k < 4; ++k) {
            for (std::size_t entry = 0; entry < target_palette.size(); ++entry) {
                if (target_palette[entry] == kSkinMarkerPalette[k]) {
                    marker_index[k] = static_cast<int>(entry);
                    break;
                }
            }
        }
        // 0x005A9050..0x005A90A1 re-indexes the target in place.
        scanned = SkinMarkerCache();
        for (std::size_t i = 0; i < count; ++i) {
            const int value = static_cast<int>(target_indices[i]);
            bool matched = false;
            for (std::size_t k = 0; k < 4; ++k) {
                if (marker_index[k] < 0 || marker_index[k] != value) {
                    continue;
                }
                if (scanned.counts[k] >= kSkinMarkerSlots) {
                    error = "more than 16 pixels carry one target marker";
                    return false;
                }
                scanned.indices[k][scanned.counts[k]] = static_cast<std::uint32_t>(i);
                ++scanned.counts[k];
                indices[i] = static_cast<std::uint8_t>(k + 1);
                matched = true;
                break;
            }
            if (!matched) {
                indices[i] = 0;
            }
        }
        scanned.valid = true;
    }

    // 0x005A90CE..0x005A90F8, written without a lane exchange.
    SkinPalette palette{};
    palette[0] = request.base_color;
    for (std::size_t k = 0; k < 4; ++k) {
        palette[k + 1] = request.special_colors[k];
    }

    struct BlendedPixel {
        std::uint32_t pixel = 0;
        std::uint32_t color = 0;
    };
    std::vector<BlendedPixel> blended;

    // 0x005A9110..0x005A9337.
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t current = palette[indices[i]];
        std::uint32_t next_index = 5;
        bool has_entry = false;
        for (const BodySkinLayer& layer : request.layers) {
            if (!layer.present) {
                continue;
            }
            const std::uint8_t source_index = layer.source8.data[i];
            std::uint32_t source = (*layer.source_palette)[source_index];
            const std::uint32_t mask = (*layer.mask_palette)[layer.mask8.data[i]] & 0xFFU;
            if (layer.remap && mask > 0) {
                const RemapWeights weights = palette_remap_weights(source);
                blend_unchecked(layer.remap_colors.data(), weights.values, 3, true, source);
            }
            if (mask > 0x7FU) {
                indices[i] = static_cast<std::uint8_t>((next_index + source_index) & 0xFFU);
                current = source;
                if (has_entry) {
                    blended.pop_back();
                    has_entry = false;
                }
            } else if (mask > 0) {
                const MaskWeights weights = mask_weights_palette(mask);
                const std::uint32_t pair[2] = {source, current};
                const float pair_weights[2] = {weights.source, weights.destination};
                blend_unchecked(pair, pair_weights, 2, false, current);
                if (has_entry) {
                    blended.back().color = current;
                } else {
                    BlendedPixel entry;
                    entry.pixel = static_cast<std::uint32_t>(i);
                    entry.color = current;
                    blended.push_back(entry);
                    has_entry = true;
                }
                indices[i] = 0xFFU;
            }
            next_index += layer.palette_size;
        }
    }

    // 0x005A934E..0x005A943C.
    std::uint32_t used = 5;
    for (const BodySkinLayer& layer : request.layers) {
        for (std::uint32_t entry = 0; entry < layer.palette_size; ++entry) {
            const std::uint32_t source = (*layer.source_palette)[entry];
            if (!layer.remap) {
                palette[used + entry] = source;
            } else {
                const RemapWeights weights = palette_remap_weights(source);
                blend_unchecked(
                    layer.remap_colors.data(), weights.values, 3, true, palette[used + entry]);
            }
        }
        used += layer.palette_size;
    }

    // 0x005A9442..0x005A94B1. 0x005A9446 leaves at once when nothing was
    // blended. Otherwise 0x005A9455 compares the count with 0xFF - used as a
    // signed value and falls back to a color quantizer when it does not fit;
    // this reference refuses instead of reimplementing that quantizer.
    if (!blended.empty()) {
        const std::uint32_t free_entries = (used >= 255U) ? 0U : (255U - used);
        if (blended.size() >= free_entries) {
            error = "blended pixels need more palette entries than remain";
            return false;
        }
    }
    for (std::size_t j = 0; j < blended.size(); ++j) {
        indices[blended[j].pixel] = static_cast<std::uint8_t>(used + j);
        palette[used + j] = blended[j].color;
    }

    // 0x005A9645..0x005A9685.
    for (std::size_t k = 0; k < 4; ++k) {
        for (std::uint32_t j = 0; j < scanned.counts[k]; ++j) {
            const std::uint32_t index = scanned.indices[k][j];
            if (index >= count) {
                error = "cached marker pixel is outside the target";
                return false;
            }
            indices[index] = static_cast<std::uint8_t>(k + 1);
        }
    }

    markers = scanned;
    out.width = request.width;
    out.height = request.height;
    out.indices = std::move(indices);
    out.palette = palette;
    out.palette_used = used + static_cast<std::uint32_t>(blended.size());
    out.blended_pixels = static_cast<std::uint32_t>(blended.size());
    return true;
}

} // namespace nfsu
