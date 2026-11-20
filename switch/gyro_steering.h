#pragma once

#include "nfsu/drive_controls.h"
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace nfsu {

// Rotate the controller like a steering wheel about its face normal. An
// upright or inclined controller can also use gravity to correct gyro drift.
class GyroSteering {
public:
    explicit GyroSteering(HidNpadIdType id) noexcept : id_(id) {}
    ~GyroSteering() { release(); }
    GyroSteering(const GyroSteering&) = delete;
    GyroSteering& operator=(const GyroSteering&) = delete;

    void release() noexcept {
        if(started_)hidStopSixAxisSensor(handle_);
        started_=seen_=false;
        style_=0;
        recenter();
    }

    void poll(const PadState& pad,std::uint64_t now) noexcept {
        const auto style=padGetStyleSet(&pad);
        const bool dual=style==HidNpadStyleTag_NpadJoyDual;
        const bool right=dual&&(padGetAttributes(&pad)&HidNpadAttribute_IsRightConnected);
        if(style!=style_||right!=right_) {
            release();
            style_=style;right_=right;
            if(style!=HidNpadStyleTag_NpadFullKey&&style!=HidNpadStyleTag_NpadJoyLeft&&
               style!=HidNpadStyleTag_NpadJoyRight&&!dual)return;
            HidSixAxisSensorHandle handles[2]{};
            if(R_FAILED(hidGetSixAxisSensorHandles(handles,dual?2:1,id_,static_cast<HidNpadStyleTag>(style))))return;
            handle_=handles[right?1:0];
            started_=R_SUCCEEDED(hidStartSixAxisSensor(handle_));
            if(!started_)return;
            // The ring can still contain the previous session's latest sample.
            HidSixAxisSensorState state{};
            if(hidGetSixAxisSensorStates(handle_,&state,1)) {
                seen_=true;sample_=state.sampling_number;sample_at_=now;
            }
            return;
        }
        if(!started_)return;
        const auto center=style==HidNpadStyleTag_NpadJoyLeft?HidNpadButton_L:
            style==HidNpadStyleTag_NpadJoyRight?HidNpadButton_R:HidNpadButton_StickL;
        if(padGetButtonsDown(&pad)&center)recenter();
        if(seen_&&now-sample_at_>kStaleUs)recenter();
        HidSixAxisSensorState state{};
        if(!hidGetSixAxisSensorStates(handle_,&state,1))return;
        if(seen_&&state.sampling_number==sample_)return;
        if(seen_&&state.sampling_number<sample_)recenter();
        seen_=true;sample_=state.sampling_number;sample_at_=now;
        if(!(state.attributes&HidSixAxisSensorAttribute_IsConnected)||!std::isfinite(state.angle.z)) {
            recenter();return;
        }

        const auto& g=state.acceleration;
        const float xy=g.x*g.x+g.y*g.y;
        const float length_squared=xy+g.z*g.z;
        const bool gravity_valid=xy>=.09F&&length_squared>=.64F&&length_squared<=1.44F;
        const float roll=gravity_valid?std::atan2(-g.x,g.y):0.F;
        const float angle=state.angle.z;
        if(!ready_) {
            ready_=true;angle_=angle;gravity_center_=gravity_valid?std::optional<float>(roll):std::nullopt;
            return;
        }
        // The HID Z integral is in turns. Output polarity is applied in
        // steer(). A fixed 90-degree Joy-Con grip does not
        // change that axis, and the gravity reference is relative to grip.
        const float delta=std::remainder(std::remainder(angle_,1.F)-std::remainder(angle,1.F),1.F);
        angle_=angle;
        roll_=std::remainder(roll_+delta*kTurn,kTurn);
        if(gravity_valid) {
            if(!gravity_center_)gravity_center_=roll-roll_;
            const float error=std::remainder(roll-*gravity_center_-roll_,kTurn);
            roll_+=error*.08F;
        }
    }

    void steer(GamepadState& pad,bool drag) noexcept {
        constexpr auto axis=static_cast<std::size_t>(GamepadAxis::left_x);
        constexpr DriveAxisCalibration range{};
        if(!ready_||!pad.connected||std::abs(pad.axes[axis])>range.inner)return;
        if(drag) {
            constexpr float engage=20.F*kTurn/360.F, release_angle=10.F*kTurn/360.F;
            if(roll_>=engage)lane_=-1;
            else if(roll_<=-engage)lane_=1;
            else if(std::abs(roll_)<=release_angle)lane_=0;
            pad.axes[axis]=lane_*range.outer;
            return;
        }
        constexpr float full=65.F*kTurn/360.F;
        pad.axes[axis]=static_cast<std::int32_t>(std::lround(
            std::clamp(-roll_/full,-1.F,1.F)*range.outer));
    }

private:
    void recenter() noexcept { ready_=false;roll_=0.F;lane_=0;gravity_center_.reset(); }

    static constexpr float kTurn=6.2831853071795864769F;
    static constexpr std::uint64_t kStaleUs=160000;
    HidNpadIdType id_;
    HidSixAxisSensorHandle handle_{};
    std::uint32_t style_=0;
    bool right_=false,started_=false,seen_=false,ready_=false;
    std::uint64_t sample_=0,sample_at_=0;
    float angle_=0.F,roll_=0.F;
    int lane_=0;
    std::optional<float> gravity_center_;
};

} // namespace nfsu
