#include "nfsu/race_session.h"
#include <iostream>

namespace {
int failures=0;
void check(bool okay,const char* label) {
    if (!okay) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
}
int main() {
    using namespace nfsu;
    std::string error; std::vector<RaceInputRun> runs;
    for (const auto* text : {"", "NFSU_RACE_INPUT 2\n1 0 0\n", "NFSU_RACE_INPUT 1\n0 0 0\n",
        "NFSU_RACE_INPUT 1\n120000 0 0\n1 0 0\n", "NFSU_RACE_INPUT 1\n1 0 32768\n",
        "NFSU_RACE_INPUT 1\n1 -1 0\n", "NFSU_RACE_INPUT 1\n1 0 0 extra\n"})
        check(!parse_race_input(text,runs,error),"malformed input refused");
    check(!parse_race_input(std::string("NFSU_RACE_INPUT 1\n1 0 0\n")+char(128),runs,error),"non-ASCII refused");
    check(parse_race_input("# controller only\nNFSU_RACE_INPUT 1\r\n3 0 -32768\n2 0 32767\n",runs,error),"limits accepted");
    const auto canonical=format_race_input(runs);
    check(parse_race_input(canonical,runs,error) && format_race_input(runs)==canonical,"recording round trip");
    std::vector<RaceInputRun> compressed;
    for (int i=0;i<3;++i) append_race_input(compressed,race_input_frame(runs[0],i));
    check(compressed.size()==1 && compressed[0].ticks==3 && compressed[0].steer==-32768,"RLE keeps full steering range");

    VehicleTrackCourse course;
    check(build_vehicle_proving_ground(course,error),"synthetic course builds");
    RaceSession a(course),b(course);
    check(!a.advance({},error),"unstarted race refused");
    check(a.start(error) && b.start(error),"sessions start");
    check(!a.start(error),"race is one shot");
    const auto origin=a.car().position;
    int countdown=0;
    while (race_car_held(a.timing()) && countdown<1000) {
        const auto pad=race_input_frame({1,0,0},countdown++);
        check(a.advance(pad,error) && b.advance(pad,error),"countdown steps");
        check(a.car().position.x==origin.x && a.car().position.z==origin.z,"countdown holds grid including gravity");
        check(a.record()==b.record(),"countdown deterministic");
    }
    check(countdown>0 && countdown<1000,"countdown reaches GO");
    check(race_session_hud(a)[2]=="GO","GO is a message, not countdown 4");
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for (int i=0;i<120;++i) {
        auto pad=race_input_frame({1,gas,0},countdown+i);
        check(a.advance(pad,error) && b.advance(pad,error),"driving steps");
        check(a.record()==b.record(),"every integrated step is deterministic");
    }
    check(a.car().position.x>origin.x+1,"neutral countdown then gas really moves the car");
    check(a.telemetry().step==240,"two collision steps per input tick");
    const auto prior=a.camera().pov;
    auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::face_north)|gamepad_button_bit(GamepadButton::select),0},500);
    check(a.advance(pad,error),"camera and manual reset accepted");
    check(a.camera().pov!=prior,"camera cycles once");
    check(a.telemetry().resets==1,"manual reset reaches collision session");
    check(a.advance(pad,error) && a.telemetry().resets==1,"holding reset does not reset every tick");
    check(race_session_hud(a).size()==4,"minimal HUD available");
    RaceCameraWorld camera(course.world);
    check(camera.clear_fraction({32,48,20},{32,48,0})<1,"camera segment hits synthetic road");
    check(camera.clear_fraction({32,48,20},{33,48,20})==1,"camera segment clear above road");

    Frame track,car;
    RenderOptions options; options.width=8; options.height=8;
    RenderLimits limits; RenderStats stats;
    const auto triangle=[&](Frame& frame,double z,unsigned char red) {
        check(frame.reset(8,8,options.background,limits,error),"composite frame reset");
        ClipVertex vertices[3];
        vertices[0].x=-1; vertices[0].y=-1;
        vertices[1].x=3; vertices[1].y=-1;
        vertices[2].x=-1; vertices[2].y=3;
        for (auto& vertex : vertices) { vertex.w=1; vertex.z=z; }
        RasterMaterial material; material.fallback_pattern=false; material.fallback[0]=red;
        Rasterizer raster(frame,options,limits,stats);
        check(raster.draw_triangle(vertices,material,0,error),"composite test triangle draws");
    };
    std::vector<unsigned char> pixels;
    triangle(track,0.5,10); triangle(car,0.7,20);
    check(compose_race_frame(track,car,pixels) && pixels[0]==10,"track occludes farther car");
    triangle(car,0.3,20);
    check(compose_race_frame(track,car,pixels) && pixels[0]==20,"nearer car occludes track");
    triangle(car,0.5,30);
    check(compose_race_frame(track,car,pixels) && pixels[0]==30,"car wins exact depth tie");
    check(car.reset(8,8,options.background,limits,error),"empty car frame");
    check(compose_race_frame(track,car,pixels) && pixels[0]==10,"empty pixels never hide track");
    Frame wrong;
    check(!compose_race_frame(track,wrong,pixels),"different frame size refused");
    std::cout << "race session tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
