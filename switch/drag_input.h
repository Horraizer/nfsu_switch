#pragma once
#include "nfsu/drive_button_queue.h"
#include "nfsu/drive_controls.h"
#include "race_input.h"
#include <switch.h>
#include <array>
#include <cstdio>

// Recover shifts and lane direction transitions from bounded native HID history.
// HID states are newest-first; world delivery is oldest-first. The stick is a
// port binding, with press/release hysteresis and center-to-rearm, not repeat.
class DragPadInput {
    static constexpr std::uint32_t mask=nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_right)|
        nfsu::gamepad_button_bit(nfsu::GamepadButton::face_east)|
        nfsu::gamepad_button_bit(nfsu::GamepadButton::dpad_left)|
        nfsu::gamepad_button_bit(nfsu::GamepadButton::dpad_right);
    nfsu::DriveButtonQueue queue_{mask,std::size_t(nfsu::GamepadAxis::left_x),nfsu::DriveAxisCalibration{}.inner};
    nfsu::GamepadFrame latest_{};
    HidNpadIdType source_=HidNpadIdType_Handheld;
    u32 style_=0;
    u64 last_=0,polls_=0,samples_=0,gaps_=0;
    bool cursor_=false,initialized_=false;
    static constexpr std::int32_t stick_press=20000,stick_release=8000;
    int stick_held_=0;
    bool stick_armed_=false;

    void reset_stick(std::int32_t y) noexcept {
        stick_held_=0;stick_armed_=y>=-stick_release&&y<=stick_release;
    }
    std::uint32_t stick_buttons(std::int32_t y) noexcept {
        if(y>=-stick_release&&y<=stick_release){stick_held_=0;stick_armed_=true;}
        else {
            // Crossing directly to the opposite side releases the gesture,
            // but only an observed center rearms it. No sign-flip repeats.
            if((stick_held_>0&&y<=stick_release)||(stick_held_<0&&y>=-stick_release))stick_held_=0;
            if(stick_armed_&&y>=stick_press){stick_held_=1;stick_armed_=false;}
            else if(stick_armed_&&y<=-stick_press){stick_held_=-1;stick_armed_=false;}
        }
        return stick_held_>0?nfsu::gamepad_button_bit(nfsu::GamepadButton::shoulder_right):
            stick_held_<0?nfsu::gamepad_button_bit(nfsu::GamepadButton::face_east):0;
    }

    using InputMap=nfsu::GamepadState(*)(u32,u64,HidAnalogStickState,HidAnalogStickState);
    static nfsu::GamepadState map_default(u32,u64 buttons,HidAnalogStickState left,HidAnalogStickState right) noexcept {
        nfsu::GamepadState input;
        input.buttons=switch_race_buttons(buttons);
        input.axes[0]=left.x;input.axes[1]=left.y;input.axes[2]=right.x;input.axes[3]=right.y;
        return input;
    }
    static size_t history(const PadState& pad,HidNpadIdType& id,u32& style,
                          std::array<HidNpadCommonState,17>& states) noexcept {
        // padInitializeDefault accepts Handheld and No1. Multiple active input
        // sources retain the merged pad snapshot instead of inventing an order
        // between independent HID clocks.
        if(pad.active_handheld) {
            if(pad.active_id_mask)return 0;
            id=HidNpadIdType_Handheld;style=HidNpadStyleTag_NpadHandheld;
            return hidGetNpadStatesHandheld(id,states.data(),states.size());
        }
        const auto mask=pad.active_id_mask;
        if(!mask||(mask&(mask-1)))return 0;
        unsigned slot=0;
        while(slot<8&&!(mask&(1U<<slot)))++slot;
        if(slot>=8)return 0;
        id=static_cast<HidNpadIdType>(HidNpadIdType_No1+slot);
        if(pad.style_set&HidNpadStyleTag_NpadFullKey){style=HidNpadStyleTag_NpadFullKey;return hidGetNpadStatesFullKey(id,states.data(),states.size());}
        if(pad.style_set&HidNpadStyleTag_NpadJoyDual){style=HidNpadStyleTag_NpadJoyDual;return hidGetNpadStatesJoyDual(id,states.data(),states.size());}
        if(pad.style_set&HidNpadStyleTag_NpadJoyLeft){style=HidNpadStyleTag_NpadJoyLeft;return hidGetNpadStatesJoyLeft(id,states.data(),states.size());}
        if(pad.style_set&HidNpadStyleTag_NpadJoyRight){style=HidNpadStyleTag_NpadJoyRight;return hidGetNpadStatesJoyRight(id,states.data(),states.size());}
        return 0;
    }
public:
    void poll(const PadState& pad,bool discard=false,InputMap map=map_default) noexcept {
        ++polls_;
        auto& latest=latest_.pads[0];
        latest=map(padGetStyleSet(&pad),padGetButtons(&pad),padGetStickPos(&pad,0),padGetStickPos(&pad,1));
        latest.connected=padIsConnected(&pad);
        std::array<HidNpadCommonState,17> states{};auto id=source_;u32 style=0;
        const auto count=history(pad,id,style,states);
        // HID may be newer than padUpdate. Its newest lane magnitude is the
        // authoritative latest value too, including on a baseline/source reset.
        const auto map_history=[&](const HidNpadCommonState& state) {
            auto input=map(style,state.buttons,state.analog_stick_l,state.analog_stick_r);
            input.connected=(state.attributes&HidNpadAttribute_IsConnected)!=0;
            return input;
        };
        const auto current=count?map_history(states[0]):latest;
        if(count)latest.axes[0]=current.axes[0];
        const bool changed=count&&(!cursor_||id!=source_||style!=style_||states[0].sampling_number<last_);
        if(discard||!initialized_||!count||changed) {
            // First poll/source change/resume establishes a current baseline.
            // Never replay history from a menu, pause or previous controller.
            if(discard||!initialized_||changed) {
                auto baseline=latest_;
                if(count)baseline.pads[0].buttons=(baseline.pads[0].buttons&~mask)|
                    (current.buttons&mask);
                reset_stick(current.axes[3]);
                queue_.reset(baseline);
            } else {
                auto frame=latest_;frame.pads[0].buttons|=stick_buttons(latest_.pads[0].axes[3]);
                queue_.observe(frame);
            }
            initialized_=true;cursor_=count!=0;source_=id;style_=style;
            if(count)last_=states[0].sampling_number;
            return;
        }
        if(states[count-1].sampling_number>last_&&states[count-1].sampling_number-last_>1)++gaps_;
        for(size_t i=count;i-- >0;) {
            const auto& s=states[i];if(s.sampling_number<=last_)continue;
            auto frame=latest_;frame.pads[0]=map_history(s);
            frame.pads[0].buttons|=stick_buttons(frame.pads[0].axes[3]);
            queue_.observe(frame);last_=s.sampling_number;++samples_;
        }
        // The history call may be slightly newer than padUpdate. Do not append
        // padUpdate's older button mask and manufacture an extra release/press.
    }
    nfsu::GamepadFrame take(std::uint64_t tick) noexcept {
        auto frame=queue_.take(latest_);frame.time_us=tick*1000000/60;return frame;
    }
    void print() const noexcept {
        std::printf("DRAG_INPUT polls=%llu history_samples=%llu history_gaps=%llu pending=%zu peak=%zu overflows=%llu\n",
            (unsigned long long)polls_,(unsigned long long)samples_,(unsigned long long)gaps_,
            queue_.pending(),queue_.peak(),(unsigned long long)queue_.overflows());
    }
};
