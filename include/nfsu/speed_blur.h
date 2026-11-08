#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace nfsu {
struct SpeedBlurFrame {
    std::array<float,4> offsets{};
    std::uint8_t alpha = 0;
    bool active() const noexcept { return alpha != 0; }
};

// PC 1.4 USA 40CD40: rendered-frame envelope, camera-space velocity,
// two four-tap radial passes (second offsets * 4), byte blend factor.
// Presentation state only; it never feeds the car, audio or camera solver.
class SpeedBlur {
public:
    void reset() noexcept { envelope_ = 0; }
    float envelope() const noexcept { return envelope_; }
    // Explicit NOS-only adapter retained for diagnostics and optional consumers.
    // The race renderer uses frame() with the original ambient blur and tail.
    SpeedBlurFrame nitrous_frame(bool active, float x, float y, float z,
                                 bool advance = true) noexcept {
        if (!active) { reset(); return {}; }
        return frame(true,x,y,z,advance);
    }
    SpeedBlurFrame frame(bool nitrous, float x, float y, float z,
                         bool advance = true) noexcept {
        if (advance && std::abs(double(envelope_) - double(nitrous)) > 1.0/255.0)
            envelope_ = nitrous ? envelope_ + 0.2F : envelope_ - 0.1F;
        const double length = std::sqrt(double(x)*x + double(y)*y + double(z)*z);
        const float speed = static_cast<float>(length);
        const double base = std::clamp((double(speed)-25.0)/100.0,0.0,1.0);
        const float strength = std::min(static_cast<float>(base + double(envelope_)*1.1),0.65F);
        SpeedBlurFrame result;
        if (!(strength > 1.0F/255.0F)) return result;
        if (speed > 0.00001F) {
            const float inv = static_cast<float>(1.0/length);
            const float nx=x*inv, ny=y*inv, nz=z*inv;
            result.offsets = {nx+nz,ny+nz,nx-nz,ny-nz};
            const double scale = (1.0+double(envelope_)*0.5F)*0.00625F*strength;
            for (auto& value : result.offsets)
                value = static_cast<float>(double(value)*scale*1.2F);
        }
        result.alpha = static_cast<std::uint8_t>(std::min(double(strength)*1.5F,1.0)*255.0F);
        return result;
    }
private:
    float envelope_ = 0;
};
} // namespace nfsu
