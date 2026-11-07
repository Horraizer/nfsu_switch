#pragma once
#include "nfsu/input.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace nfsu {

// Keep selected button transitions across display polls without a world step.
// An optional axis preserves direction transitions, not every magnitude.
// Unselected axes and buttons always come from the latest sample. The queue
// contains states (including releases), so DriveControls keeps its normal edge
// and connection-latch rules. It never synthesizes a repeat for a held button.
class DriveButtonQueue {
public:
    explicit DriveButtonQueue(std::uint32_t mask,std::size_t axis=kGamepadAxisCount,
                              std::int32_t inner=0) noexcept : mask_(mask),axis_(axis),inner_(inner) {}
    void reset(const GamepadFrame& frame) noexcept {
        head_=count_=0;ready_=true;
        blocked_=frame.pads[0].buttons&mask_;
        observed_=state(frame);observed_.buttons=0;delivered_=observed_;
        push(observed_);
    }
    void observe(const GamepadFrame& frame) noexcept {
        if(!ready_){reset(frame);return;}
        const auto& pad=frame.pads[0];
        blocked_&=pad.buttons;
        const State next=state(frame);
        if(next.connected==observed_.connected&&next.buttons==observed_.buttons&&next.direction==observed_.direction)return;
        // A disconnect cancels undelivered presses. Keep the disconnect itself
        // even if reconnection occurs before the next simulation tick.
        if(!next.connected){head_=count_=0;blocked_=0;}
        if(count_==states_.size()) {
            ++overflows_;reset(frame);return; // fail closed, no stuck/repeated key
        }
        observed_=next;push(next);
    }
    GamepadFrame take(const GamepadFrame& latest) noexcept {
        if(!ready_)reset(latest);
        if(count_){delivered_=states_[head_];head_=(head_+1)%states_.size();--count_;}
        auto out=latest;
        out.pads[0].connected=delivered_.connected;
        out.pads[0].buttons=(out.pads[0].buttons&~mask_)|delivered_.buttons;
        // Deliver a missed flick even when latest has returned to center.
        // While held, use the newest magnitude rather than replaying jitter.
        if(axis_<kGamepadAxisCount&&direction(out.pads[0].axes[axis_])!=delivered_.direction)
            out.pads[0].axes[axis_]=delivered_.axis_value;
        if(!delivered_.connected)out.pads[0].buttons=0;
        if(!delivered_.connected&&axis_<kGamepadAxisCount)out.pads[0].axes[axis_]=0;
        return out;
    }
    std::size_t pending() const noexcept{return count_;}
    std::size_t peak() const noexcept{return peak_;}
    std::uint64_t overflows() const noexcept{return overflows_;}
private:
    struct State {bool connected=false;std::uint32_t buttons=0;int direction=0;std::int32_t axis_value=0;};
    int direction(std::int32_t value)const noexcept {return value < -inner_?-1:value > inner_?1:0;}
    State state(const GamepadFrame& frame)const noexcept {
        const auto& pad=frame.pads[0];State value;value.connected=pad.connected;
        if(pad.connected) {
            value.buttons=pad.buttons&mask_&~blocked_;
            if(axis_<kGamepadAxisCount){value.axis_value=pad.axes[axis_];value.direction=direction(value.axis_value);}
        }
        return value;
    }
    void push(State value) noexcept {
        states_[(head_+count_)%states_.size()]=value;++count_;
        if(count_>peak_)peak_=count_;
    }
    std::array<State,64> states_{};
    std::size_t head_=0,count_=0,peak_=0;
    std::uint32_t mask_=0,blocked_=0;
    std::size_t axis_=kGamepadAxisCount;
    std::int32_t inner_=0;
    std::uint64_t overflows_=0;
    State observed_{},delivered_{};
    bool ready_=false;
};
}
