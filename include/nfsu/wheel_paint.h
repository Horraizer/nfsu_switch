#pragma once
#include <cstdint>
namespace nfsu {
// Original5AA680/5A8570/5A8640, decoded RGBA lanes. Mask is original blue.
// The source alpha survives; this is the proven32-bit writable target path.
std::uint32_t wheel_paint_pixel(std::uint32_t source,unsigned mask,std::uint32_t paint) noexcept;
}
