# R99-R72 for Nintendo Switch

Download `nfsu-switch-v0.2-r99-r72-sd.zip` and extract it to the root of the SD card. To update an existing installation, replace `sdmc:/switch/nfsu-switch/NFSU_switch.nro` with the standalone `NFSU_switch.nro`. An existing forwarder that uses this path will launch the update. The game reads the original files from your own PC 1.4 USA installation under `sdmc:/nfsu/`. Open Homebrew Menu in full-memory application mode.

## What you can try

- Race a second player on one Switch in Circuit, Sprint, Drag or Drift. Both players can pick a car, adjust its appearance in the room with Y and choose stick or tilt steering. Choose a top/bottom or left/right layout. Civilian traffic is configurable in Circuit, Sprint and Drag.
- In Options, choose 540p or 720p for handheld races, or 1080p while docked. Wet-road reflections and particles have separate controls in split-screen.
- Drive through the water channel on National Rail. This revision updates wheel spray, the animated water surface and environmental particles. The particle checks compare the port against both the PC and Xbox originals.
- The source includes changes to opponent throttle, braking, passing, traffic and collisions. Event 19 still shows a trajectory difference after contact with a roadside sign. Work on opponent behavior continues.

## Verification and known limits

This is a prerelease. The development build passed its Switch packaging checks and focused retail effects tests. The full desktop suites still have known failures. This exact executable awaits a playthrough on physical Switch hardware. For a bug report, include R99-R72, the event or course, and handheld or docked mode. The diagnostic build label inside the NRO is `artwork-r97`.

The two binary downloads can be verified with `SHA256SUMS.txt`. The tagged source and the installation guide are available alongside this release.
