#include "nfsu/race_hud.h"
#include <limits>
#include "nfsu/arg_path.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;
void check(bool okay, const char* name) {
    if (!okay) { std::fprintf(stderr, "race HUD: %s\n", name); ++failures; }
}

struct Point { double x, y; };

std::pair<Point, Point> needle_ends(const nfsu::RaceHudFrame& frame,
                                    bool red = true) {
    // Fill, optional RED and Tach precede Needle. Its ring is at row 64;
    // the shaft ends near 125.
    const auto* v = &frame.vertices[(red ? 3 : 2) * 4];
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

// Geometry fixtures explicitly set the display target. Timing is tested below.
void build_needle_fixture(const nfsu::VehicleState& car, double max, double red,
                     std::uint32_t width, std::uint32_t height,
                     nfsu::RaceHudFrame& frame) {
    nfsu::build_race_hud_frame_for_car(car,max,red,width,height,frame);
}

void test_needle_cadence() {
    nfsu::VehicleState car{};
    car.gear=nfsu::kVehicleGearFirst;
    nfsu::RaceHudFrame sixty,thirty,eight;
    const auto draw=[&](nfsu::RaceHudFrame& frame) {
        nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    };
    for (unsigned tick=1;tick<=60;++tick) {
        car.step=tick*2;car.engine_rpm=5000+tick*50;
        draw(sixty);
        if (tick%2==0) draw(thirty);
        if (tick%8==0) draw(eight);
    }
    // Catch-up may present once after multiple committed ticks.
    draw(eight);
    check(sixty.readout.rotation==thirty.readout.rotation &&
          sixty.readout.rotation==eight.readout.rotation,
          "presentation frequency and catch-up do not change the completed-state needle");
    car.engine_rpm=3000;car.gear++;
    draw(sixty);
    check(sixty.readout.engine_rpm==3000 &&sixty.readout.gear==car.gear &&
          sixty.readout.rotation==nfsu::race_hud_needle_rotation(3000,8000),
          "original full physics engine RPM and gear reach the HUD together");
    const auto paused=sixty;
    for(unsigned present=0;present<30;++present) draw(sixty);
    check(sixty.readout.rotation==paused.readout.rotation &&sixty.quads==paused.quads &&
          std::memcmp(sixty.vertices.data(),paused.vertices.data(),sizeof(sixty.vertices))==0,
          "paused and repeated frames leave all needle geometry unchanged");
    car.step=0;car.engine_rpm=800;car.gear=nfsu::kVehicleGearFirst;
    draw(sixty);
    nfsu::RaceHudFrame reentry;draw(reentry);
    check(sixty.readout.rotation==reentry.readout.rotation &&
          std::memcmp(sixty.vertices.data(),reentry.vertices.data(),sizeof(sixty.vertices))==0,
          "reset and fresh entry have identical geometry without stale follower state");
}

void test_needle_endpoint(const nfsu::RaceHudFrame& frame,
                          double label_x, double label_y, bool red = true) {
    const auto* face=&frame.vertices[(red ? 2 : 1)*4];
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
    const auto* face=&frame.vertices[(red ? 2 : 1)*4];
    const double u0=face[0].u*nfsu::kRaceHudAtlasWidth, v0=face[0].v*512.0;
    const double u1=face[1].u*nfsu::kRaceHudAtlasWidth, v1=face[2].v*512.0;
    check(std::abs((u1-u0)-256.0)<0.01 &&
          std::abs((v1-v0)-128.0)<0.01,
          "LINES samples its full 256x128 retail sprite");
    const double pitch=(face[1].px-face[0].px)/256.0;
    const double hub_x=face[0].px+69.0*pitch;
    const double hub_y=face[0].py+70.0*pitch;
    check(std::abs(hub_x-1133.0)<0.01 &&
          std::abs(hub_y-(601.0-pitch))<0.01,
          "decoded LINES hub uses measured anchor and recovered SAT offset");
    const auto* needle=&frame.vertices[(red ? 3 : 2)*4];
    check(std::abs((needle[1].u-needle[0].u)*nfsu::kRaceHudAtlasWidth-16.0)<0.01 &&
          std::abs((needle[3].v-needle[0].v)*512.0-128.0)<0.01,
          "needle samples the complete 16x128 retail sprite");
    check(needle[0].color[0]==207 && needle[0].color[1]==233 &&
          needle[0].color[2]==242 && needle[0].color[3]==255,
          "3rdPersonNeedle uses SAT 0x457C BGRA (242,233,207,255)");
    const auto* fill=&frame.vertices[0];
    const auto* overlay=&frame.vertices[1*4];
    check(std::lround(fill[0].u*nfsu::kRaceHudAtlasWidth)==0 &&
          std::lround(fill[0].v*512)==256 &&
          std::lround(needle[0].u*nfsu::kRaceHudAtlasWidth)==256 &&
          std::lround(needle[0].v*512)==256,
          "needle samples its retail atlas after the Tach face");
    check(fill[0].px==face[0].px && fill[0].py==face[0].py &&
          fill[2].px==face[2].px && fill[2].py==face[2].py &&
          fill[0].color[3]==150,
          "fill uses the complete shared SAT rectangle and alpha");
    if (red)
        check(overlay[0].color[0]==150 && overlay[0].color[1]==0 &&
              overlay[0].color[2]==0 && overlay[0].color[3]==150 &&
              face>overlay && needle>face,
              "draw order is fill, Redline, Tach, then Needle");
    else
        check(face>fill && needle>face,
              "without Redline, draw order is fill, Tach, then Needle");
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
        // Each independent geometry fixture uses the completed car state.
        frame = nfsu::RaceHudFrame{};
        const auto dial=nfsu::select_race_hud_dial(item.max,item.red);
        check(dial.lines_hash==item.lines && dial.red_hash==item.overlay,
              "retail max and redline select DYNTEX LINES and RED");
        nfsu::build_race_hud_frame_for_car(car,item.max,item.red,1280,720,frame);
        const auto face_index=(item.overlay ? 2 : 1)*4;
        check(std::lround(frame.vertices[face_index].u*nfsu::kRaceHudAtlasWidth)==item.x &&
              std::lround(frame.vertices[face_index].v*512)==item.y,
              "frame uses the selected LINES atlas slot");
        if (item.overlay) {
            check(std::lround(frame.vertices[1*4].u*nfsu::kRaceHudAtlasWidth)==item.rx &&
                  std::lround(frame.vertices[1*4].v*512)==item.ry,
                  "frame uses the selected RED atlas slot");
        } else {
            check(frame.quads==8,"below 6500 in the 8500LINES band draws no RED");
            check(std::abs(needle_angle(frame,false)-226.0)<1.0,
                  "no RED keeps the needle at the stock zero angle");
        }
        test_face_registration(frame,item.overlay!=0);
        if (item.max==8000.0 || item.max==9000.0) {
            test_needle_endpoint(frame,34.0,105.0,item.overlay!=0);
            car.engine_rpm=item.max;
            build_needle_fixture(car,item.max,item.red,1280,720,frame);
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
        build_needle_fixture(car,8000,7500,1280,720,frame);
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
    for (const auto& sample : {std::pair<double,double>{8999,44},
                              {9000,50}, {9999,50}, {10000,45}})
        check(std::abs(nfsu::race_hud_needle_rotation(0,sample.first)-sample.second)<0.001,
              "zero RPM selects the exact piecewise max-RPM boundary");
    check(std::abs(nfsu::race_hud_needle_rotation(8000,9000)-192.222222222)<0.001 &&
          std::abs(nfsu::race_hud_needle_rotation(9000,10000)-207.0)<0.001,
          "explicit upgraded redlines use their own needle range");
}

void test_needle_over_scale() {
    // An opaque synthetic scale mark crosses the real needle geometry at
    // numeral radius. This verifies compositing, not only vertex ordinals.
    nfsu::RaceHudAtlas atlas;
    atlas.rgba.assign(nfsu::kRaceHudAtlasWidth*512*4,0);
    const auto white=[&](int left,int top,int width,int height) {
        for (int y=top;y<top+height;++y) for (int x=left;x<left+width;++x)
            for (int channel=0;channel<4;++channel)
                atlas.rgba[(y*nfsu::kRaceHudAtlasWidth+x)*4+channel]=255;
    };
    white(256,0,256,128); // stock LINES slot
    white(256,256,16,128); // Needle
    for (const int width : {640,1280}) for (const bool red : {false,true})
        for (const double rpm : {0.0,3000.0,6200.0,8000.0}) {
            const int height=width*9/16;
            nfsu::VehicleState car{};
            car.engine_rpm=rpm;
            nfsu::RaceHudFrame frame;
            nfsu::build_race_hud_frame_for_car(car,8000,red ? 7500 : 6200,
                                               width,height,frame);
            const auto [ring,tip]=needle_ends(frame,red);
            const int x=static_cast<int>(std::floor(ring.x+(tip.x-ring.x)*0.85));
            const int y=static_cast<int>(std::floor(ring.y+(tip.y-ring.y)*0.85));
            std::vector<unsigned char> pixels(width*height*4,17);
            nfsu::composite_race_hud(atlas,frame,pixels.data(),width,height,width*4);
            const auto at=(y*width+x)*4;
            check(pixels[at]==207 && pixels[at+1]==233 && pixels[at+2]==242,
                  "needle remains visible over an overlapping scale numeral");
            check(pixels[0]==17 && pixels[1]==17 && pixels[2]==17,
                  "layer correction leaves pixels outside HUD untouched");
            // Negative control recreates the deployed ordering defect.
            const int face=red ? 2 : 1, needle=face+1;
            for (int i=0;i<4;++i)
                std::swap(frame.vertices[face*4+i],frame.vertices[needle*4+i]);
            std::fill(pixels.begin(),pixels.end(),17);
            nfsu::composite_race_hud(atlas,frame,pixels.data(),width,height,width*4);
            check(pixels[at]==0 && pixels[at+1]==128 && pixels[at+2]==192,
                  "legacy order actually hides the needle at the same crossing");
        }
}

void test_gear_and_speed() {
    nfsu::VehicleState car{};
    nfsu::RaceHudFrame frame;
    car.gear = nfsu::kVehicleGearNeutral;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[4*4].u * nfsu::kRaceHudAtlasWidth) == 293,
          "neutral uses the N glyph");
    car.gear = nfsu::kVehicleGearReverse;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(std::lround(frame.vertices[4*4].u * nfsu::kRaceHudAtlasWidth) == 348,
          "reverse uses the R glyph");
    check(frame.quads == 9, "zero mph has one speed glyph and three unit letters");
    car.velocity.x = 45.0;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,frame);
    check(frame.quads == 11, "100 mph has three speed glyphs and three unit letters");
    check(frame.vertices[8].color[3] == 255, "Tach face has full SAT alpha");
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
    check(std::abs(gear[0].px-1217.1667F)<0.01 &&
          std::abs(gear[0].py-570.4167F)<0.01 &&
          std::abs(speed[0].px-1149.65F)<0.01 &&
          std::abs(speed[0].py-653.0333F)<0.01,
          "right-aligned SAD text uses native LCD bearings and advance");
    // Narrow 1 and wide 0 have different bearings and advances. Their
    // pen endpoints share the original right anchor for one/three digits.
    car.velocity.x=45.0;
    nfsu::build_race_hud_frame_for_car(car,8000,7500,1280,720,full);
    speed=&full.vertices[5*4];
    const auto* second=&full.vertices[6*4];
    const auto* third=&full.vertices[7*4];
    check(std::abs(speed[0].px-1115.65F)<0.01 &&
          std::abs(second[0].px-1125.65F)<0.01 &&
          std::abs(third[0].px-1149.65F)<0.01 &&
          std::abs((speed[1].px-speed[0].px)-13.0F)<0.01 &&
          std::lround(speed[0].u*nfsu::kRaceHudAtlasWidth)==375 &&
          std::lround(speed[0].v*512)==291,
          "100 mph uses native proportional advance and padded 1 UVs");
    const auto* units=&full.vertices[8*4];
    check(std::abs(units[0].px-1192.4F)<0.01 &&
          std::abs(units[0].py-635.2833F)<0.01 &&
          std::abs((units[1].px-units[0].px)-17.3333F)<0.01 &&
          std::lround(units[0].u*nfsu::kRaceHudAtlasWidth)==400 &&
          std::lround(units[1].u*nfsu::kRaceHudAtlasWidth)==415,
          "MPH uses native italic M metrics and the FEng 0.8 scale");
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

bool resolved_directory(const std::filesystem::path& directory,
                        std::filesystem::path& resolved) {
#ifdef _WIN32
    // MinGW weakly_canonical does not resolve directory junctions. Resolve
    // the existing directory through its handle, including nested aliases.
    const HANDLE handle=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if (handle==INVALID_HANDLE_VALUE) return false;
    const DWORD size=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED);
    std::vector<wchar_t> name(size+1);
    const DWORD length=size ? GetFinalPathNameByHandleW(handle,name.data(),
        static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED) : 0;
    CloseHandle(handle);
    if (!length || length>=name.size()) return false;
    std::wstring text(name.data(),length);
    // The standard library used here cannot open extended-prefix paths.
    if (text.compare(0,8,L"\\\\?\\UNC\\")==0) text=L"\\\\"+text.substr(8);
    else if (text.compare(0,4,L"\\\\?\\")==0) text=text.substr(4);
    resolved=std::filesystem::path(text);
    return true;
#else
    std::error_code error;
    resolved=std::filesystem::canonical(directory,error);
    return !error;
#endif
}

bool fresh_capture_outside(const std::filesystem::path& root,
                           const std::filesystem::path& out) {
    namespace fs=std::filesystem;
    std::error_code error;
    if (!fs::is_directory(root,error) || error) return false;
    const auto target=fs::absolute(out,error);
    if (error) return false;
#ifdef _WIN32
    // Refuse every existing entry, including a dangling reparse point.
    if (GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES) return false;
    const DWORD status=GetLastError();
    if (status!=ERROR_FILE_NOT_FOUND && status!=ERROR_PATH_NOT_FOUND) return false;
#else
    if (fs::symlink_status(target,error).type()!=fs::file_type::not_found) return false;
    error.clear();
#endif
    // Resolve the deepest existing parent before walking its physical
    // ancestors. Identity comparisons cover case, short names and aliases;
    // a lexical prefix test cannot establish containment on this filesystem.
    auto parent=target.parent_path();
    while (!parent.empty()) {
        if (fs::exists(parent,error)) {
            if (error || !fs::is_directory(parent,error) || error) return false;
            fs::path physical;
            if (!resolved_directory(parent,physical)) return false;
            while (!physical.empty()) {
                if (fs::equivalent(root,physical,error) || error) return false;
                const auto next=physical.parent_path();
                if (next==physical) break;
                physical=next;
            }
            return true;
        }
        if (error) return false;
        const auto next=parent.parent_path();
        if (next==parent) break;
        parent=next;
    }
    return false;
}

int capture(const std::filesystem::path& root, const std::filesystem::path& out) {
    if (!fresh_capture_outside(root,out)) {
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
        nfsu::RaceFpsHistory history;
        for (int i=0;i<90;++i) history.add(i==60 ? 50 : 1000.0/60);
        nfsu::append_race_fps_overlay(atlas,history,1280,720,frame);
        check(frame.quads< nfsu::kRaceHudMaxQuads,"FPS overlay fits fixed HUD capacity");
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

bool directory_alias(const std::filesystem::path& target,
                     const std::filesystem::path& link) {
#ifdef _WIN32
    // A junction needs no symlink privilege. All names belong to the fresh
    // test fixture; no subprocess or narrow path conversion is involved.
    std::error_code error;
    if (!std::filesystem::create_directory(link,error) || error) return false;
    const HANDLE handle=CreateFileW(link.c_str(),GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (handle==INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return false; }
    const auto name=std::filesystem::absolute(target).native();
    const auto substitute=L"\\??\\"+name;
    const std::size_t bytes=(substitute.size()+name.size()+2)*sizeof(wchar_t);
    if (bytes>0xffff-8) { CloseHandle(handle); RemoveDirectoryW(link.c_str()); return false; }
    struct Header { DWORD tag; WORD bytes, reserved, offset, length, print_offset, print_length; };
    static_assert(sizeof(Header)==16,"junction reparse header layout");
    std::vector<unsigned char> data(sizeof(Header)+bytes,0);
    const Header header{IO_REPARSE_TAG_MOUNT_POINT,static_cast<WORD>(bytes+8),0,0,
        static_cast<WORD>(substitute.size()*sizeof(wchar_t)),
        static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)),
        static_cast<WORD>(name.size()*sizeof(wchar_t))};
    std::memcpy(data.data(),&header,sizeof(header));
    std::memcpy(data.data()+sizeof(header),substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
    std::memcpy(data.data()+sizeof(header)+(substitute.size()+1)*sizeof(wchar_t),
                name.c_str(),(name.size()+1)*sizeof(wchar_t));
    DWORD unused=0;
    const bool okay=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,data.data(),
        static_cast<DWORD>(data.size()),nullptr,0,&unused,nullptr)!=0;
    CloseHandle(handle);
    if (!okay) RemoveDirectoryW(link.c_str());
    return okay;
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target,link,error);
    return !error;
#endif
}

void test_capture_paths() {
    namespace fs=std::filesystem;
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    const auto base=fs::current_path()/(std::string("hud253-capture-")+std::to_string(pid));
    std::error_code error;
    // Never remove a pre-existing fixture, even if a process ID was reused.
    if (!fs::create_directory(base,error) || error) {
        check(false,"fresh capture safety fixture created"); return;
    }
    const auto root=base/"protected";
    fs::create_directories(root/"nested");
    fs::create_directory(base/"existing-output");
    fs::create_directory(base/"protected-sibling");
    check(!fresh_capture_outside(root,root/"new-output"),"direct protected descendant refused");
    check(!fresh_capture_outside(root,root/"missing"/"deeper"),"missing protected descendants refused");
    check(!fresh_capture_outside(root,base/"existing-output"),"existing output refused");
    check(fresh_capture_outside(root,base/"new-output"),"fresh outside sibling accepted");
    check(fresh_capture_outside(root,base/"protected-sibling"/"new-output"),
          "similar lexical prefix outside protected root accepted");
    const bool same=fs::equivalent(root,base/"PROTECTED",error);
    check(fresh_capture_outside(root,base/"PROTECTED"/"new-output")!=same,
          "case alias follows actual filesystem identity");
    struct AliasCase { const char* name; fs::path target; bool inside; };
    const AliasCase aliases[]={{"root-alias",root,true},
        {"nested-alias",root/"nested",true},
        {"outside-alias",base/"protected-sibling",false}};
    for (const auto& item : aliases) {
        const auto alias=base/item.name;
        const bool made=directory_alias(item.target,alias);
        check(made,"directory alias created in writable fixture");
        if (made) {
            check(!fresh_capture_outside(root,alias),"existing alias output refused");
            check(fresh_capture_outside(root,alias/"new-output")!=item.inside,
                  "root/nested aliases refused and outside alias accepted");
            check(fresh_capture_outside(root,alias/"missing"/"deeper")!=item.inside,
                  "uncreated descendants through alias follow protected identity");
#ifdef _WIN32
            check(RemoveDirectoryW(alias.c_str())!=0,"only junction entry removed");
#else
            check(fs::remove(alias,error) && !error,"only directory symlink removed");
#endif
        }
    }
    check(fs::is_empty(root/"nested"),"protected fixture remains untouched");
    fs::remove_all(base,error);
    check(!error,"own capture safety fixture cleaned");
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
    {
        nfsu::RaceFpsHistory history;
        history.add(0); history.add(-1); history.add(std::numeric_limits<double>::infinity());
        check(history.count==0,"FPS rejects invalid intervals");
        for (int i=0;i<90;++i) history.add(1000.0/60);
        check(std::abs(1000/history.mean_ms()-60)<1e-9,"FPS uses complete presentation intervals");
        history.add(100);
        check(history.count==90 && history.intervals[89]==100 && history.mean_ms()>17,
              "FPS retains a long stutter and evicts oldest sample");
    }
    test_dial();
    test_rpm();
    test_needle_cadence();
    test_needle_over_scale();
    test_gear_and_speed();
    test_layout_and_current_state();
    test_capture_paths();
    std::printf("race HUD: %s\n",failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
