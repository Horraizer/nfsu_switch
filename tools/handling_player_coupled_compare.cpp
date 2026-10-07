#include "nfsu/player_powertrain.h"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    const float owner_speed=argc>=2?std::stof(argv[1]):0.0F;
    const float lateral_speed=argc>=3?std::stof(argv[2]):0.0F;
    const float history_torque=argc>=4?std::stof(argv[3]):0.0F;
    const auto h=nfsu::stock_240sx_handling();
    int gear=0, mode=0;
    float clutch=0, throttle=0, spin=0;
    std::cout<<std::setprecision(17);
    while (std::cin>>gear>>mode>>clutch>>throttle>>spin) {
        if (gear<0||gear>=8||mode<0||mode>3) return 3;
        nfsu::PlayerEngineState engine;
        nfsu::player_engine_reset(engine,5000.0F);
        nfsu::PlayerGearState gearbox;gearbox.mode=mode;gearbox.clutch=clutch;gearbox.requested_gear=gear;
        nfsu::player_gear_seed(gearbox,12345U);
        std::array<nfsu::PlayerWheelWork,4> w{};
        for (std::size_t k=0;k<4;++k) {
            w[k].spin=spin;w[k].contact=true;w[k].load=3.0F;
            w[k].service=1.0F;w[k].velocity_long=8.316501835256739F;
            w[k].owner_speed=owner_speed;
            w[k].velocity_lat=lateral_speed;
            w[k].history_torque=history_torque;
            w[k].position_x=h.wheels[k].longitudinal-h.centre_of_mass[0];
            w[k].position_y=h.wheels[k].lateral;
        }
        const std::array<float,4> spins{{spin,spin,spin,spin}};
        const float equivalent=nfsu::player_wheel_engine_speed(h,gear,spins);
        gearbox.wheel_engine_speed=equivalent;
        const float crank=nfsu::player_engine_torque(h,engine,gear,throttle);
        const float transmitted=nfsu::player_clutch_torque(h,engine,gearbox,gear,equivalent,crank);
        nfsu::player_distribute_torque(h,gear,engine,w,transmitted);
        for (std::size_t k=0;k<4;++k) nfsu::player_wheel_prepare(h,k,w[k]);
        for (unsigned iteration=0;iteration<5;++iteration) {
            nfsu::player_body_wheel_constraints(h,w,0.0F,0.0F,0.0F);
            const float correction=nfsu::player_coupled_correction(h,gear,engine,gearbox,w,crank);
            nfsu::player_distribute_torque(h,gear,engine,w,correction);
        }
        for (std::size_t k=0;k<4;++k) nfsu::player_wheel_limit(h,k,w[k]);
        nfsu::player_engine_feedback_finalize(h,gear,gearbox,engine,w,throttle,crank);
        const float feedback=engine.feedback_torque;
        const float rate=nfsu::player_engine_acceleration(h,engine,crank);
        std::array<float,4> wheel_rates{};
        for (std::size_t k=0;k<4;++k) wheel_rates[k]=nfsu::player_wheel_acceleration(h,k,w[k],1.0F/120.0F);
        for (std::size_t k=0;k<4;++k) nfsu::player_wheel_integrate(w[k],wheel_rates[k],1.0F/120.0F);
        nfsu::player_engine_integrate(h,engine,crank,1.0F/120.0F);
        nfsu::player_gear_integrate(h,gearbox,1.0F/120.0F);
        nfsu::player_gear_update(h,gear,gearbox,engine,w,throttle);
        std::cout<<feedback<<','<<rate<<','<<engine.angular_speed<<','
                 <<nfsu::player_engine_publish_rpm(h,engine,5000.0F,1.0F/120.0F);
        for (const auto& wheel:w) std::cout<<','<<wheel.torque<<','<<wheel.force_long<<','
                                          <<wheel.force_lat<<','<<wheel.slip<<','<<wheel.spin;
        std::cout<<','<<gearbox.clutch<<','<<gearbox.mode<<','<<gearbox.wheel_engine_speed
                 <<','<<gearbox.up_offset<<','<<gearbox.down_offset<<','<<gearbox.rng_state;
        std::cout<<'\n';
    }
    return std::cin.eof()?0:4;
}
