# Getting started with 0.1

## Required game edition

Use your own **Need for Speed: Underground (2003), PC version 1.4 USA (North America)**
with the original English frontend files. This is NFS Underground 1, not
Underground 2, the Xbox release, or a remastered texture/mod pack.

The project owner confirms that the tested installation is **1.4 USA**.
The file identities below identify the actual inputs used during development.
Other regional editions, patch levels and modified data sets have not been
validated for this release.

| File | SHA-256 of the tested input |
| --- | --- |
| `speed.exe` (research reference; not run by the port) | `c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7` |
| `Global/GLOBALB.BUN` | `39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9` |
| `FrontEnd/FrontB.lzc` | `76f368d5e5e30f96a8182bf457dba4491395e075c3f336d86a32012b56e16f90` |

The runtime reads `GLOBALB.BUN`; a different `GlobalB.lzc` is not a substitute.
Additional file identities and the original data layout are documented in
[SWITCH_ASSETS.md](SWITCH_ASSETS.md). That document's old minimal stock-car file
list is insufficient for the current menu, tuned Skyline, movies and audio.

## Install on Switch

1. Have a Switch capable of running homebrew and Homebrew Menu in **full-memory
   application mode** (title takeover). Do not use Album/applet mode for this build.
2. Download `nfsu-switch-0.1.nro` from the 0.1 release. Put it at
   `sdmc:/switch/nfsu-switch/nfsu-switch-0.1.nro`.
3. Open your PC game's installed folder (the folder containing `speed.exe`).
   Copy these complete directories into `sdmc:/nfsu/`, retaining subdirectories:
   `Cars`, `FrontEnd`, `Global`, `Languages`, `Movies`, `Sound`, `SNDSTREAMS`,
   `SpeechStreams`, and `Tracks`. This deliberately favors a complete data copy
   over an incomplete minimal subset. Do not copy mod loaders or replacement data.
4. The resulting layout should look like this:

   ```text
   sdmc:/switch/nfsu-switch/nfsu-switch-0.1.nro
   sdmc:/nfsu/Cars/SKYLINE/GEOMETRY.BIN
   sdmc:/nfsu/FrontEnd/FrontB.lzc
   sdmc:/nfsu/Global/GLOBALB.BUN
   sdmc:/nfsu/Languages/English.bin
   sdmc:/nfsu/Movies/na_boot.mad
   sdmc:/nfsu/Sound/...
   sdmc:/nfsu/SNDSTREAMS/...
   sdmc:/nfsu/SpeechStreams/...
   sdmc:/nfsu/Tracks/...
   ```
5. Launch the NRO. A/B skips the opening movies. Choose **Free Driving** in
   the menu and press A. The supplied build loads Olympic Square and the
   tuned Skyline. Underground displays an unavailable notice in 0.1.

No `speed.exe`, Windows installation, emulator or external video player is
needed on Switch. The NRO contains the port and its shaders, not retail assets.
The port reads your copied data; it does not patch the PC installation.

An optional ASCII file at `sdmc:/config/nfsu_switch/assets.cfg` can relocate data:

```text
root = sdmc:/games/nfsu
```

Use an ASCII path and save without a UTF-8 BOM. The default needs no config file.

## Controls in the supplied free-driving build

| Control | Action |
| --- | --- |
| Left stick / D-pad | Steer |
| ZR | Accelerate |
| ZL | Brake; hold at rest for reverse assist |
| B | Handbrake |
| L (hold) | Nitrous while driving; consumes the bottle |
| X | Change camera |
| R3 (hold right stick button) | Look back / view from the front; release to restore camera |
| Minus | Reset the car |
| Y | Pause / resume |
| Plus | Leave free driving and return to the menu |
| Menu Up/Down or left stick | Select menu entry |
| Menu A / B | Confirm / back |

The tuned free-drive car uses turbo level 1 and nitrous level 3. Nitrous is
reserved on L, rather than the generic diagnostic control map's downshift.
Turbo adds engine performance automatically; it is not a second boost button.
There is no invented continuous nitrous refill: re-entering a new session
initializes the bottle; a car reset alone does not refill it.

## Known limits and troubleshooting

- Full Underground progression, traffic and finished opponent AI are absent.
- There is no normal track-selection screen. The menu's free drive is Olympic
  Square. Atlantic can be launched through the developer `--live --event 8`
  path, which is not the same menu/free-drive experience.
- Physics, steering, tachometer, sound and rendering are still being matched
  to the original. Visual effects are implemented in bounded forms, not a
  claim of complete original rendering parity.
- Targeting smooth 60 FPS does not mean 60 FPS everywhere. The FPS overlay
  is disabled in the release. Do not compare PC reference renderer timing
  to Switch GPU performance.
- If preparation fails, first verify full-memory mode, the exact SD layout,
  a complete data copy and the original file identities. Photograph the error.
- If input seems latched after entry, release held controls and try again.
- If a visual artifact appears, report the location and whether re-entry
  clears it. Include the `boost-r6` build identity in the report.

## Build from source

The downloadable NRO is the actual hardware-accepted binary. A rebuild with a
different SDK can differ byte-for-byte and needs its own hardware check.
The `v0.1` tag adds documentation only to runtime source commit
`24665d0a03bbae52e5f16c5f1a1d38b62676a23a`.

```sh
git clone https://github.com/Horraizer/nfsu_switch.git
cd nfsu_switch
git checkout v0.1
```

### Switch NRO

Install devkitPro's `switch-dev` tools: devkitA64, libnx, deko3d, uam,
switch-tools, switch-cmake, devkita64-cmake and switch-pkg-config. Also install
CMake 3.21+, Ninja and host Python 3.8+. Use a devkitPro/MSYS2 shell on Windows
or a correctly configured devkitPro environment on Linux.

```sh
export DEVKITPRO=/opt/devkitpro
cmake -S . -B build-switch -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DNFSU_RACE_BUILD_TAG=boost-r6 \
  -DNFSU_RACE_FXAA=ON \
  -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF \
  -DNFSU_RACE_FPS_OVERLAY=OFF \
  -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF
cmake --build build-switch --target nfsu_switch_race_nro
ctest --test-dir build-switch --output-on-failure \
  -R '^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders|nfsu_switch_frontend_profile_contract)$'
```

Output: `build-switch/switch/nfsu_switch_race.nro`. Shader compilation and
embedding are part of this target. The host checks above validate packaging;
they do not run a Switch emulator or establish hardware performance.
For the complete SDK test suite, build all targets and run plain CTest.
See [SWITCH_BUILD.md](SWITCH_BUILD.md) for SDK discovery and diagnostics.

### Desktop tools and tests

Use a C++17 compiler, CMake, Ninja and Python. GCC is the exercised native
build path. Desktop diagnostic tools do not substitute for the Switch
application; platform-specific window/audio backends have separate limits.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNFSU_RETAIL_ROOT=/path/to/your/pc/game
cmake --build build
ctest --test-dir build --output-on-failure
```

Use an ASCII path for the retail root and quote it if it contains spaces.
A configure without `NFSU_RETAIL_ROOT` skips retail validation and is not a
complete game-data test. The README's developer reference lists the tools;
most are research/diagnostic executables, not a finished desktop game launcher.

## Sending with Netloader

With the Switch's Homebrew Menu Netloader open on your local network:

```sh
nxlink -a SWITCH_IP -s nfsu-switch-0.1.nro
```

Keep the data on SD; Netloader sends the NRO only. Capture the terminal output
when reporting crashes. Never post copyrighted retail files with a bug report.
