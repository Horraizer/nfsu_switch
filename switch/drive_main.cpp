// The driving controls and the chase camera of issue #59 on the console.
//
// First it replays the built-in demonstration of docs/CONTROLS_CAMERA.md
// through nfsu_drive_session - the controls, the vehicle model on flat
// ground and the chase camera, compiled for AArch64 from the same sources
// and with the same floating-point options as the desktop - and prints the
// stream's hash, how long it took, and whether it is the one pinned on the
// desktop, kDrivePinnedDemoSha256.
//
// Then it drives: the pad is sampled once a frame into a GamepadFrame, the
// way docs/CONTROLS_CAMERA.md says the race application will, and each
// sample is one 1/60 s tick of the same session. There is nothing to see
// yet but numbers - the speed, the gear, the inputs as applied and the
// camera - on the text console; the picture waits for the deko3d renderer.
// + ends the application; - is the controls' reset request, which puts the
// car back at the start here because issue #60's reset is not there yet.
// It reads no file and writes none.

#include "nfsu/drive_session.h"
#include "nfsu/sha256.h"
#include "nfsu/stock_view.h"

#include <switch.h>

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// The GamepadButton mapping is neutral code a desktop test checks
// (nfsu::gamepad_buttons_from_npad); this holds it to libnx's own values.
static_assert(nfsu::kNpadButtonZL == static_cast<std::uint64_t>(HidNpadButton_ZL), "ZL");
static_assert(nfsu::kNpadButtonZR == static_cast<std::uint64_t>(HidNpadButton_ZR), "ZR");
static_assert(nfsu::kNpadButtonB == static_cast<std::uint64_t>(HidNpadButton_B), "B");
static_assert(nfsu::kNpadButtonX == static_cast<std::uint64_t>(HidNpadButton_X), "X");
static_assert(nfsu::kNpadButtonL == static_cast<std::uint64_t>(HidNpadButton_L), "L");
static_assert(nfsu::kNpadButtonR == static_cast<std::uint64_t>(HidNpadButton_R), "R");
static_assert(nfsu::kNpadButtonMinus == static_cast<std::uint64_t>(HidNpadButton_Minus), "Minus");
static_assert(nfsu::kNpadButtonLeft == static_cast<std::uint64_t>(HidNpadButton_Left), "Left");
static_assert(nfsu::kNpadButtonRight == static_cast<std::uint64_t>(HidNpadButton_Right), "Right");
static_assert(JOYSTICK_MAX == nfsu::kAxisMax, "stick range");

// One controller frame from player 1 and the handheld controls, which
// padInitializeDefault reads as one. Held buttons only: the controls compute
// their own presses.
nfsu::GamepadFrame sample(const PadState& pad, std::uint64_t tick) {
    nfsu::GamepadFrame input;
    input.time_us = armTicksToNs(tick) / 1000U;
    nfsu::GamepadState& state = input.pads[0];
    state.connected = padIsConnected(&pad);
    state.buttons = nfsu::gamepad_buttons_from_npad(padGetButtons(&pad));
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);
    state.axes[0] = left.x;
    state.axes[1] = left.y;
    state.axes[2] = right.x;
    state.axes[3] = right.y;
    return input;
}

const char* gear_name(int gear) {
    static const char* const kNames[] = {"R", "N", "1", "2", "3", "4", "5", "6"};
    return gear >= 0 && gear < 8 ? kNames[gear] : "?";
}

} // namespace

int main()
{
    consoleInit(nullptr);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    bool sockets = false;
    int nxlink_socket = -1;
    if (__nxlink_host.s_addr != 0 && R_SUCCEEDED(socketInitializeDefault())) {
        sockets = true;
        nxlink_socket = nxlinkStdio();
    }

    std::printf("nfsu_switch_drive: the controls and chase camera of issue #59\n");
    const nfsu::DriveSessionSetup setup = nfsu::default_drive_session_setup();
    {
        nfsu::DriveSession demo;
        std::string error;
        if (!demo.start(setup, error)) {
            std::printf("the session refuses its setup: %s\n", error.c_str());
        } else {
            const std::uint64_t frequency = armGetSystemTickFreq();
            const std::uint64_t start = armGetSystemTick();
            bool completed = true;
            for (const nfsu::GamepadFrame& frame : nfsu::drive_demo_recording()) {
                if (!demo.advance(frame, error)) {
                    std::printf("the demonstration stopped: %s\n", error.c_str());
                    completed = false;
                    break;
                }
            }
            const std::uint64_t ticks = armGetSystemTick() - start;
            if (completed) {
                const std::uint64_t bytes = demo.hashed_bytes();
                const std::string hash = nfsu::sha256_hex(demo.finish());
                std::printf("demo frames %llu bytes %llu ms %llu\nsha256 %s\n",
                            static_cast<unsigned long long>(demo.frames()),
                            static_cast<unsigned long long>(bytes),
                            static_cast<unsigned long long>(ticks * 1000U / frequency),
                            hash.c_str());
                std::printf("result: %s the pinned desktop stream\n",
                            hash == nfsu::kDrivePinnedDemoSha256 ? "EQUAL to" : "DIFFERENT from");
            }
        }
    }
    std::printf("\nDrive: left stick or D-pad steers, ZR throttle, ZL brake (hold at a stop\n"
                "to reverse), B handbrake, R/L shift, X next view, - back to the start,\n"
                "+ exits.\n");
    consoleUpdate(nullptr);

    nfsu::DriveSession session;
    std::string error;
    bool running = session.start(setup, error);
    if (!running) {
        std::printf("the session refuses its setup: %s\n", error.c_str());
    }
    // appletMainLoop returns false when the system asks the application to
    // close, so either way out of this loop reaches the shutdown below.
    while (appletMainLoop()) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
            break;
        }
        if (running) {
            if (!session.advance(sample(pad, armGetSystemTick()), error)) {
                std::printf("\nthe session stopped: %s\n", error.c_str());
                running = false;
            } else if (session.frame_state().reset) {
                running = session.start(setup, error);
            }
        }
        if (running) {
            const nfsu::VehicleTelemetry& t = session.telemetry();
            const nfsu::VehicleInput& in = session.last_input();
            const nfsu::ChaseCameraView& v = session.view();
            std::printf("\x1b[12;1Hframe %8llu  %7.1f km/h  gear %s  rpm %5.0f  \n"
                        "throttle %4.2f brake %4.2f steer %+5.2f %s  \n"
                        "position %9.2f %9.2f  camera POV %d heading %5u lag %+6d  \n",
                        static_cast<unsigned long long>(session.frames()),
                        t.forward_speed * 3.6, gear_name(t.gear), t.engine_rpm, in.throttle,
                        in.brake, in.steer, in.handbrake ? "hb" : "  ", t.position.x,
                        t.position.y, v.pov, static_cast<unsigned>(v.heading),
                        static_cast<int>(v.lag));
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
