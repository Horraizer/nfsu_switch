#include "nfsu/nitrous_controls.h"

#include <algorithm>

namespace nfsu {
bool NitrousControls::configure(const DriveControlsConfig& config, bool enabled, std::string& error) {
    // Validate the caller's table before reserving L; reservation must not
    // accidentally make an invalid table appear valid.
    if (!validate_drive_controls_config(config, error)) return false;
    auto reserved = config;
    if (enabled) {
        reserved.bindings.erase(std::remove_if(reserved.bindings.begin(), reserved.bindings.end(),
            [](const DriveBinding& binding) {
                return binding.control.kind == GamepadControl::Kind::button &&
                       binding.control.index == static_cast<std::uint8_t>(GamepadButton::shoulder_left);
            }), reserved.bindings.end());
    }
    if (!driving_.configure(reserved, error)) return false;
    enabled_ = enabled;
    reset();
    return true;
}
void NitrousControls::reset() noexcept {
    driving_.reset();
    held_ = pressed_ = released_ = false;
    for (std::size_t i = 0; i < kMaxGamepads; ++i) connected_[i] = latched_[i] = false;
}
void NitrousControls::update(const GamepadFrame& frame) {
    driving_.update(frame);
    const bool was_held = held_;
    held_ = false;
    for (std::size_t i = 0; i < kMaxGamepads; ++i) {
        const auto& pad = frame.pads[i];
        const bool down = (pad.buttons & gamepad_button_bit(GamepadButton::shoulder_left)) != 0;
        if (!pad.connected) { connected_[i] = latched_[i] = false; continue; }
        if (!connected_[i]) latched_[i] = down;
        connected_[i] = true;
        if (!down) latched_[i] = false;
        if (enabled_ && down && !latched_[i]) held_ = true;
    }
    pressed_ = held_ && !was_held;
    released_ = !held_ && was_held;
}
} // namespace nfsu
