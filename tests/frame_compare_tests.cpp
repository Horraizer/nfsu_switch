#include "nfsu/frame_compare.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    using namespace nfsu;
    int failures = 0;
    std::string error;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { std::printf("FAIL: %s (%s)\n", name, error.c_str()); ++failures; }
    };
    std::vector<unsigned char> a(400, 100), b = a, difference;
    const auto view = [](const auto& data, std::uint32_t w=10, std::uint32_t h=10, std::size_t stride=40) {
        return RgbaFrameView{data.data(), data.size(), w, h, stride};
    };
    FrameComparison m;
    check(compare_frames(view(a), view(b), m, error) && m.pixels == 100 && m.exact_pixels == 100 &&
          m.within_two_pixels == 100 && m.max_channel_difference == 0 &&
          m.rgb_squared_error == 0 && std::isinf(m.psnr_db) && m.passed, "identical frame");
    b[0] = 102;
    check(compare_frames(view(a), view(b), m, error) && m.exact_pixels == 99 &&
          m.within_two_pixels == 100 && m.max_channel_difference == 2 && m.rgb_squared_error == 4 &&
          std::abs(m.psnr_db - 66.8814162425961) < 1e-10 && m.passed, "RGB PSNR includes all pixels, delta two inclusive");
    b[0] = 103;
    check(compare_frames(view(a), view(b), m, error) && m.within_two_pixels == 99 && m.passed,
          "99 percent is accepted");
    b[4] = 103;
    check(compare_frames(view(a), view(b), m, error) && m.within_two_pixels == 98 &&
          m.psnr_db > 35 && !m.passed, "98 percent fails despite high PSNR");
    b = a; b[0] = b[1] = b[2] = 145;
    check(compare_frames(view(a), view(b), m, error) && m.within_two_pixels == 99 &&
          m.psnr_db > 35 && m.passed, "PSNR just above 35 passes with 99 percent");
    b[0] = b[1] = b[2] = 146;
    check(compare_frames(view(a), view(b), m, error) && m.within_two_pixels == 99 &&
          m.psnr_db < 35 && !m.passed, "PSNR just below 35 fails despite 99 percent");
    b[0] = b[1] = b[2] = 255;
    check(compare_frames(view(a), view(b), m, error) && m.rgb_squared_error == 3*155*155 &&
          !m.passed, "large error cannot hide in 99 percent background agreement");
    b = a; b[3] = 103; b[7] = 103;
    check(compare_frames(view(a), view(b), m, error) && m.exact_pixels == 98 &&
          m.within_two_pixels == 98 && m.max_channel_difference == 3 &&
          std::isinf(m.psnr_db) && !m.passed, "alpha counts in pixel criteria but not RGB PSNR");
    check(amplify_frame_difference(view(a), view(b), difference, error) && difference[0] == 48 &&
          difference[1] == 0 && difference[2] == 48 && difference[3] == 255, "alpha error visible as magenta");
    b = a; b[0] = 101; b[1] = 104; b[2] = 200;
    const auto saved_a = a, saved_b = b;
    check(amplify_frame_difference(view(a), view(b), difference, error) && difference.size() == 400 &&
          difference[0] == 16 && difference[1] == 64 && difference[2] == 255 && difference[3] == 255 &&
          a == saved_a && b == saved_b, "difference amplification clamps and preserves inputs");
    std::vector<unsigned char> padded_a(24, 200), padded_b(24, 0);
    for (std::size_t y=0; y<2; ++y)
        for (std::size_t c=0; c<8; ++c) { padded_a[y*16+c]=10; padded_b[y*16+c]=10; }
    check(compare_frames(view(padded_a,2,2,16), view(padded_b,2,2,16), m, error) &&
          m.pixels == 4 && m.exact_pixels == 4 && m.passed, "ignore independent row padding");
    padded_b[16] = 11;
    check(compare_frames(view(padded_a,2,2,16), view(padded_b,2,2,16), m, error) &&
          m.exact_pixels == 3 && m.rgb_squared_error == 1, "top-first second row uses stride");
    std::vector<unsigned char> full_black(1280*720*4, 0), full_white(1280*720*4, 255);
    check(compare_frames(view(full_black,1280,720,5120), view(full_white,1280,720,5120), m, error) &&
          m.pixels == 921600 && m.rgb_squared_error == 179781120000ULL &&
          m.max_channel_difference == 255 && m.exact_pixels == 0 && m.within_two_pixels == 0 &&
          m.psnr_db == 0 && !m.passed, "full-frame squared error uses 64-bit accumulation");
    const auto reject = [&](RgbaFrameView bad, const char* name) {
        m.passed = true; m.pixels = 99;
        check(!compare_frames(view(a), bad, m, error) && !m.passed && !m.pixels && !error.empty(), name);
        difference.assign(4, 10);
        check(!amplify_frame_difference(view(a), bad, difference, error) && difference.empty(), name);
    };
    auto bad = view(a); bad.pixels = nullptr; reject(bad, "null input refused");
    bad = view(a); bad.width = 0; reject(bad, "empty width refused");
    bad = view(a); bad.height = 0; reject(bad, "empty height refused");
    bad = view(a); bad.width = 9; reject(bad, "mismatched dimensions refused");
    bad = view(a); bad.byte_count = 399; reject(bad, "truncated last row refused");
    bad = view(a); bad.stride = 39; reject(bad, "short stride refused");
    bad = view(a); bad.stride = std::numeric_limits<std::size_t>::max(); reject(bad, "stride overflow refused");
    bad = view(a); bad.width = 16385; reject(bad, "oversized dimension refused");
    bad = view(a); bad.width = bad.height = 16384; reject(bad, "oversized pixel count refused");
    check(!compare_frames({}, {}, m, error) && !m.passed, "two empty answers are not agreement");
    std::printf("frame comparison: %d failures\n", failures);
    return failures ? 1 : 0;
}
