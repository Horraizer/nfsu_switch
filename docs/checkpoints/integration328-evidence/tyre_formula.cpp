#include "../../../src/vehicle.cpp"
#include <iostream>
#include <iomanip>
int main() {
    double slip,load,slip_scale,load_scale,force_scale;
    std::cout << std::setprecision(17);
    while (std::cin >> slip >> load >> slip_scale >> load_scale >> force_scale) {
        const nfsu::VehicleAxleTyre tyre{static_cast<float>(slip_scale),
            static_cast<float>(load_scale),static_cast<float>(force_scale),1.8F,1.5F};
        const double v_lat=1.0;
        using nfsu::lateral_curve;
        const double p = 343.775 * slip / static_cast<double>(tyre.slip_scale);
        const double x = 0.8 * (load / 1000.0) /
            static_cast<double>(tyre.lateral_load_scale);
        double curve_force = lateral_curve(6, x);
        if (p < 6.0) {
            const auto idx = static_cast<unsigned>(p);
            const double frac = p - static_cast<double>(idx);
            curve_force = lateral_curve(idx, x) * (1.0 - frac) +
                          lateral_curve(idx + 1, x) * frac;
        }
        double fy = -curve_force * static_cast<double>(tyre.lateral_scale) * 2500.0 *
                    (v_lat < 0.0 ? -1.0 : 1.0);
        std::cout << -fy << "\n";
    }
    return 0;
}
