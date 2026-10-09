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
    check(!parse_race_input("NFSU_RACE_INPUT 1\n1 " + std::to_string(kGamepadButtonMask+1U) + " 0\n",runs,error),
          "unknown button bit refused");
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
    check(race_session_hud(a)==std::vector<std::string>{"RACE HUD UNAVAILABLE"},"invalid HUD handled");
    check(!a.advance({},error),"unstarted race refused");
    check(a.start(error) && b.start(error),"sessions start");
    check(!a.start(error),"race is one shot");
    const auto origin=a.car().position;
    int countdown=0;
    while (race_car_held(a.timing()) && countdown<1000) {
        const auto pad=race_input_frame({1,0,0},countdown++);
        check(a.advance(pad,error) && b.advance(pad,error),"countdown steps");
        check(a.car().position.x==origin.x && a.car().position.z==origin.z,"countdown holds grid including gravity");
        check(a.car().step==0 && a.car().simulation_seconds==0,
              "held countdown does not consume solver clock");
        check(a.record()==b.record(),"countdown deterministic");
    }
    check(countdown>0 && countdown<1000,"countdown reaches GO");
    check(race_session_hud(a)[2]=="GO","GO is a message, not countdown 4");
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for (int i=0;i<120;++i) {
        auto pad=race_input_frame({1,gas,0},countdown+i);
        check(a.advance(pad,error) && b.advance(pad,error),"driving steps");
        check(a.record()==b.record(),"every integrated step is deterministic");
        if(i==0) {
            check(a.physics_telemetry().size()==4,"original ordinary branch exposes four slice records");
            for(std::size_t k=0;k<a.physics_telemetry().size();++k) {
                const auto& slice=a.physics_telemetry()[k];
                check(slice.model.solver_dt==1.0/240.0,"race uses source-backed solver interval");
                check(slice.tick==(k==3),"track checks run once after the fourth slice");
            }
        }
    }
    check(a.car().position.x>origin.x+1,"neutral countdown then gas really moves the car");
    check(a.telemetry().step==480,"four collision slices per input tick preserve two seconds of race time");
    const auto prior=a.camera().pov;
    auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::face_north)|gamepad_button_bit(GamepadButton::select),0},500);
    check(a.advance(pad,error),"camera and manual reset accepted");
    check(a.camera().pov!=prior,"camera cycles once");
    check(a.telemetry().resets==1,"manual reset reaches collision session");
    for(std::size_t k=0;k<a.physics_telemetry().size();++k)
        check((a.physics_telemetry()[k].reset!=VehicleTrackReset::none)==(k==3),
              "manual reset runs only at the final outer slice");
    check(a.car().step==0 && a.car().simulation_seconds==0,
          "manual reset restarts the mechanical clock");
    check(a.advance(pad,error) && a.telemetry().resets==1,"holding reset does not reset every tick");
    check(a.car().step==4 && std::abs(a.car().simulation_seconds-1.0/60.0)<1e-12,
          "held reset resumes exactly one outer interval");
    check(race_session_hud(a).size()==4,"minimal HUD available");
    // R3 is held, not a POV cycle. The normal camera must keep following
    // during the hold and return immediately to its current selected view.
    {
        DriveControls controls;
        const auto rear=gamepad_button_bit(GamepadButton::stick_right);
        auto p=race_input_frame({1,rear,0},0);
        controls.update(p);
        check(!controls.frame_state().look_back,"R3 hot-plug hold remains latched");
        p.pads[0].buttons=0;controls.update(p);
        p.pads[0].buttons=rear;controls.update(p);
        check(controls.frame_state().look_back&&!controls.frame_state().camera,"R3 requests held rear view without cycling");
        controls.update(p);
        check(controls.frame_state().look_back,"R3 remains active while held");
        p.pads[0].connected=false;controls.update(p);
        check(!controls.frame_state().look_back,"disconnect releases rear view");

        RaceSession normal(course),rear_session(course);
        check(normal.start(error)&&rear_session.start(error),"rear camera sessions start");
        unsigned tick=0;
        const auto advance_pair=[&](std::uint32_t buttons,bool rear_held,int axis=0) {
            auto n=race_input_frame({1,buttons,axis},tick++),r=n;
            if(rear_held)r.pads[0].buttons|=rear;
            check(normal.advance(n,error)&&rear_session.advance(r,error),"rear camera pair advances");
            check(normal.car().position.x==rear_session.car().position.x&&
                  normal.car().position.y==rear_session.car().position.y&&
                  normal.car().position.z==rear_session.car().position.z,
                  "rear camera does not change vehicle motion");
        };
        advance_pair(0,false);
        while(race_car_held(normal.timing())&&tick<1000)advance_pair(0,false);
        for(int moving=0;moving<120;++moving)advance_pair(gas,false);
        for(int pov=0;pov<3;++pov) {
            for(int hold=0;hold<12;++hold) {
                advance_pair(gas,true,-6000);
                const auto subject=chase_subject(rear_session.car(),rear_session.spec());
                const auto& r=rear_session.camera();const auto& n=normal.camera();
                check(r.pov==n.pov,"R3 preserves selected POV");
                check((r.eye.x-subject.origin.x)*subject.forward.x+
                      (r.eye.y-subject.origin.y)*subject.forward.y>0,
                      "held rear view is ahead of the car");
                check(r.forward.x*subject.forward.x+r.forward.y*subject.forward.y<0,
                      "held rear view looks backward toward the front of the car");
            }
            advance_pair(0,false);
            const auto& n=normal.camera();const auto& r=rear_session.camera();
            check(r.eye.x==n.eye.x&&r.eye.y==n.eye.y&&r.eye.z==n.eye.z&&
                  r.heading==n.heading&&r.pitch==n.pitch&&r.pov==n.pov,
                  "release restores current ordinary view without orbit or stale history");
            advance_pair(gamepad_button_bit(GamepadButton::face_north),false);
            advance_pair(0,false);
        }
    }
    RaceCameraWorld camera(course.world);
    check(camera.clear_fraction({32,48,20},{32,48,0})<1,"camera segment hits synthetic road");
    check(camera.clear_fraction({32,48,20},{33,48,20})==1,"camera segment clear above road");

    check(camera.clear_fraction({32,48,20},{5032,48,20})==0,"refused long camera segment blocks camera");

    // Drive a complete synthetic race through controls and collision. No
    // fabricated route events: catch lost lap/finish forwarding in the glue.
    VehicleTrackCourse two_laps=course;
    check(build_race_route(two_laps.track,2,0,0,two_laps.route,error),"two-lap synthetic route");
    RaceSession race(two_laps); RaceRouteDriver driver; bool saw_lap=false;
    check(race.start(error),"synthetic race starts");
    while (race.timing().outcome==RaceOutcome::running && race.timing().step<12000) {
        if (!race.advance(driver.sample(race),error)) { check(false,"synthetic race advances"); break; }
        const auto& result=race.timing().racers[0];
        if (!saw_lap && result.highest_lap==1) {
            saw_lap=true;
            check(result.lap_ticks[1]>0 && race.timing().outcome==RaceOutcome::running,"first lap does not finish two-lap race");
            const auto hud=race_session_hud(race);
            check(hud[0]=="EVENT 1  LAP 2 / 2","HUD advances lap");
            check(hud[1]=="CURRENT "+format_race_ticks(race.timing().step_ticks-result.crossing_ticks[0])+
                  "  LAST "+format_race_ticks(result.lap_ticks[1]),"HUD starts next lap immediately and preserves last lap");
        }
    }
    check(saw_lap,"lap event reaches integration");
    check(race.timing().outcome==RaceOutcome::finished,"integrated synthetic race finishes");
    if (race.timing().outcome==RaceOutcome::finished) {
        const auto before=race.record(); const auto step=race.timing().step;
        const auto pose=race.car().position; const auto hud=race_session_hud(race);
        const auto& result=race.timing().racers[0];
        check(result.highest_lap==2 && result.lap_ticks[2]>0,"completed lap reaches timing");
        check(hud[1]=="CURRENT "+format_race_ticks(result.lap_ticks[2])+"  LAST "+format_race_ticks(result.lap_ticks[2]),
              "finished HUD uses completed lap time");
        check(hud[2]=="FINISHED "+format_race_ticks(result.finish_ticks),"finish reaches HUD");
        check(!race.advance(race_input_frame({1,gas,32767},step),error),"advance after finish refused");
        check(race.timing().step==step && race.record()==before && race.car().position.x==pose.x &&
              race.car().position.y==pose.y && race_session_hud(race)==hud,"finish freezes physics timing record and HUD");
    }

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
