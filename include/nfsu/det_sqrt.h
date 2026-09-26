#pragma once

namespace nfsu {

// One rounding to binary64 on the supported x86 and AArch64 targets: hardware
// sqrtsd on x86 (SSE2) and fsqrt on AArch64. MinGW's out-of-line libm sqrt
// can round twice, through x87 extended precision, at -O0 and -Os (issue
// #125, docs/RENDERER.md "Determinism"). Shared by the CPU reference
// renderer (docs/RENDERER.md), the controller stick dead zone
// (docs/INPUT.md) and track contact's sweep length (docs/TRACK_COLLISION.md)
// wherever a double-precision square root sits on a replayed path, so that
// none of the three depends on the build. Moved out of nfsu_render into its
// own small platform library (issue #165) so the neutral nfsu_input and
// nfsu_track_contact can call it without linking the renderer; see the
// NFSU_PLATFORM_LIBRARIES comment in CMakeLists.txt for why this library
// cannot be neutral itself.
double det_sqrt(double value) noexcept;

} // namespace nfsu
