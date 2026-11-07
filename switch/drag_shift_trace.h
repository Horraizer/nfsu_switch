#pragma once
#include "nfsu/race_session.h"
#include <array>
#include <cstdio>

// Sparse, bounded drag observations. Console I/O happens after driving ends.
class DragShiftTrace {
    struct Before {
        int gear=0,request=0;
        float rpm=0,target=0;
    };
    struct Event {
        unsigned actor=0,kind=0;
        std::uint64_t step=0,buttons=0;
        int clock=0,gear_before=0,gear=0,request_before=0,request=0,mode=0;
        float rpm_before=0,rpm=0,target=0,quality=0,clutch=0,speed=0,multiplier=0,throttle=0;
    };
    std::array<Before,4> before_{};
    std::array<Event,128> events_{};
    unsigned count_=0,dropped_=0;
    bool held_=true;
public:
    void before(const nfsu::RaceSession& race) noexcept {
        held_=nfsu::race_car_held(race.timing());
        for(unsigned i=0;i<=race.rival_count();++i) {
            const auto& s=i?race.rival(i-1):race;
            const auto& c=s.car();
            before_[i]={c.gear,c.transmission.requested_gear,
                float(c.engine.angular_speed*9.54930400848388671875),s.drag_upshift_rpm()};
        }
    }
    void after(const nfsu::RaceSession& race,std::uint64_t buttons) noexcept {
        if(nfsu::race_car_held(race.timing()))return;
        for(unsigned i=0;i<=race.rival_count();++i) {
            const auto& s=i?race.rival(i-1):race;const auto& c=s.car();
            const auto& b=before_[i];
            const unsigned kind=held_?0:c.transmission.requested_gear!=b.request?1:
                c.gear!=b.gear?2:3;
            if(kind==3)continue;
            if(count_==events_.size()){++dropped_;continue;}
            auto& e=events_[count_++];e.actor=i;e.kind=kind;
            e.step=race.timing().step;e.clock=race.timing().now_ticks;e.buttons=buttons;
            e.gear_before=b.gear;e.gear=c.gear;e.request_before=b.request;
            e.request=c.transmission.requested_gear;e.mode=c.transmission.mode;
            e.rpm_before=b.rpm;e.rpm=float(c.engine.angular_speed*9.54930400848388671875);
            e.target=b.target;e.quality=c.transmission.shift_quality;
            e.clutch=c.transmission.clutch;e.speed=c.owner_speeds.magnitude;
            e.multiplier=c.engine.throttle_multiplier;
            e.throttle=float(s.physics_telemetry().back().model.throttle);
        }
    }
    void print() const noexcept {
        std::printf("DRAG_TRACE events=%u dropped=%u buffered=1 flush=after_driving actor0=human gears=raw_N1_first2\n",count_,dropped_);
        for(unsigned i=0;i<count_;++i) {
            const auto& e=events_[i];
            std::printf("DRAG_EVENT kind=%s actor=%u step=%llu clock=%d buttons=%llu gear=%d->%d request=%d->%d mode=%d rpm_before=%.3f rpm_after=%.3f target_before=%.3f quality=%.3f clutch=%.4f speed_mps=%.4f torque_multiplier=%.3f throttle=%.4f\n",
                e.kind==0?"GO":e.kind==1?"REQUEST":"COMMIT",e.actor,
                (unsigned long long)e.step,e.clock,(unsigned long long)e.buttons,
                e.gear_before,e.gear,e.request_before,e.request,e.mode,
                double(e.rpm_before),double(e.rpm),double(e.target),double(e.quality),
                double(e.clutch),double(e.speed),double(e.multiplier),double(e.throttle));
        }
    }
};
