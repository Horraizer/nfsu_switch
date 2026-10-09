#pragma once

#include "nfsu/drive_controls.h"

namespace nfsu {
// Owner issue314 comment8529: L is exclusively nitrous in the enabled
// mode. This adapter removes every L binding from the driving table in
// that mode. Disabled mode retains the supplied table (including shift
// down in the default diagnostic layout). RaceSession selects the enabled
// adapter only when the resolved performance setup installs nitrous.
class NitrousControls {
public:
    bool configure(const DriveControlsConfig& config, bool enabled, std::string& error);
    void update(const GamepadFrame& frame);
    void reset() noexcept;
    bool enabled() const noexcept { return enabled_; }
    bool held() const noexcept { return held_; }
    bool pressed() const noexcept { return pressed_; }
    bool released() const noexcept { return released_; }
    const DriveControls& driving() const noexcept { return driving_; }
    const DriveFrameState& frame_state() const noexcept { return driving_.frame_state(); }
    VehicleInput step(double speed, int gear, VehicleGearbox gearbox, bool raw_steering = false) {
        return driving_.step(speed, gear, gearbox, raw_steering);
    }
private:
    DriveControls driving_;
    bool enabled_ = false;
    bool held_ = false;
    bool pressed_ = false;
    bool released_ = false;
    bool connected_[kMaxGamepads] = {};
    bool latched_[kMaxGamepads] = {};
};
} // namespace nfsu
