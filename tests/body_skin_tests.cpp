#include "nfsu/body_skin.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
long g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void check_color(std::uint32_t got, std::uint32_t want, const std::string& message) {
    ++g_checks;
    if (got != want) {
        std::cerr << "test failure: " << message << ": got " << hex(got) << " want " << hex(want)
                  << '\n';
        ++g_failures;
    }
}

std::uint32_t blend(
    const std::vector<std::uint32_t>& colors,
    const std::vector<float>& weights,
    bool lane3_max) {
    std::uint32_t result = 0xDEADBEEFU;
    std::string error;
    const bool ok = nfsu::skin_blend(
        colors.data(), weights.data(), colors.size(), lane3_max, result, error);
    check(ok, "blend should succeed: " + error);
    return result;
}

void test_lane_swap() {
    // 0x005A8B7E..0x005A8C07 rebuilds a dword as (b3<<24)|(b0<<16)|(b1<<8)|b2
    // from the four bytes b0..b3, which exchanges lanes 0 and 2.
    check_color(nfsu::skin_swap_lane02(0x11223344U), 0x11443322U, "swap lane 0 and 2");
    check_color(nfsu::skin_swap_lane02(0x000000FFU), 0x00FF0000U, "swap lifts lane 0");
    check_color(nfsu::skin_swap_lane02(0x0000FF00U), 0x0000FF00U, "swap keeps lane 1");
    check_color(nfsu::skin_swap_lane02(0xFF000000U), 0xFF000000U, "swap keeps lane 3");
    check_color(
        nfsu::skin_swap_lane02(nfsu::skin_swap_lane02(0x89ABCDEFU)),
        0x89ABCDEFU,
        "swap is its own inverse");
    // The two marker lists are the same colors in the same order.
    for (std::size_t k = 0; k < 4; ++k) {
        check_color(
            nfsu::skin_swap_lane02(nfsu::kSkinMarkerSurface[k]),
            nfsu::kSkinMarkerPalette[k],
            "marker list " + std::to_string(k) + " agrees under the lane swap");
    }
}

void test_blend_weights() {
    // A weight of one reproduces the color, because every lane value is an
    // exact integer and 0x00674898 truncates.
    check_color(blend({0x01020304U}, {1.0F}, false), 0x01020304U, "unit weight");

    // 0x3F000000 halves each lane and truncates: lane values 4, 3, 2, 1
    // become 2, 1, 1, 0.
    check_color(blend({0x01020304U}, {0.5F}, false), 0x00010102U, "half weight truncates");

    // 0x005A8689 skips a color whose weight is not strictly greater than
    // 0x006CC970, so the scale constant itself contributes nothing.
    check_color(blend({0xFFFFFFFFU}, {nfsu::skin_weight_scale()}, false), 0U, "weight at the bound");
    // One float32 step above the bound: 255 * 0x3B808082 is 1.0000002, so
    // every lane contributes exactly one.
    check_color(
        blend({0xFFFFFFFFU}, {std::nextafter(nfsu::skin_weight_scale(), 1.0F)}, false),
        0x01010101U,
        "weight just above the bound");
    check_color(blend({0xFFFFFFFFU}, {0.0F}, false), 0U, "zero weight");

    // 255 * 0x3B808081 rounds to exactly 1.0f, so a full lane value maps to
    // a full weight.
    check_color(blend({0x000000FFU}, {1.0F}, false), 0x000000FFU, "full lane");

    // Only an upper clamp exists (0x005A89FF..0x005A8A38).
    check_color(
        blend({0x000000C8U, 0x000000C8U}, {1.0F, 1.0F}, false), 0x000000FFU, "lanes clamp at 255");

    // Lane 3 is summed when the flag is clear and maximised, after a mask to
    // 8 bits, when it is set (0x005A86FB..0x005A8704 against 0x005A8719).
    check_color(blend({0x80000000U, 0x40000000U}, {1.0F, 1.0F}, false), 0xC0000000U, "lane 3 sum");
    check_color(blend({0x80000000U, 0x40000000U}, {1.0F, 1.0F}, true), 0x80000000U, "lane 3 max");
    // (int)(200 * 2.0) is 400; the sum clamps it to 255, the maximum masks
    // it to 400 & 0xFF = 144.
    check_color(blend({0xC8000000U}, {2.0F}, false), 0xFF000000U, "lane 3 sum clamps");
    check_color(blend({0xC8000000U}, {2.0F}, true), 0x90000000U, "lane 3 max wraps to 8 bits");

    // The unrolled and the tail loop must agree; six colors exercises both.
    const std::vector<std::uint32_t> six(6, 0x00000010U);
    check_color(blend(six, std::vector<float>(6, 1.0F), false), 0x00000060U, "six colors");

    std::uint32_t result = 0;
    std::string error;
    const std::vector<std::uint32_t> colors(nfsu::kSkinMaxBlendColors + 1, 0U);
    const std::vector<float> weights(nfsu::kSkinMaxBlendColors + 1, 1.0F);
    check(
        !nfsu::skin_blend(
            colors.data(), weights.data(), colors.size(), false, result, error),
        "blend refuses an unbounded count");
    check(!error.empty(), "blend reports why it refused");
}

void test_blend_edges() {
    // The exported constants are the two the paths load.
    check(nfsu::skin_weight_scale_bits() == 0x3B808081U, "weight scale bit pattern");
    check(nfsu::skin_mask_scale() == 0.0078125F, "mask scale is 1/128");

    std::uint32_t result = 0xDEADBEEFU;
    std::string error;
    check(
        nfsu::skin_blend(nullptr, nullptr, 0, false, result, error),
        "a zero count is allowed");
    check_color(result, 0U, "a zero count sums to nothing");
    const float weights[1] = {1.0F};
    check(
        !nfsu::skin_blend(nullptr, weights, 1, false, result, error),
        "a null color array with a non-zero count is refused");

    // 0x005A8689 tests C3|C0, which is also set when the compare is
    // unordered, so a NaN weight is skipped rather than propagated.
    const std::uint32_t colors[1] = {0x01020304U};
    const float nan_weight[1] = {std::numeric_limits<float>::quiet_NaN()};
    check_color(blend({0x01020304U}, {nan_weight[0]}, false), 0U, "a NaN weight is skipped");
    const float negative[1] = {-1.0F};
    check_color(blend({colors[0]}, {negative[0]}, false), 0U, "a negative weight is skipped");

    // 0x006748A7 stores a 64-bit result and only its low dword is read, so
    // a product past 2^31 wraps instead of saturating. 255 * 8500000 is
    // 0x81316CE0, whose low byte is 0xE0, and the accumulator is negative to
    // the signed compare at 0x005A8A04, so the low byte is what is stored.
    check_color(blend({0x000000FFU}, {8500000.0F}, false), 0x000000E0U, "a product past 2^31 wraps");
    check_color(blend({0x000000FFU}, {10000000.0F}, false), 0x00000080U, "another wrapped product");
    // Two products that each fit but whose sum does not: the accumulator
    // wraps the same way the game's 32-bit add does.
    check_color(
        blend({0x000000FFU, 0x000000FFU}, {8000000.0F, 8000000.0F}, false),
        0x00000000U,
        "a wrapped accumulator sum");
}

nfsu::BodySkinRequest make_request(std::uint32_t width, std::uint32_t height) {
    nfsu::BodySkinRequest request;
    request.width = width;
    request.height = height;
    return request;
}

void test_type32_base_and_markers() {
    nfsu::BodySkinRequest request = make_request(2, 2);
    request.base_color = 0x11223344U;
    request.special_colors = {0x01020304U, 0x05060708U, 0x090A0B0CU, 0x0D0E0F10U};

    const std::vector<std::uint32_t> target = {
        nfsu::kSkinMarkerSurface[0], 0x7F7F7F7FU, nfsu::kSkinMarkerSurface[1],
        nfsu::kSkinMarkerSurface[3]};

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 out;
    std::string error;
    check(
        nfsu::composite_body_skin_32(request, target, markers, out, error),
        "type 32 base composite: " + error);
    check(markers.valid, "marker cache becomes valid");
    check(markers.counts[0] == 1 && markers.counts[1] == 1, "markers 0 and 1 found once");
    check(markers.counts[2] == 0, "marker 2 is absent");
    check(markers.counts[3] == 1, "marker 3 found once");
    check(markers.indices[0][0] == 0 && markers.indices[1][0] == 2, "marker pixel indices");

    // Every color is stored with lanes 0 and 2 exchanged.
    check_color(out.pixels[0], 0x01040302U, "special color 0 at its marker");
    check_color(out.pixels[1], 0x11443322U, "base color fills the rest");
    check_color(out.pixels[2], 0x05080706U, "special color 1 at its marker");
    check_color(out.pixels[3], 0x0D100F0EU, "special color 3 at its marker");

    // A valid cache reuses the indices and ignores the target contents.
    nfsu::BodySkin32 again;
    check(
        nfsu::composite_body_skin_32(request, {}, markers, again, error),
        "cached marker composite: " + error);
    check(again.pixels == out.pixels, "cached composite repeats the first result");
}

void test_type32_mask() {
    nfsu::BodySkinRequest request = make_request(4, 1);
    request.base_color = 0;

    const std::vector<std::uint32_t> source(4, 0x00112233U);
    const std::vector<std::uint32_t> mask = {0x00000000U, 0x00000040U, 0x00000080U, 0x000000FFU};

    nfsu::BodySkinLayer layer;
    layer.present = true;
    layer.source32 = {source.data(), source.size()};
    layer.mask32 = {mask.data(), mask.size()};
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 out;
    std::string error;
    const std::vector<std::uint32_t> target(4, 0U);
    check(nfsu::composite_body_skin_32(request, target, markers, out, error), "mask composite: " + error);

    check_color(out.pixels[0], 0U, "mask 0 leaves the target alone");
    // mask 64 gives t = 0.5 and 1 - t = 0.5; the source lanes 0x33, 0x22,
    // 0x11 halve and truncate to 25, 17 and 8, and the zero target adds
    // nothing.
    check_color(out.pixels[1], 0x00081119U, "mask 64 blends half");
    check_color(out.pixels[2], 0x00112233U, "mask 128 replaces");
    check_color(out.pixels[3], 0x00112233U, "mask 255 replaces");
}

void test_type32_remap() {
    nfsu::BodySkinRequest request = make_request(1, 1);
    request.base_color = 0;

    const std::vector<std::uint32_t> source = {0x00FF8000U};
    const std::vector<std::uint32_t> mask = {0x000000FFU};

    nfsu::BodySkinLayer layer;
    layer.present = true;
    layer.remap = true;
    layer.source32 = {source.data(), source.size()};
    layer.mask32 = {mask.data(), mask.size()};
    // Caller lane order; the path exchanges lanes 0 and 2 first, so the
    // weighted colors are 0x00604020, 0x00C0B0A0 and 0x00302010.
    layer.remap_colors = {0x00204060U, 0x00A0B0C0U, 0x00102030U, 0x00FFFFFFU};
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 out;
    std::string error;
    const std::vector<std::uint32_t> target(1, 0U);
    check(nfsu::composite_body_skin_32(request, target, markers, out, error), "remap: " + error);

    // Weights are lanes 2, 1 and 0 of the source, scaled by 0x3B808081:
    // 255 gives exactly 1.0f, 128 gives 0.501960814f and 0 is skipped.
    // Lane sums are 0x20 + 80, 0x40 + 88 and 0x60 + 96.
    check_color(out.pixels[0], 0x00C09870U, "three-color remap");

    // 0x005A8CA9..0x005A8CC3 does read and rewrite the fourth remap color,
    // but no blend consumes it, so changing it must not matter.
    request.layers[0].remap_colors[3] = 0x00000000U;
    nfsu::BodySkin32 other;
    nfsu::SkinMarkerCache fresh;
    check(
        nfsu::composite_body_skin_32(request, target, fresh, other, error),
        "remap without a fourth color: " + error);
    check(other.pixels == out.pixels, "the fourth remap color is unused");
}

void test_type32_default_remap_is_identity() {
    // When a layer has no color attribute object, 0x005AA1A7 substitutes
    // 0xFF << (8 * k) for remap color k. After the type-32 lane exchange
    // those are the three unit colors, so the remap reproduces the source's
    // lanes 0..2 and clears lane 3: for every v in 0..255,
    // trunc(255 * float32(v * 0x3B808081)) == v.
    // The one exception is v == 1, whose weight is exactly 0x3B808081 and
    // is therefore dropped by the strict compare at 0x005A8681.
    const std::size_t count = 256;
    std::vector<std::uint32_t> source(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t v = static_cast<std::uint32_t>(i);
        source[i] = v | (v << 8U) | (v << 16U) | 0xFF000000U;
    }
    const std::vector<std::uint32_t> mask(count, 0x000000FFU);

    nfsu::BodySkinRequest request = make_request(static_cast<std::uint32_t>(count), 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    layer.remap = true;
    layer.source32 = {source.data(), source.size()};
    layer.mask32 = {mask.data(), mask.size()};
    layer.remap_colors = {0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0xFF000000U};
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 out;
    std::string error;
    const std::vector<std::uint32_t> target(count, 0U);
    check(
        nfsu::composite_body_skin_32(request, target, markers, out, error),
        "default remap: " + error);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t want = (i == 1) ? 0U : (source[i] & 0x00FFFFFFU);
        check_color(out.pixels[i], want, "default remap at lane value " + std::to_string(i));
    }
    check_color(out.pixels[1], 0U, "a lane value of one is dropped by the weight bound");
}

void test_type32_layer_order() {
    nfsu::BodySkinRequest request = make_request(1, 1);
    request.base_color = 0;

    const std::vector<std::uint32_t> first = {0x00000011U};
    const std::vector<std::uint32_t> second = {0x00000022U};
    const std::vector<std::uint32_t> opaque = {0x000000FFU};

    nfsu::BodySkinLayer a;
    a.present = true;
    a.source32 = {first.data(), first.size()};
    a.mask32 = {opaque.data(), opaque.size()};
    nfsu::BodySkinLayer b = a;
    b.source32 = {second.data(), second.size()};
    request.layers = {a, b};

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 out;
    std::string error;
    const std::vector<std::uint32_t> target(1, 0U);
    check(nfsu::composite_body_skin_32(request, target, markers, out, error), "order: " + error);
    check_color(out.pixels[0], 0x00000022U, "the last layer wins");
}

void test_type32_refusals() {
    std::string error;

    // A zero-size target is not an error: every loop in the game is
    // entry-guarded (0x005A8AE3, 0x005A8C0D) and it returns 1.
    {
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest zero = make_request(0, 4);
        check(
            nfsu::composite_body_skin_32(zero, {}, markers, out, error),
            "zero width composites nothing: " + error);
        check(out.pixels.empty(), "an empty target produces no pixels");
    }
    {
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        out.pixels.assign(3, 0x12345678U);
        nfsu::BodySkinRequest huge = make_request(65535, 65535);
        check(
            !nfsu::composite_body_skin_32(huge, {}, markers, out, error),
            "oversize target refused");
        check(out.pixels.size() == 3, "a refusal leaves out untouched");
        check(!markers.valid, "a refusal leaves the cache untouched");
    }
    {
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest short_target = make_request(2, 2);
        check(
            !nfsu::composite_body_skin_32(short_target, {0U}, markers, out, error),
            "short target refused");
    }
    {
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest many = make_request(1, 1);
        nfsu::BodySkinLayer empty;
        many.layers.assign(nfsu::kSkinMaxLayers + 1, empty);
        check(
            !nfsu::composite_body_skin_32(many, {0U}, markers, out, error),
            "more than eight layers refused");
    }
    {
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest missing = make_request(1, 1);
        nfsu::BodySkinLayer present;
        present.present = true;
        missing.layers.push_back(present);
        check(
            !nfsu::composite_body_skin_32(missing, {0U}, markers, out, error),
            "present layer without pixels refused");
    }
    {
        // A source or mask shorter than the target is refused; the game
        // would read past it. A longer one is accepted, as for the target.
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        const std::vector<std::uint32_t> one(1, 0U);
        nfsu::BodySkinRequest truncated = make_request(2, 1);
        nfsu::BodySkinLayer partial;
        partial.present = true;
        partial.source32 = {one.data(), one.size()};
        partial.mask32 = {one.data(), one.size()};
        truncated.layers.push_back(partial);
        check(
            !nfsu::composite_body_skin_32(truncated, {0U, 0U}, markers, out, error),
            "short layer source refused");
    }
    {
        // Over-long views are fine for the target and for a layer alike.
        nfsu::SkinMarkerCache markers;
        nfsu::BodySkin32 out;
        const std::vector<std::uint32_t> four(4, 0x00000080U);
        nfsu::BodySkinRequest longer = make_request(2, 1);
        nfsu::BodySkinLayer layer;
        layer.present = true;
        layer.source32 = {four.data(), four.size()};
        layer.mask32 = {four.data(), four.size()};
        longer.layers.push_back(layer);
        check(
            nfsu::composite_body_skin_32(longer, four, markers, out, error),
            "over-long views accepted: " + error);
        check(out.pixels.size() == 2, "only the target's pixels are produced");
    }
    {
        // Seventeen pixels carrying one marker overflow the 16-entry list.
        nfsu::SkinMarkerCache fresh;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest crowded = make_request(17, 1);
        const std::vector<std::uint32_t> crowd(17, nfsu::kSkinMarkerSurface[2]);
        check(
            !nfsu::composite_body_skin_32(crowded, crowd, fresh, out, error),
            "seventeen markers refused");
        check(!fresh.valid, "a refused scan leaves the cache invalid");
        check(out.pixels.empty(), "a refused scan leaves out untouched");
    }
    {
        // Sixteen of the same marker fit, and each one is recorded.
        nfsu::SkinMarkerCache fresh;
        nfsu::BodySkin32 out;
        nfsu::BodySkinRequest full = make_request(16, 1);
        const std::vector<std::uint32_t> crowd(16, nfsu::kSkinMarkerSurface[2]);
        full.special_colors[2] = 0x0A0B0C0DU;
        check(
            nfsu::composite_body_skin_32(full, crowd, fresh, out, error),
            "sixteen markers accepted: " + error);
        check(fresh.counts[2] == 16, "all sixteen are cached");
        check(fresh.indices[2][15] == 15, "the sixteenth slot holds the last pixel");
        for (std::size_t i = 0; i < 16; ++i) {
            check_color(
                out.pixels[i],
                nfsu::skin_swap_lane02(0x0A0B0C0DU),
                "repeated marker pixel " + std::to_string(i));
        }
    }
}

struct Type8Fixture {
    std::vector<std::uint8_t> target_indices;
    nfsu::SkinPalette target_palette{};
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> mask;
    nfsu::SkinPalette source_palette{};
    nfsu::SkinPalette mask_palette{};

    void bind(nfsu::BodySkinLayer& layer) const {
        layer.source8 = {source.data(), source.size()};
        layer.mask8 = {mask.data(), mask.size()};
        layer.source_palette = &source_palette;
        layer.mask_palette = &mask_palette;
    }
};

void test_type8_composite() {
    Type8Fixture fixture;
    fixture.target_palette[7] = nfsu::kSkinMarkerPalette[0];
    fixture.target_palette[9] = nfsu::kSkinMarkerPalette[2];
    fixture.target_indices = {7, 3, 9, 3};

    fixture.source = {0, 1, 2, 0};
    fixture.mask = {0, 1, 2, 3};
    fixture.source_palette[0] = 0xAA000000U;
    fixture.source_palette[1] = 0xAA000001U;
    fixture.source_palette[2] = 0xAA000002U;
    fixture.mask_palette[0] = 0x00000000U;
    fixture.mask_palette[1] = 0x00000040U;
    fixture.mask_palette[2] = 0x00000080U;
    fixture.mask_palette[3] = 0x000000FFU;

    nfsu::BodySkinRequest request = make_request(2, 2);
    request.base_color = 0x11111111U;
    request.special_colors = {0x22222222U, 0x33333333U, 0x44444444U, 0x55555555U};

    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 3;
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "type 8 composite: " + error);

    check(markers.counts[0] == 1 && markers.indices[0][0] == 0, "marker 0 at pixel 0");
    check(markers.counts[2] == 1 && markers.indices[2][0] == 2, "marker 2 at pixel 2");
    check(markers.counts[1] == 0 && markers.counts[3] == 0, "markers 1 and 3 absent");

    // Entries 0..4 are the base and the four special colors, stored without
    // a lane exchange (0x005A90E1..0x005A90F8).
    check_color(out.palette[0], 0x11111111U, "palette entry 0 is the base color");
    check_color(out.palette[1], 0x22222222U, "palette entry 1 is special color 0");
    check_color(out.palette[4], 0x55555555U, "palette entry 4 is special color 3");
    // The layer's own palette follows, unchanged because it does not remap.
    check_color(out.palette[5], 0xAA000000U, "layer palette starts at entry 5");
    check_color(out.palette[7], 0xAA000002U, "layer palette ends at entry 7");
    check(out.blended_pixels == 1, "one pixel needed its own entry");
    // pixel 1: mask 64 blends source palette entry 1 over the base color.
    // Lanes 1, 0, 0, 0xAA against 0x11 each: 0+8, 0+8, 0+8, 85+8.
    check_color(out.palette[8], 0x5D080808U, "blended pixel color");

    check(out.indices[0] == 1, "marker pixel 0 keeps special index 1");
    check(out.indices[1] == 8, "blended pixel takes the appended entry");
    check(out.indices[2] == 3, "the marker fixup overwrites the composited index");
    check(out.indices[3] == 5, "opaque pixel uses base index 5 plus source index 0");
    check(out.palette_used == 9, "nine palette entries are in use");
}

void test_type8_index_arithmetic() {
    Type8Fixture fixture;
    fixture.target_indices = {0, 0};
    fixture.source = {2, 1};
    fixture.mask = {0, 0};
    fixture.mask_palette[0] = 0x000000FFU;
    for (std::size_t i = 0; i < 8; ++i) {
        fixture.source_palette[i] = 0x01000000U + static_cast<std::uint32_t>(i);
    }

    nfsu::BodySkinRequest request = make_request(2, 1);
    nfsu::BodySkinLayer first;
    first.present = true;
    fixture.bind(first);
    first.palette_size = 4;

    // A second opaque layer must land after the first layer's four entries.
    const std::vector<std::uint8_t> second_source = {3, 0};
    nfsu::BodySkinLayer second = first;
    second.source8 = {second_source.data(), second_source.size()};
    second.palette_size = 2;

    request.layers = {first, second};

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "index arithmetic: " + error);
    // next_index is 5 for the first layer and 5 + 4 for the second.
    check(out.indices[0] == 9 + 3, "second layer index adds the first layer's size");
    check(out.indices[1] == 9 + 0, "second layer index of source 0");
    check_color(out.palette[5], 0x01000000U, "first layer palette at entry 5");
    check_color(out.palette[9], 0x01000000U, "second layer palette at entry 9");
    check(out.palette_used == 11, "two layers use eleven entries");
}

void test_type8_remap_palette() {
    Type8Fixture fixture;
    fixture.target_indices = {0};
    fixture.source = {0};
    fixture.mask = {0};
    fixture.mask_palette[0] = 0x000000FFU;
    fixture.source_palette[0] = 0x0000FFA0U;

    nfsu::BodySkinRequest request = make_request(1, 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    layer.remap = true;
    fixture.bind(layer);
    layer.palette_size = 1;
    // The type-8 path uses these unchanged (no lane exchange).
    layer.remap_colors = {0x00010203U, 0x00040506U, 0x00070809U, 0x00FFFFFFU};
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "type 8 remap: " + error);

    // Weights are lanes 0, 1 and 2 of 0x0000FFA0: 0xA0, 0xFF and 0x00.
    // 0xFF scales to exactly 1.0f and 0xA0 to 0.627451f; lane 2 is skipped.
    // Lane 0: (int)(3 * 0.627451) + 6 = 1 + 6 = 7.
    // Lane 1: (int)(2 * 0.627451) + 5 = 1 + 5 = 6.
    // Lane 2: (int)(1 * 0.627451) + 4 = 0 + 4 = 4.
    check_color(out.palette[5], 0x00040607U, "remapped palette entry");
    // The pixel is opaque, so it points at that entry.
    check(out.indices[0] == 5, "opaque remapped pixel index");
}

void test_type8_blend_then_opaque() {
    Type8Fixture fixture;
    fixture.target_indices = {0};
    fixture.source = {0};
    fixture.mask = {0};
    fixture.mask_palette[0] = 0x00000040U;
    fixture.source_palette[0] = 0x00FFFFFFU;

    const std::vector<std::uint8_t> opaque_mask = {1};
    fixture.mask_palette[1] = 0x000000FFU;

    nfsu::BodySkinRequest request = make_request(1, 1);
    nfsu::BodySkinLayer partial;
    partial.present = true;
    fixture.bind(partial);
    partial.palette_size = 1;

    nfsu::BodySkinLayer full = partial;
    full.mask8 = {opaque_mask.data(), opaque_mask.size()};

    request.layers = {partial, full};

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "blend then opaque: " + error);
    // 0x005A9210 drops the entry the partial layer appended.
    check(out.blended_pixels == 0, "the appended entry is dropped again");
    check(out.indices[0] == 6, "the opaque layer indexes its own palette");
    check(out.palette_used == 7, "only the two layer palettes are used");
}

void test_type8_two_partial_blends() {
    // 0x005A92C9 updates the pixel's existing side-table entry instead of
    // appending a second one, so two partial layers leave exactly one.
    Type8Fixture fixture;
    fixture.target_indices = {0};
    fixture.source = {0};
    fixture.mask = {0};
    fixture.mask_palette[0] = 0x00000040U;
    fixture.source_palette[0] = 0x00FFFFFFU;

    nfsu::BodySkinRequest request = make_request(1, 1);
    request.base_color = 0;
    nfsu::BodySkinLayer first;
    first.present = true;
    fixture.bind(first);
    first.palette_size = 1;
    nfsu::BodySkinLayer second = first;
    request.layers = {first, second};

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "two partial blends: " + error);
    check(out.blended_pixels == 1, "one side-table entry, not two");
    // t = 64/128 = 0.5 twice. First: 127 + 0 per lane. Second: 127 + 63.
    check_color(out.palette[7], 0x00BEBEBEU, "the entry holds the second blend");
    check(out.indices[0] == 7, "the pixel points at the appended entry");
    check(out.palette_used == 8, "five fixed entries, two layers, one blend");
}

void test_type8_cached_markers() {
    Type8Fixture fixture;
    fixture.target_palette[9] = nfsu::kSkinMarkerPalette[1];
    fixture.target_indices = {9, 3, 3, 3};
    fixture.source = {0, 0, 0, 0};
    fixture.mask = {0, 0, 0, 0};
    fixture.mask_palette[0] = 0x000000FFU;
    fixture.source_palette[0] = 0x0A0B0C0DU;

    nfsu::BodySkinRequest request = make_request(2, 2);
    request.base_color = 0x11111111U;
    request.special_colors[1] = 0x22222222U;
    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 1;
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 first;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, first, error),
        "type 8 first run: " + error);
    check(markers.counts[1] == 1 && markers.indices[1][0] == 0, "marker 1 cached");

    // 0x005A90B7 zeroes the whole index plane on a cached call, so the
    // untouched target is no longer needed and the result must repeat.
    nfsu::BodySkin8 again;
    const nfsu::SkinPalette empty_palette{};
    check(
        nfsu::composite_body_skin_8(request, {}, empty_palette, markers, again, error),
        "type 8 cached run: " + error);
    check(again.indices == first.indices, "the cached run repeats the indices");
    check(again.palette == first.palette, "the cached run repeats the palette");
    check(again.indices[0] == 2, "the marker pixel keeps special index 2");
}

void test_type8_marker_overflow() {
    Type8Fixture fixture;
    fixture.target_palette[4] = nfsu::kSkinMarkerPalette[3];
    fixture.target_indices.assign(17, 4);
    fixture.source.assign(17, 0);
    fixture.mask.assign(17, 0);

    nfsu::BodySkinRequest request = make_request(17, 1);
    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    out.indices.assign(2, 0x55U);
    std::string error;
    check(
        !nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "seventeen type 8 markers refused");
    check(!markers.valid, "a refused type 8 scan leaves the cache invalid");
    check(out.indices.size() == 2, "a refused type 8 scan leaves out untouched");

    // Sixteen fit.
    fixture.target_indices.assign(16, 4);
    nfsu::BodySkinRequest smaller = make_request(16, 1);
    nfsu::SkinMarkerCache fresh;
    check(
        nfsu::composite_body_skin_8(
            smaller, fixture.target_indices, fixture.target_palette, fresh, out, error),
        "sixteen type 8 markers accepted: " + error);
    check(fresh.counts[3] == 16, "all sixteen are cached");
    for (std::size_t i = 0; i < 16; ++i) {
        check(out.indices[i] == 4, "repeated marker pixel " + std::to_string(i));
    }
}

void test_type8_index_wrap() {
    // 0x005A9204 is `add dl, bl`, a byte add, and the source index is a full
    // byte that NUMCOLOURS does not bound. The reference reproduces the
    // wrap rather than refusing it.
    Type8Fixture fixture;
    fixture.target_indices = {0, 0};
    fixture.source = {250, 251};
    fixture.mask = {0, 0};
    fixture.mask_palette[0] = 0x000000FFU;
    fixture.source_palette[250] = 0x000000AAU;
    fixture.source_palette[251] = 0x000000BBU;

    nfsu::BodySkinRequest request = make_request(2, 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 1;
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "index wrap: " + error);
    check(out.indices[0] == 0xFF, "5 + 250 lands on the pending sentinel");
    check(out.indices[1] == 0x00, "5 + 251 wraps to zero");
}

void test_type8_full_palette() {
    // 5 + 251 = 256 exactly. The merge writes pal[5..255], which is in
    // bounds, and with nothing blended 0x005A9446 leaves before the stage
    // that reserves index 255. The game returns 1; so does this.
    Type8Fixture fixture;
    fixture.target_indices = {0, 0};
    fixture.source = {0, 1};
    fixture.mask = {0, 0};
    fixture.mask_palette[0] = 0x000000FFU;
    for (std::size_t e = 0; e < 251; ++e) {
        fixture.source_palette[e] = 0x01000000U + static_cast<std::uint32_t>(e);
    }

    nfsu::BodySkinRequest request = make_request(2, 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 251;
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "a merged palette of exactly 256 entries: " + error);
    check(out.palette_used == 256, "the palette ends exactly at 256");
    check_color(out.palette[255], 0x010000FAU, "the last merged entry");
    check(out.indices[0] == 5 && out.indices[1] == 6, "the pixels index the merge");

    // One more entry would write past the palette.
    request.layers[0].palette_size = 252;
    nfsu::SkinMarkerCache fresh;
    check(
        !nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, fresh, out, error),
        "a merged palette of 257 entries refused");

    // And a full palette with one blended pixel has nowhere to put it.
    request.layers[0].palette_size = 251;
    fixture.mask_palette[0] = 0x00000040U;
    nfsu::SkinMarkerCache third;
    check(
        !nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, third, out, error),
        "a blended pixel with no free entry refused");
}

void test_type8_refusals() {
    Type8Fixture fixture;
    fixture.target_indices = {0};
    fixture.source = {0};
    fixture.mask = {0};
    fixture.mask_palette[0] = 0x000000FFU;

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;

    nfsu::BodySkinRequest big = make_request(1024, 1024);
    check(
        !nfsu::composite_body_skin_8(big, {}, fixture.target_palette, markers, out, error),
        "more than 262144 pixels refused");

    nfsu::BodySkinRequest ghost = make_request(1, 1);
    nfsu::BodySkinLayer absent;
    absent.palette_size = 3;
    ghost.layers.push_back(absent);
    check(
        !nfsu::composite_body_skin_8(
            ghost, fixture.target_indices, fixture.target_palette, markers, out, error),
        "absent layer with palette entries refused");

    nfsu::BodySkinRequest wide = make_request(1, 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 257;
    wide.layers.push_back(layer);
    check(
        !nfsu::composite_body_skin_8(
            wide, fixture.target_indices, fixture.target_palette, markers, out, error),
        "a layer declaring more than 256 entries refused");
    wide.layers[0].palette_size = 256;
    check(
        !nfsu::composite_body_skin_8(
            wide, fixture.target_indices, fixture.target_palette, markers, out, error),
        "merged palette over 256 entries refused");

    // A short 8-bit source is refused the same way.
    const std::vector<std::uint8_t> one(1, 0);
    nfsu::BodySkinRequest truncated = make_request(2, 1);
    nfsu::BodySkinLayer partial;
    partial.present = true;
    partial.source8 = {one.data(), one.size()};
    partial.mask8 = {one.data(), one.size()};
    partial.source_palette = &fixture.source_palette;
    partial.mask_palette = &fixture.mask_palette;
    truncated.layers.push_back(partial);
    const std::vector<std::uint8_t> two(2, 0);
    check(
        !nfsu::composite_body_skin_8(
            truncated, two, fixture.target_palette, markers, out, error),
        "short 8-bit layer source refused");

    nfsu::BodySkinRequest no_palette = make_request(1, 1);
    nfsu::BodySkinLayer bare;
    bare.present = true;
    bare.source8 = {fixture.source.data(), fixture.source.size()};
    bare.mask8 = {fixture.mask.data(), fixture.mask.size()};
    no_palette.layers.push_back(bare);
    check(
        !nfsu::composite_body_skin_8(
            no_palette, fixture.target_indices, fixture.target_palette, markers, out, error),
        "present layer without palettes refused");
}

void test_type8_blend_overflow() {
    // 250 pixels that all blend partially leave only 250 free entries, and
    // 0x005A9455 sends exactly that case to the quantizer. The reference
    // refuses instead of guessing it.
    const std::size_t pixels = 250;
    Type8Fixture fixture;
    fixture.target_indices.assign(pixels, 0);
    fixture.source.assign(pixels, 0);
    fixture.mask.assign(pixels, 0);
    fixture.mask_palette[0] = 0x00000040U;
    fixture.source_palette[0] = 0x00FFFFFFU;

    nfsu::BodySkinRequest request = make_request(static_cast<std::uint32_t>(pixels), 1);
    nfsu::BodySkinLayer layer;
    layer.present = true;
    fixture.bind(layer);
    layer.palette_size = 1;
    request.layers.push_back(layer);

    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin8 out;
    std::string error;
    check(
        !nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, markers, out, error),
        "blended pixels beyond the free palette refused");
    check(!error.empty(), "the refusal says why");

    // One layer entry leaves 249 free, so 248 blended pixels are the most
    // that fit; two opaque pixels bring the count down to that.
    fixture.mask[0] = 1;
    fixture.mask[1] = 1;
    fixture.mask_palette[1] = 0x000000FFU;
    nfsu::SkinMarkerCache fresh;
    check(
        nfsu::composite_body_skin_8(
            request, fixture.target_indices, fixture.target_palette, fresh, out, error),
        "248 blended pixels fit: " + error);
    check(out.blended_pixels == pixels - 2, "248 entries appended");
    check(out.palette_used == 6 + (pixels - 2), "the palette ends just below 255");
}

} // namespace

int main() {
    test_lane_swap();
    test_blend_weights();
    test_blend_edges();
    test_type32_base_and_markers();
    test_type32_mask();
    test_type32_remap();
    test_type32_default_remap_is_identity();
    test_type32_layer_order();
    test_type32_refusals();
    test_type8_composite();
    test_type8_index_arithmetic();
    test_type8_remap_palette();
    test_type8_blend_then_opaque();
    test_type8_two_partial_blends();
    test_type8_cached_markers();
    test_type8_marker_overflow();
    test_type8_index_wrap();
    test_type8_full_palette();
    test_type8_refusals();
    test_type8_blend_overflow();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "body skin tests passed, " << g_checks << " checks\n";
    return 0;
}
