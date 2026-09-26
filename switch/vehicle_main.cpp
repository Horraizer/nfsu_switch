// The vehicle model of issue #58 on the console: runs every built-in
// scripted input sequence of docs/VEHICLE.md through nfsu_vehicle, compiled
// for AArch64 from the same sources and with the same floating-point options
// as the desktop, and prints each run's telemetry hash and how long it took.
// For "mixed" it also says whether the hash is the one pinned on the
// desktop, kVehiclePinnedMixedSha256. It reads no file and writes none; the
// report is on the screen and on stdout, which nxlink carries to the PC when
// it started the application. + exits. docs/VEHICLE.md, "On the console".

#include "nfsu/sha256.h"
#include "nfsu/vehicle.h"

#include <switch.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

struct Run {
    std::string hash;
    std::uint64_t records = 0;
    bool completed = false;
};

Run run_script(const nfsu::VehicleModel& model, const nfsu::VehicleScript& script) {
    Run run;
    nfsu::VehiclePose pose;
    pose.speed = script.speed;
    pose.gearbox = script.gearbox;
    nfsu::VehicleState state;
    if (!model.reset(state, pose)) {
        return run;
    }
    const nfsu::VehicleFlatGround ground(0.0);
    nfsu::VehicleTelemetryHash hash;
    nfsu::VehicleTelemetry telemetry;
    const std::uint64_t steps = script.total_steps();
    for (std::uint64_t i = 0; i < steps; ++i) {
        if (!model.step(state, script.input_at(i), ground, telemetry)) {
            return run;
        }
        hash.add(telemetry);
    }
    run.records = hash.records();
    run.hash = nfsu::sha256_hex(hash.finish());
    run.completed = true;
    return run;
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

    std::printf("nfsu_switch_vehicle: the vehicle model of issue #58, flat ground\n");
    const nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());
    if (!model.valid()) {
        std::printf("the model refuses the built-in 240SX: %s\n", model.error().c_str());
    } else {
        const std::uint64_t frequency = armGetSystemTickFreq();
        for (const std::string& name : nfsu::builtin_vehicle_script_names()) {
            nfsu::VehicleScript script;
            if (!nfsu::builtin_vehicle_script(name, script)) {
                continue;
            }
            const std::uint64_t start = armGetSystemTick();
            const Run run = run_script(model, script);
            const std::uint64_t ticks = armGetSystemTick() - start;
            const unsigned long long ms =
                static_cast<unsigned long long>(ticks * 1000U / frequency);
            if (!run.completed) {
                std::printf("%-10s stopped: the model refused a step\n", name.c_str());
                continue;
            }
            std::printf("%-10s records %llu ms %llu sha256 %s\n", name.c_str(),
                        static_cast<unsigned long long>(run.records), ms, run.hash.c_str());
            if (name == "mixed") {
                std::printf("result: %s the pinned desktop telemetry\n",
                            run.hash == nfsu::kVehiclePinnedMixedSha256 ? "EQUAL to"
                                                                        : "DIFFERENT from");
            }
            consoleUpdate(nullptr);
        }
    }
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
