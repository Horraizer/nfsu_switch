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
    // Disc, LINES and RED precede the needle. The top edge is its tip.
    const auto* v = &frame.vertices[3 * 4];
    const double x = (v[0].px + v[1].px) * 0.5 - 1072.9;
    const double y = 591.75 - (v[0].py + v[1].py) * 0.5;
    double angle = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    if (angle < 0.0) angle += 360.0;
    return angle;
}

void test_dial() {
    struct Case { double max, red; std::uint32_t lines, overlay; int x, y, rx, ry; };
    const Case cases[] = {
        {7000,6000,0x4F9F0245,0x88DF62DF,0,0,0,384},
        {7499,6500,0x4F9F0245,0x25AEEEB6,0,0,64,384},
        {7500,6500,0xC41B7346,0x94892E04,256,0,128,384},
        {8000,7500,0xC41B7346,0xD5D99FA0,256,0,192,384},
        {8499,7000,0xC41B7346,0xD5D99FA0,256,0,192,384},
        {8500,7500,0x3897E447,0xE1836AC5,0,128,0,416},
        {9000,8000,0x3897E447,0x22D3DC61,0,128,64,416},
        {9499,8000,0x3897E447,0x22D3DC61,0,128,64,416},
        {9500,8500,0x40FE164F,0x2E7DA786,256,128,128,416},
        {10000,9000,0x40FE164F,0x6FCE1922,256,128,192,416},
    };
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    for (const auto& item : cases) {
        const auto dial=nfsu::select_race_hud_dial(item.max,item.red);
        check(dial.lines_hash==item.lines && dial.red_hash==item.overlay,
              "retail max and redline select DYNTEX LINES and RED");
        nfsu::build_race_hud_frame_for_car(car,item.max,item.red,1280,720,frame);
        check(std::lround(frame.vertices[4].u*512)==item.x &&
              std::lround(frame.vertices[4].v*512)==item.y,
              "frame uses the selected LINES atlas slot");
        check(std::lround(frame.vertices[8].u*512)==item.rx &&
              std::lround(frame.vertices[8].v*512)==item.ry,
              "frame uses the selected RED atlas slot");
    }
}

void test_rpm() {
    nfsu::VehicleState car{};
    car.gear = nfsu::kVehicleGearFirst;
    nfsu::RaceHudFrame frame;
    for (const auto& [rpm, rotation] :
         {std::pair<double,double>{0.0,44.0}, {7500.0,204.3125},
          {8000.0,215.0}, {9000.0,215.0}, {-100.0,44.0}}) {
        car.engine_rpm = rpm;
        nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
        check(std::abs(nfsu::race_hud_needle_rotation(rpm,8000)-rotation)<0.001,
              "retail stock needle rotation clamps at both endpoints");
        check(std::abs(needle_angle(frame)-(274.0-rotation))<1.0,
              "needle geometry follows the retail rotation");
    }
    check(std::abs(nfsu::race_hud_needle_rotation(4500,9000)-130.0)<0.001,
          "tuned 9000 rpm band uses 50 to 210 degrees");
    check(std::abs(nfsu::race_hud_needle_rotation(5000,10000)-135.0)<0.001,
          "10000 rpm band uses 45 to 225 degrees");
}

void test_gear_and_speed() {
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    car.gear = nfsu::kVehicleGearNeutral;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[5*4].u * 512) == 186,
          "neutral uses the N glyph");
    car.gear = nfsu::kVehicleGearReverse;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[5*4].u * 512) == 241,
          "reverse uses the R glyph");
    check(frame.quads == 9, "zero mph has one speed glyph");
    car.velocity.x = 45.0;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(frame.quads == 11, "100 mph has three speed glyphs");
    check(frame.vertices[4].color[3] < 255, "dial face is translucent");
}

} // namespace

int main() {
    test_dial();
    test_rpm();
    test_gear_and_speed();
    return failures ? 1 : 0;
}
