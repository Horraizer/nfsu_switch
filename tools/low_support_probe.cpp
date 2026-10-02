#include "nfsu/vehicle.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--table") {
        for (const auto& pair : nfsu::vehicle_low_support_curve()) for (float v : pair) {
            std::uint32_t bits = 0; std::memcpy(&bits, &v, 4);
            for (unsigned k = 0; k < 4; ++k)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << ((bits >> (8*k)) & 255U);
        }
        std::cout << '\n'; return 0;
    }
    if (argc != 1) return 2;
    std::cout << std::setprecision(17);
    std::string line; unsigned rows = 0;
    while (std::getline(std::cin, line)) {
        nfsu::VehicleVec3 omega; double tonnes = 0; unsigned wheels = 0, state = 0;
        std::istringstream in(line); std::string extra;
        if (!(in >> omega.x >> omega.y >> omega.z >> tonnes >> wheels >> state) ||
            (in >> extra) || !std::isfinite(tonnes) || !(tonnes > 0) ||
            !std::isfinite(omega.x) || !std::isfinite(omega.y) || !std::isfinite(omega.z) ||
            wheels > 4 || state > 2) { std::cerr << "FAIL: invalid context\n"; return 1; }
        const auto torque = nfsu::vehicle_low_support_torque(omega, tonnes*1000, wheels, state);
        std::cout << torque.x/1000 << ' ' << torque.y/1000 << ' ' << torque.z/1000 << '\n'; ++rows;
    }
    if (!std::cin.eof() || !rows) { std::cerr << "FAIL: input read\n"; return 1; }
    return std::cout ? 0 : 1;
}
