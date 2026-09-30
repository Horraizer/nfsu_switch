#include "nfsu/race_hud.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace {

int failures = 0;
void check(bool okay, const char* name) {
    if (!okay) { std::fprintf(stderr, "race HUD: %s\n", name); ++failures; }
}

double needle_angle(const nfsu::RaceHudFrame& frame) {
    // Quad 7 follows the dial and its six red ticks. The top edge is the tip.
    const auto* v = &frame.vertices[7 * 4];
    const double x = (v[0].px + v[1].px) * 0.5 - 1072.9;
    const double y = 591.75 - (v[0].py + v[1].py) * 0.5;
    double angle = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    if (angle < 0.0) angle += 360.0;
    return angle;
}

void test_rpm() {
    nfsu::VehicleState car{};
    car.gear = nfsu::kVehicleGearFirst;
    nfsu::RaceHudFrame frame;
    for (const auto& [rpm, expected] :
         {std::pair<double,double>{0.0,231.4}, {7500.0,61.7125},
          {8000.0,50.4}}) {
        car.engine_rpm = rpm;
        nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
        check(std::abs(needle_angle(frame)-expected)<1.0,
              "needle points to the 8000 dial label and redline");
    }
}

void test_gear_and_speed() {
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    car.gear = nfsu::kVehicleGearNeutral;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[9*4].u * 512) == 154,
          "neutral uses the N glyph");
    car.gear = nfsu::kVehicleGearReverse;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[9*4].u * 512) == 209,
          "reverse uses the R glyph");
    check(frame.quads == 13, "zero mph has one speed glyph");
    car.velocity.x = 45.0;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(frame.quads == 15, "100 mph has three speed glyphs");
    check(frame.vertices[0].color[3] < 255, "dial surround is translucent");
}

} // namespace

int main() {
    test_rpm();
    test_gear_and_speed();
    return failures ? 1 : 0;
}
