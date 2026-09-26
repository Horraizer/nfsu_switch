#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Top-first RGBA8 rows, including any byte stride padding. Borrowed data;
// byte_count must cover the last row. Comparison ignores row padding.
struct RgbaFrameView {
    const unsigned char* pixels = nullptr;
    std::size_t byte_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
};

// Fixed acceptance from issue #147. Exact/within-two and maximum include
// all four channels; PSNR uses RGB only (owner confirmed), every pixel.
constexpr double kFrameCompareMinPsnrDb = 35.0;
constexpr unsigned kFrameDifferenceAmplification = 16;
struct FrameComparison {
    std::uint64_t pixels = 0;
    std::uint64_t exact_pixels = 0;
    std::uint64_t within_two_pixels = 0;
    unsigned max_channel_difference = 0;
    std::uint64_t rgb_squared_error = 0;
    double psnr_db = 0;
    bool passed = false;
};
// No cropping, registration, gamma transform or background exclusion. Empty,
// mismatched, truncated, oversized (>16384 per axis or >16M pixels) views
// are errors, not agreement. Refusal resets result; perfect RGB has +inf PSNR.
bool compare_frames(const RgbaFrameView& cpu, const RgbaFrameView& gpu,
                    FrameComparison& result, std::string& error);
// abs(RGB difference)*16 clamped to 255, opaque alpha. Alpha-only errors
// also light up red/blue (magenta), so every counted difference is visible.
// Output is tight top-first RGBA8, cleared on refusal. Inputs unchanged.
bool amplify_frame_difference(const RgbaFrameView& cpu, const RgbaFrameView& gpu,
                              std::vector<unsigned char>& difference, std::string& error);

} // namespace nfsu
