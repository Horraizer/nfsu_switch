#include "nfsu/race_hud.h"
#include "nfsu/arg_path.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

int failures = 0;
void check(bool okay, const char* name) {
    if (!okay) { std::fprintf(stderr, "race HUD: %s\n", name); ++failures; }
}

struct Point { double x, y; };

std::pair<Point, Point> needle_ends(const nfsu::RaceHudFrame& frame,
                                    bool red = true) {
    // Fill and optional RED precede Needle; Tach follows it in FEng
    // depth order. The texture's ring is at row 64; its shaft ends near 125.
    const auto* v = &frame.vertices[(red ? 2 : 1) * 4];
    const Point top{(v[0].px+v[1].px)*0.5,(v[0].py+v[1].py)*0.5};
    const Point bottom{(v[2].px+v[3].px)*0.5,(v[2].py+v[3].py)*0.5};
    const auto at = [&](double row) {
        return Point{top.x+(bottom.x-top.x)*row/128.0,
                     top.y+(bottom.y-top.y)*row/128.0};
    };
    return {at(64.0),at(125.0)};
}

double needle_angle(const nfsu::RaceHudFrame& frame, bool red = true) {
    const auto [ring, tip] = needle_ends(frame, red);
    double angle = std::atan2(ring.y-tip.y,tip.x-ring.x) *
                   180.0 / 3.14159265358979323846;
    if (angle < 0.0) angle += 360.0;
    return angle;
}

void test_needle_endpoint(const nfsu::RaceHudFrame& frame,
                          double label_x, double label_y, bool red = true) {
    const auto* face=&frame.vertices[(red ? 3 : 2)*4];
    const double pitch=(face[1].px-face[0].px)/256.0;
    const Point hub{face[0].px+69.0*pitch,face[0].py+70.0*pitch};
    const Point label{face[0].px+label_x*pitch,
                      face[0].py+label_y*pitch};
    const auto [ring, tip]=needle_ends(frame,red);
    const double vx=tip.x-ring.x, vy=tip.y-ring.y;
    const double lx=label.x-ring.x, ly=label.y-ring.y;
    const double length=std::hypot(vx,vy);
    const double label_radius=std::hypot(lx,ly);
    check(std::abs(ring.x-hub.x)<0.01 &&
          std::abs(ring.y-hub.y-pitch)<0.01,
          "needle centre retains the one-texel FEng offset from face hub");
    check(std::abs(length-61.0*pitch)<0.5,
          "needle shaft uses the FEng 128-texel length and shared scale");
    // The traced rotation frame leaves the fitted tuned "9" label 23 px
    // off the ray. Keep this as a direction and length guard.
    check((vx*lx+vy*ly)/length>label_radius-4.0 &&
          length>label_radius+10.0 &&
          std::abs(vx*ly-vy*lx)/length<25.0,
          "needle shaft tip points outward past the endpoint label");
}

void test_face_registration(const nfsu::RaceHudFrame& frame, bool red) {
    const auto* face=&frame.vertices[(red ? 3 : 2)*4];
    const double u0=face[0].u*512.0, v0=face[0].v*512.0;
    const double u1=face[1].u*512.0, v1=face[2].v*512.0;
    check(std::abs((u1-u0)-256.0)<0.01 &&
          std::abs((v1-v0)-128.0)<0.01,
          "LINES samples its full 256x128 retail sprite");
    const double pitch=(face[1].px-face[0].px)/256.0;
    const double hub_x=face[0].px+69.0*pitch;
    const double hub_y=face[0].py+70.0*pitch;
    check(std::abs(hub_x-1133.0)<0.01 &&
          std::abs(hub_y-(601.0-pitch))<0.01,
          "decoded LINES hub uses measured anchor and recovered SAT offset");
    const auto* needle=&frame.vertices[(red ? 2 : 1)*4];
    check(std::abs((needle[1].u-needle[0].u)*512.0-16.0)<0.01 &&
          std::abs((needle[3].v-needle[0].v)*512.0-128.0)<0.01,
          "needle samples the complete 16x128 retail sprite");
    check(needle[0].color[0]==207 && needle[0].color[1]==233 &&
          needle[0].color[2]==242 && needle[0].color[3]==255,
          "3rdPersonNeedle uses SAT 0x457C BGRA (242,233,207,255)");
    const auto* fill=&frame.vertices[0];
    const auto* overlay=&frame.vertices[1*4];
    check(std::lround(fill[0].u*512)==0 &&
          std::lround(fill[0].v*512)==256 &&
          std::lround(needle[0].u*512)==256 &&
          std::lround(needle[0].v*512)==256,
          "needle follows retail fill and precedes the Tach face");
    check(fill[0].px==face[0].px && fill[0].py==face[0].py &&
          fill[2].px==face[2].px && fill[2].py==face[2].py &&
          fill[0].color[3]==150,
          "fill uses the complete shared SAT rectangle and alpha");
    if (red)
        check(overlay[0].color[0]==150 && overlay[0].color[1]==0 &&
              overlay[0].color[2]==0 && overlay[0].color[3]==150 &&
              needle>overlay && face>needle,
              "draw order is fill, Redline, Needle, then Tach");
    else
        check(needle>fill && face>needle,
              "without Redline, draw order is fill, Needle, then Tach");
    check(face[0].color[0]==0 && face[0].color[1]==128 &&
          face[0].color[2]==192 && face[0].color[3]==255,
          "3rdPersonTach uses its own blue SAT colour");
    if (red) {
        const auto* overlay=&frame.vertices[1*4];
        check(std::abs((overlay[0].px-face[0].px)-79.0*pitch)<0.5 &&
              std::abs((overlay[0].py-face[0].py)-6.0*pitch)<0.5 &&
              std::abs((overlay[1].px-overlay[0].px)-64.0*pitch)<0.5,
              "RED shares the LINES texel frame");
        check(overlay[0].color[0]==150 && overlay[0].color[1]==0 &&
              overlay[0].color[2]==0 && overlay[0].color[3]==150,
              "Redline uses SAT 0x411C BGRA (0,0,150,150)");
    }
}

void test_dial() {
    struct Case { double max, red; std::uint32_t lines, overlay; int x, y, rx, ry; };
    const Case cases[] = {
        {7000,6000,0x4F9F0245,0x88DF62DF,0,0,0,384},
        {7499,6500,0x4F9F0245,0x25AEEEB6,0,0,64,384},
        {7499,6499,0x4F9F0245,0x88DF62DF,0,0,0,384},
        {7500,6500,0xC41B7346,0x94892E04,256,0,128,384},
        {8000,6200,0xC41B7346,0,256,0,0,0},
        {8000,6400,0xC41B7346,0,256,0,0,0},
        {8000,7500,0xC41B7346,0xD5D99FA0,256,0,192,384},
        {8499,7000,0xC41B7346,0xD5D99FA0,256,0,192,384},
        {8500,7500,0x3897E447,0xE1836AC5,0,128,0,416},
        {9000,8000,0x3897E447,0x22D3DC61,0,128,64,416},
        {9499,8000,0x3897E447,0x22D3DC61,0,128,64,416},
        {9500,8500,0x40FE164F,0x2E7DA786,256,128,128,416},
        {9999,9000,0x40FE164F,0x6FCE1922,256,128,192,416},
        {10000,9000,0x40FE164F,0x6FCE1922,256,128,192,416},
    };
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    for (const auto& item : cases) {
        const auto dial=nfsu::select_race_hud_dial(item.max,item.red);
        check(dial.lines_hash==item.lines && dial.red_hash==item.overlay,
              "retail max and redline select DYNTEX LINES and RED");
        nfsu::build_race_hud_frame_for_car(car,item.max,item.red,1280,720,frame);
        const auto face_index=(item.overlay ? 3 : 2)*4;
        check(std::lround(frame.vertices[face_index].u*512)==item.x &&
              std::lround(frame.vertices[face_index].v*512)==item.y,
              "frame uses the selected LINES atlas slot");
        if (item.overlay) {
            check(std::lround(frame.vertices[1*4].u*512)==item.rx &&
                  std::lround(frame.vertices[1*4].v*512)==item.ry,
                  "frame uses the selected RED atlas slot");
        } else {
            check(frame.quads==6,"below 6500 in the 8500LINES band draws no RED");
            check(std::abs(needle_angle(frame,false)-226.0)<1.0,
                  "no RED keeps the needle at the stock zero angle");
        }
        test_face_registration(frame,item.overlay!=0);
        if (item.max==8000.0 || item.max==9000.0) {
            test_needle_endpoint(frame,34.0,105.0,item.overlay!=0);
            car.engine_rpm=item.max;
            nfsu::build_race_hud_frame_for_car(car,item.max,item.red,
                                               1280,720,frame);
            test_needle_endpoint(frame,item.max==9000.0 ? 107.0 : 106.0,
                                 item.max==9000.0 ? 33.0 : 34.0,
                                 item.overlay!=0);
            car.engine_rpm=0.0;
        }
    }
}

void test_rpm() {
    nfsu::VehicleState car{};
    car.gear = nfsu::kVehicleGearFirst;
    nfsu::RaceHudFrame frame;
    for (const auto& [rpm, rotation] :
         {std::pair<double,double>{0.0,44.0}, {800.0,61.1}, {7500.0,204.3125},
          {8000.0,215.0}, {9000.0,215.0}, {-100.0,44.0}}) {
        car.engine_rpm = rpm;
        nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
        check(std::abs(nfsu::race_hud_needle_rotation(rpm,8000)-rotation)<0.001,
              "retail stock needle rotation clamps at both endpoints");
        check(std::abs(needle_angle(frame)-(270.0-rotation))<1.0,
              "needle geometry follows the retail rotation");
    }
    check(std::abs(nfsu::race_hud_needle_rotation(4500,9000)-130.0)<0.001,
          "tuned 9000 rpm band uses 50 to 210 degrees");
    check(std::abs(nfsu::race_hud_needle_rotation(5000,10000)-135.0)<0.001,
          "10000 rpm band uses 45 to 225 degrees");
    check(std::abs(nfsu::race_hud_needle_rotation(9999,9999)-210.0)<0.001,
          "9999 max RPM stays in the 50 to 210 degree band");
}

void test_gear_and_speed() {
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    car.gear = nfsu::kVehicleGearNeutral;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[4*4].u * 512) == 298,
          "neutral uses the N glyph");
    car.gear = nfsu::kVehicleGearReverse;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[4*4].u * 512) == 353,
          "reverse uses the R glyph");
    check(frame.quads == 7, "zero mph has one speed glyph");
    car.velocity.x = 45.0;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(frame.quads == 9, "100 mph has three speed glyphs");
    check(frame.vertices[12].color[3] == 255, "Tach face has full SAT alpha");
    const auto* gear=&frame.vertices[4*4];
    const auto* speed=&frame.vertices[5*4];
    const auto* units=&frame.vertices[8*4];
    check(gear[0].color[0]==0 && gear[0].color[1]==128 &&
          gear[0].color[2]==192 && gear[0].color[3]==255,
          "3rdPersonGear uses SAD 0x4680 blue at full alpha");
    check(speed[0].color[0]==0 && speed[0].color[1]==128 &&
          speed[0].color[2]==192 && speed[0].color[3]==255,
          "3rdPersonSpeed uses SAD 0x42F0 blue at full alpha");
    check(units[0].color[0]==150 && units[0].color[1]==190 &&
          units[0].color[2]==255 && units[0].color[3]==255,
          "3rdPersonSpeedUnits uses SAD 0x43EC at full alpha");
}

void test_layout_and_current_state() {
    nfsu::VehicleState car{};
    car.engine_rpm=800;
    car.wheel_rpm=0;
    car.gear=nfsu::kVehicleGearFirst;
    nfsu::RaceHudFrame full, half;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,full);
    nfsu::build_race_hud_frame_for_car(car,8000,7500,640,360,half);
    check(full.quads==half.quads,"CPU and presentation canvas use the same quad count");
    for (std::size_t i=0;i<full.quads*4U;++i) {
        check(std::abs(half.vertices[i].px*2-full.vertices[i].px)<0.001 &&
              std::abs(half.vertices[i].py*2-full.vertices[i].py)<0.001 &&
              half.vertices[i].u==full.vertices[i].u &&
              half.vertices[i].v==full.vertices[i].v &&
              std::memcmp(half.vertices[i].color,full.vertices[i].color,4)==0,
              "presentation size changes positions only; shared UV/tint data is identical");
    }
    const auto* gear=&full.vertices[4*4];
    const auto* speed=&full.vertices[5*4];
    check(std::abs((gear[0].px+gear[1].px)*0.5F-1231.3333F)<0.01 &&
          std::abs(gear[0].py-574.6667F)<0.01 &&
          std::abs(speed[0].py-656.0333F)<0.01,
          "text uses SAD centres, scale and measured font ascent");
    // A deliberately inconsistent wheel-derived value cannot override the
    // completed engine value. Reusing a frame must not retain the old gear.
    struct Sample { int gear; double rpm, wheel; };
    const Sample samples[]={{2,6500,9000},{3,4200,4300},{3,4800,9000},
                            {2,6200,6300},{2,800,-1000}};
    std::uint64_t step=10;
    for (const auto& s:samples) {
        car.step=++step; car.gear=s.gear; car.engine_rpm=s.rpm; car.wheel_rpm=s.wheel;
        nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,full);
        check(full.readout.physics_step==car.step && full.readout.gear==s.gear &&
              full.readout.engine_rpm==s.rpm && full.readout.wheel_rpm==s.wheel,
              "up/downshift readout uses one current car state without cached telemetry");
        check(std::abs(needle_angle(full)-(270-full.readout.rotation))<0.001,
              "the current engine readout and needle quad agree immediately across shifts");
    }
    const auto spec=nfsu::stock_240sx_vehicle_spec();
    const nfsu::VehicleModel model(spec);
    nfsu::VehiclePose pose; pose.speed=20; pose.gearbox=nfsu::VehicleGearbox::manual;
    check(model.reset(car,pose),"manual shift fixture reset");
    for (int i=0;i<6;++i) {
        nfsu::VehicleInput input;
        input.shift=i==2 ? nfsu::VehicleShift::up :
                    i==4 ? nfsu::VehicleShift::down : nfsu::VehicleShift::none;
        nfsu::VehicleTelemetry telemetry;
        check(model.step(car,input,nfsu::VehicleFlatGround(),telemetry),"manual shift fixture step");
        nfsu::build_race_hud_frame_for_car(car,spec.handling.max_rpm,
                                          spec.handling.redline_rpm,1280,720,full);
        check(full.readout.physics_step==telemetry.step &&
              full.readout.engine_rpm==telemetry.engine_rpm &&
              full.readout.gear==telemetry.gear,
              "completed model step, gear and HUD readout have the same timestamp");
        check(car.gear==(i>=2 && i<4 ? 3 : 2),"manual up/down fixture actually changes gear");
    }
    nfsu::build_race_hud_frame_for_car(car,8000,7500,0,0,full);
    check(full.quads==0,"zero-sized canvas emits no stale geometry");
}

int capture(const std::filesystem::path& root, const std::filesystem::path& out) {
    const auto source=std::filesystem::weakly_canonical(root);
    const auto target=std::filesystem::weakly_canonical(out);
    auto si=source.begin(), ti=target.begin();
    while (si!=source.end() && ti!=target.end() && *si==*ti) {++si; ++ti;}
    if (si==source.end() || std::filesystem::exists(out)) {
        std::fprintf(stderr,"race HUD: capture needs a fresh directory outside retail\n"); return 1;
    }
    nfsu::RaceHudAtlas atlas; std::string error;
    if (!nfsu::load_race_hud_atlas(root,atlas,error)) {
        std::fprintf(stderr,"race HUD: %s\n",error.c_str()); return 1;
    }
    std::filesystem::create_directories(out);
    struct Fixture { const char* name; double rpm,max,red,mph; int gear; };
    const Fixture fixtures[]={{"stock-idle",800,8000,7500,0,2},
        {"stock-driving",6200,8000,7500,62,3},
        {"engine-b-level2",6500,9000,8000,62,3}};
    for (const auto& fixture:fixtures) {
        nfsu::VehicleState car{}; car.engine_rpm=fixture.rpm; car.gear=fixture.gear;
        car.velocity.x=(fixture.mph+0.01)/2.237;
        nfsu::RaceHudFrame frame;
        nfsu::build_race_hud_frame_for_car(car,fixture.max,fixture.red,1280,720,frame);
        std::vector<unsigned char> pixels(1280*720*4,64);
        nfsu::composite_race_hud(atlas,frame,pixels.data(),1280,720,1280*4);
        nfsu::FrameWriteResult written;
        if (!nfsu::write_rgba_tga(1280,720,pixels,
                                  out/(std::string(fixture.name)+".tga"),{},written)) {
            std::fprintf(stderr,"race HUD: %s\n",written.error.c_str()); return 1;
        }
    }
    std::puts("PASS: three port-only 1280x720 HUD fixtures written; retail atlas decoded read-only");
    return 0;
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=1) {
        std::string error;
        if (!nfsu::check_arguments(argc,argv,1,error)) {
            std::fprintf(stderr,"race HUD: %s\n",error.c_str()); return 1;
        }
        if (argc==4 && std::strcmp(argv[1],"--capture")==0)
            return capture(nfsu::argument_path(argv[2]),nfsu::argument_path(argv[3]));
        std::fprintf(stderr,"Usage: nfsu_race_hud_tests [--capture RETAIL NEW_DIRECTORY]\n");
        return 1;
    }
    test_dial();
    test_rpm();
    test_gear_and_speed();
    test_layout_and_current_state();
    std::printf("race HUD: %s\n",failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
