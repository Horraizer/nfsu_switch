#include "nfsu/steering.h"
#include "nfsu/sha256.h"
#include <iostream>
#include <iomanip>
#include <locale>
#include <string>
int main() {
    std::cin.imbue(std::locale::classic()); std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(17);
    const auto bytes = nfsu::steering_table_bytes();
    std::cout << "tables " << nfsu::sha256_hex(nfsu::sha256(bytes.data(),bytes.size())) << '\n';
    nfsu::SteeringState state;
    std::string command;
    while (std::cin >> command) {
        if (command=="reset") { state={}; continue; }
        nfsu::SteeringContext c; int mode, predicate;
        if (command!="step" || !(std::cin >> c.raw >> c.seconds >> c.dt >> c.forward_speed >>
            c.speed >> c.heading_minus_velocity_deg >> c.throttle >> c.handbrake >>
            c.brake >> c.multiplier >> c.predicted_max_speed >> c.shift_elapsed >> mode >> predicate) ||
            (mode!=0 && mode!=1) || (predicate!=0 && predicate!=1)) {
            std::cerr << "ERROR: invalid context\n"; return 1;
        }
        c.bypass_mode=mode!=0; c.predicate=predicate!=0;
        nfsu::SteeringResult r;
        if (!nfsu::steering_step(c,state,r)) { std::cerr << "ERROR: refused context\n"; return 1; }
        std::cout << "step " << r.filtered << ' ' << r.rate_history << ' ' << r.limit_deg << ' '
            << r.target_deg << ' ' << r.rate_deg_s << ' ' << r.angle_deg << ' '
            << r.shift_factor << ' ' << r.angle << ' ' << state.assist << '\n';
    }
    if (!std::cin.eof()) { std::cerr << "ERROR: input read\n"; return 1; }
    return 0;
}
