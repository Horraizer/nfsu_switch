#include "nfsu/player_powertrain.h"
#include <iomanip>
#include <iostream>

int main() {
    float longitudinal=0.0F,lateral=0.0F;unsigned angle=0;
    std::cout<<std::setprecision(17);
    while (std::cin>>longitudinal>>lateral>>angle) {
        if (angle>65535U) return 2;
        std::cout<<nfsu::player_velocity_angle(longitudinal,lateral)<<','
                 <<nfsu::player_angle_sin(static_cast<std::uint16_t>(angle))<<'\n';
    }
    return std::cin.eof()?0:3;
}
