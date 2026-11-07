// Issue251: stdin bridge for comparison with authenticated original gates.
#include "nfsu/vehicle.h"
#include <iomanip>
#include <iostream>

int main() {
    double axis, spin;
    unsigned locked;
    std::cout << std::setprecision(17);
    while (std::cin >> axis >> spin >> locked) {
        nfsu::VehicleWheelState wheel;
        wheel.spin = spin;
        wheel.brake_locked = locked != 0;
        nfsu::vehicle_brake_pin(wheel, axis);
        std::cout << wheel.spin << ' ' << wheel.brake_locked << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
