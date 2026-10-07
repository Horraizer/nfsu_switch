#include "nfsu/player_powertrain.h"

#include <array>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const auto handling = nfsu::stock_240sx_handling();
    if (argc == 2 && std::string(argv[1]) == "--record") {
        std::array<unsigned char, nfsu::kVehicleRecordSize> low{}, high{};
        high.fill(255);
        nfsu::encode_vehicle_handling(handling, low.data(), low.size());
        nfsu::encode_vehicle_handling(handling, high.data(), high.size());
        std::cout << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < low.size(); ++i)
            if (low[i] == high[i]) std::cout << std::setw(3) << i << ':' <<
                std::setw(2) << static_cast<unsigned>(low[i]) << '\n';
        return 0;
    }
    if (argc != 1) return 2;
    int gear = 0, mode = 0;
    float clutch = 0, throttle = 0, spin = 0;
    std::cout << std::setprecision(17);
    while (std::cin >> gear >> mode >> clutch >> throttle >> spin) {
        if (gear < 0 || gear >= 8 || mode < 0 || mode > 3) return 3;
        nfsu::PlayerEngineState engine;
        nfsu::player_engine_reset(engine, 5000.0F);
        const float prior = engine.angular_speed;
        nfsu::PlayerGearState gearbox;
        gearbox.mode = mode; gearbox.clutch = clutch;
        const std::array<float,4> wheels{{spin,spin,spin,spin}};
        const float equivalent = nfsu::player_wheel_engine_speed(handling, gear, wheels);
        const float crank = nfsu::player_engine_torque(handling, engine, gear, throttle);
        const float transmitted = nfsu::player_clutch_torque(handling, engine, gearbox,
                                                           gear, equivalent, crank);
        engine.feedback_torque = -transmitted;
        const float acceleration = nfsu::player_engine_acceleration(handling, engine, crank);
        nfsu::player_engine_integrate(handling, engine, crank, 1.0F/120.0F);
        const float exported = nfsu::player_engine_publish_rpm(handling, engine,5000.0F,1.0F/120.0F);
        const float driven = ((transmitted * handling.gear_ratios[static_cast<std::size_t>(gear)]) *
                               handling.final_drive_front) * 0.5F;
        std::cout << prior << ',' << equivalent << ',' << -transmitted << ',' << acceleration
                  << ',' << engine.angular_speed << ',' << exported << ','
                  << engine.feedback_torque << ',' << driven << '\n';
    }
    return std::cin.eof() ? 0 : 4;
}
