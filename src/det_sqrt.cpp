#include "nfsu/det_sqrt.h"

#include <cmath>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

// The correctly rounded double-precision square root, moved out of
// src/render.cpp by issue #165 so nfsu_input and nfsu_track_contact can call
// it without linking nfsu_render. The body is unchanged, bit for bit, from
// the one PR #162 (issue #125) added: only its home moved.

namespace nfsu {

double det_sqrt(double value) noexcept {
#if defined(__SSE2__) || defined(_M_X64)
    // An explicit sqrtsd avoids MinGW's x87 libm path at -O0 and -Os.
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(value)));
#elif defined(__aarch64__)
    double result;
    __asm__ ("fsqrt %d0, %d1" : "=w" (result) : "w" (value));
    return result;
#else
    return std::sqrt(value);
#endif
}

} // namespace nfsu
