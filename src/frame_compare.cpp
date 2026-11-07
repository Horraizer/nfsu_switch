#include "nfsu/frame_compare.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace nfsu {
namespace {
bool valid(const RgbaFrameView& v) {
    if (!v.pixels || !v.width || !v.height || v.width > 16384 || v.height > 16384 ||
        std::uint64_t{v.width} * v.height > 16000000) return false;
    const std::size_t row = std::size_t{v.width} * 4;
    if (v.stride < row || v.byte_count < row ||
        (v.height > 1 && v.stride > (v.byte_count - row) / (v.height - 1))) return false;
    return true;
}
bool pair(const RgbaFrameView& a, const RgbaFrameView& b, std::string& error) {
    error.clear();
    if (!valid(a) || !valid(b) || a.width != b.width || a.height != b.height) {
        error = "frame comparison requires matching, nonempty, bounded RGBA8 views";
        return false;
    }
    return true;
}
unsigned delta(unsigned char a, unsigned char b) { return a > b ? a-b : b-a; }
} // namespace

bool compare_frames(const RgbaFrameView& cpu, const RgbaFrameView& gpu,
                    FrameComparison& result, std::string& error) {
    result = {};
    if (!pair(cpu, gpu, error)) return false;
    FrameComparison out;
    out.pixels = std::uint64_t{cpu.width} * cpu.height;
    for (std::uint32_t y = 0; y < cpu.height; ++y) {
        const auto* a = cpu.pixels + y*cpu.stride;
        const auto* b = gpu.pixels + y*gpu.stride;
        for (std::uint32_t x = 0; x < cpu.width; ++x) {
            unsigned worst = 0;
            for (unsigned c = 0; c < 4; ++c) {
                const auto d = delta(a[x*4+c], b[x*4+c]);
                worst = std::max(worst, d);
                if (c < 3) out.rgb_squared_error += std::uint64_t{d} * d;
            }
            out.exact_pixels += worst == 0;
            out.within_two_pixels += worst <= 2;
            out.max_channel_difference = std::max(out.max_channel_difference, worst);
        }
    }
    out.psnr_db = out.rgb_squared_error == 0 ? std::numeric_limits<double>::infinity() :
        10 * std::log10(65025.0 * (3 * out.pixels) / static_cast<double>(out.rgb_squared_error));
    // Integer counts decide the 99% boundary, never a rounded display value.
    out.passed = out.within_two_pixels * 100 >= out.pixels * 99 &&
                 out.psnr_db >= kFrameCompareMinPsnrDb;
    result = out;
    return true;
}

bool amplify_frame_difference(const RgbaFrameView& cpu, const RgbaFrameView& gpu,
                              std::vector<unsigned char>& difference, std::string& error) {
    difference.clear();
    if (!pair(cpu, gpu, error)) return false;
    try {
        std::vector<unsigned char> out(std::size_t{cpu.width} * cpu.height * 4);
        for (std::uint32_t y = 0; y < cpu.height; ++y) {
            const auto* a = cpu.pixels + y*cpu.stride;
            const auto* b = gpu.pixels + y*gpu.stride;
            for (std::uint32_t x = 0; x < cpu.width; ++x) {
                const auto alpha = delta(a[x*4+3], b[x*4+3]);
                const auto offset = (std::size_t{y} * cpu.width + x) * 4;
                for (unsigned c = 0; c < 3; ++c) {
                    const auto d = delta(a[x*4+c], b[x*4+c]);
                    out[offset+c] = static_cast<unsigned char>(std::min(255U,
                        kFrameDifferenceAmplification * (c == 1 ? d : std::max(d, alpha))));
                }
                out[offset+3] = 255;
            }
        }
        difference.swap(out);
        return true;
    } catch (const std::bad_alloc&) {
        error = "not enough memory for amplified frame difference"; return false;
    }
}
} // namespace nfsu
