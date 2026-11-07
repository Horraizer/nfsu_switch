// The minimal Nintendo Switch application: proof that the devkitA64
// toolchain, libnx and the NRO packaging work, and nothing more. It
// initialises the console and the first controller, runs a frame loop until
// + is pressed or the system asks the application to close, and shuts down.
// It renders nothing and loads no shader. docs/SWITCH_BUILD.md.

#include <switch.h>

#include <cstdio>

int main()
{
    consoleInit(nullptr);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::printf("nfsu_switch: devkitA64 and libnx are working.\n");
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

    consoleExit(nullptr);
    return 0;
}
