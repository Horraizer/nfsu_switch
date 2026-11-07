// The vehicle on the track of issue #60 on the console: runs every built-in
// scenario of docs/VEHICLE_TRACK.md through nfsu_vehicle_track, compiled for
// AArch64 from the same sources and with the same floating-point options as
// the desktop, and prints each run's telemetry hash, how long it took and
// whether it equals the hash pinned on the desktop. The proving ground is
// built in code and always runs; the circuit - event 1 on track 1003 - runs
// when sdmc:/nfsu/Tracks/TRACKB1003.lzc is on the card, where
// docs/SWITCH_ASSETS.md puts it, and is read, never written. Event 1's race
// is two forward laps on a track whose lap-skip byte is clear, as
// `nfsu_probe race-events` reads it on the desktop; the console does not
// read the event tables. The report is on the screen and on stdout, which
// nxlink carries to the PC when it started the application. + exits.

#include "nfsu/sha256.h"
#include "nfsu/vehicle_track.h"

#include <switch.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Run {
    std::string hash;
    std::uint64_t records = 0;
    std::uint64_t ms = 0;
    bool completed = false;
};

Run run_scenario(const nfsu::VehicleModel& model, const nfsu::VehicleTrackCourse& course,
                 const nfsu::VehicleTrackScenario& scenario) {
    Run run;
    nfsu::VehicleTrackSession session(model, course);
    if (!session.valid() ||
        !session.start(nfsu::vehicle_track_scenario_pose(course.start, scenario))) {
        return run;
    }
    const std::uint64_t frequency = armGetSystemTickFreq();
    const std::uint64_t start = armGetSystemTick();
    nfsu::VehicleTrackTelemetryHash hash;
    nfsu::VehicleTrackTelemetry telemetry;
    const std::uint64_t steps = scenario.script.total_steps();
    for (std::uint64_t i = 0; i < steps; ++i) {
        if (!session.step(scenario.script.input_at(i), telemetry)) {
            return run;
        }
        hash.add(telemetry);
    }
    run.ms = (armGetSystemTick() - start) * 1000U / frequency;
    run.records = hash.records();
    run.hash = nfsu::sha256_hex(hash.finish());
    run.completed = true;
    return run;
}

// Runs every scenario of one course and counts the runs whose hash is the
// pinned one.
void run_course(const nfsu::VehicleModel& model, const nfsu::VehicleTrackCourse& course,
                nfsu::VehicleTrackCourseKind kind, int& equal, int& total) {
    for (const nfsu::VehicleTrackScenario& scenario : nfsu::vehicle_track_scenarios(kind)) {
        const Run run = run_scenario(model, course, scenario);
        ++total;
        if (!run.completed) {
            std::printf("%-13s stopped: the session refused a step\n", scenario.name.c_str());
            consoleUpdate(nullptr);
            continue;
        }
        const bool same = run.hash == scenario.pinned;
        equal += same ? 1 : 0;
        std::printf("%-13s records %llu ms %llu %s sha256 %s\n", scenario.name.c_str(),
                    static_cast<unsigned long long>(run.records),
                    static_cast<unsigned long long>(run.ms), same ? "EQUAL" : "DIFFERENT",
                    run.hash.c_str());
        consoleUpdate(nullptr);
    }
}

} // namespace

int main()
{
    consoleInit(nullptr);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // nxlink sets the host address when it starts the application; without
    // it there is nothing to connect to and the report stays on the screen.
    bool sockets = false;
    int nxlink_socket = -1;
    if (__nxlink_host.s_addr != 0 && R_SUCCEEDED(socketInitializeDefault())) {
        sockets = true;
        nxlink_socket = nxlinkStdio();
    }

    std::printf("nfsu_switch_vehicle_track: the vehicle on the track, issue #60\n");
    int equal = 0;
    int total = 0;
    const nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());
    if (!model.valid()) {
        std::printf("the model refuses the built-in 240SX: %s\n", model.error().c_str());
    } else {
        std::string error;
        nfsu::VehicleTrackCourse ground;
        if (!nfsu::build_vehicle_proving_ground(ground, error)) {
            std::printf("the proving ground did not build: %s\n", error.c_str());
        } else {
            std::printf("proving ground, built in code:\n");
            consoleUpdate(nullptr);
            run_course(model, ground, nfsu::VehicleTrackCourseKind::proving_ground, equal, total);
        }
        const char* trackb = "sdmc:/nfsu/Tracks/TRACKB1003.lzc";
        nfsu::VehicleTrackCourse circuit;
        if (!nfsu::load_vehicle_track_course(trackb, 2, 0, 0, 0, circuit, error)) {
            std::printf("circuit: %s did not load (%s); its runs are skipped\n", trackb,
                        error.c_str());
        } else {
            std::printf("circuit, event 1 on %s from grid slot 0:\n", trackb);
            consoleUpdate(nullptr);
            run_course(model, circuit, nfsu::VehicleTrackCourseKind::circuit, equal, total);
        }
    }
    std::printf("result: %d of %d runs EQUAL to the pinned desktop telemetry\n", equal, total);
    std::printf("Press + to exit.\n");

    // appletMainLoop returns false when the system asks the application to
    // close, so either way out of this loop reaches the shutdown below.
    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        consoleUpdate(nullptr);
    }

    if (nxlink_socket >= 0) {
        close(nxlink_socket);
    }
    if (sockets) {
        socketExit();
    }
    consoleExit(nullptr);
    return 0;
}
