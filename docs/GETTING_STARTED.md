# Getting started with v0.2-r42

This is an experimental prerelease. Download it from the
[v0.2-r42 release page](https://github.com/Horraizer/nfsu_switch/releases/tag/v0.2-r42).

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

1. Open Homebrew Menu in **full-memory application mode** (title takeover).
   Album/applet mode does not provide the memory this build needs.
2. Download `nfsu-switch-v0.2-r42-sd.zip` and extract its contents at the
   top level of the SD card. Alternatively, put the standalone NRO under
   `sdmc:/switch/nfsu-switch/`. The NRO filename is not the game-data folder.
3. From your own PC1.4 USA installation, copy these complete directories to
   `sdmc:/nfsu/`, keeping subdirectories: `Cars`, `FrontEnd`, `Global`,
   `Languages`, `Movies`, `Sound`, `SNDSTREAMS`, `SpeechStreams`, and `Tracks`.
4. Check the layout, especially `Languages/English.bin`:

   ```text
   SD card/
     switch/
       nfsu-switch/
         NFSU_switch.nro
     nfsu/
       Cars/
       FrontEnd/FrontB.lzc
       Global/GLOBALB.BUN
       Languages/English.bin
       Movies/
       Sound/
       SNDSTREAMS/
       SpeechStreams/
       Tracks/
   ```

   Avoid accidental nesting such as `nfsu/nfsu/Cars` or putting game data
   under `switch/nfsu-switch/nfsu`.
5. Launch `NFSU_switch.nro` from Homebrew Menu. Select Go Underground, Quick Race or Multiplayer.
   Free Run is practice inside the standalone race setup. Keep existing saves;
   the release archive contains no profile files and does not reset progress.

The NRO contains the port, its shaders and diagnostic recordings. It contains
no retail game data. `speed.exe` is not needed on the SD card. Full campaign
coverage uses more files than the historical first-lap minimum asset list.

An optional ASCII `sdmc:/config/nfsu_switch/assets.cfg` can move the data root:

```text
root = sdmc:/games/nfsu
```

Use an ASCII path, without a UTF-8 BOM. The default needs no config file.
Profiles are stored under `sdmc:/switch/nfsu-saves/`; keep a backup before
trying experimental builds. Do not share personal saves in public reports.

## Driving controls

| Control | Action |
| --- | --- |
| Left stick / D-pad | Steering; lane change in Drag |
| ZR | Accelerate |
| ZL | Brake / reverse |
| B | Handbrake |
| A (hold) | Nitrous, when fitted |
| X | Change camera |
| R3 (hold) | Look back; release to restore the normal view |
| Right stick up / down | Drag gear up / down |
| R / L | Alternative manual gear up / down |
| Plus | Race pause; LAN room/race leave follows the displayed prompt |
| Minus | Car reset where available |
| L3 + R3 | Toggle diagnostic FPS/frame-time overlay |
| Menu A / B | Accept / back |

Turbo is an automatic performance upgrade, not a separate boost button.
The chosen car/tier determines installed nitrous and performance parts.
Use the original-style screens for purchases, rewards and profile selection.

## Race display settings

In Options, cycle Race Resolution through Auto, 540p and 720p. The selection
persists across restarts. For a 60 FPS target in handheld mode, start with
540p at stock clocks. The project owner recommends 720p with a GPU overclock
for the same target. Frame rate can vary by race and scene. Motion blur, road
reflections and MPH/km/h are also configurable. Enable diagnostic logging
under Options when collecting an SD log.

## Two-player LAN

Both consoles need this build and their own local PC data copy, on the same
router network. Open Multiplayer. A creates a room; X joins by the host's
IPv4 address. The host sets course, direction, laps and the shared performance
tier. Each driver selects a car; Y opens the appearance garage. B at the garage
root returns to the room. The guest confirms readiness with A; the host starts
when ready. See [LAN_MULTIPLAYER](LAN_MULTIPLAYER.md) for scope and limitations.

This is custom router LAN, not Nintendo online play or direct local wireless.
No room discovery, traffic or AI grid is provided by this prototype. Guest
movement is host-authoritative; timing, latency and handling still need paired
hardware comparison. Do not mix old and new protocol versions.

## Known limits

- The r42 artifact has desktop and packaging validation. Its console visual,
  input and paired LAN validation is pending.
- Full campaign records and automated start/finish checks do not establish
  a flawless 8-10-hour playthrough, original AI/physics/audio parity or locked 60 FPS.
- Remaining drift HUD, collision/traffic/audio and some frontend behavior
  continue to be matched against original data/code.
- Missing `English.bin`, an incomplete data tree, a wrong regional asset set
  or applet-mode memory can prevent startup/loading. Check the displayed error.
- For a crash, preserve any SD/Netloader log and report `drag-grid-r42`.

## Build from source

```sh
git clone https://github.com/Horraizer/nfsu_switch.git
cd nfsu_switch
git checkout v0.2-r42
```

Install devkitPro switch-dev: devkitA64, libnx, deko3d, uam, switch-tools,
switch-cmake, devkita64-cmake and switch-pkg-config. Use CMake3.21+, Ninja and
standalone Python3.12 (the exercised version) in a configured devkitPro/MSYS2 or Linux shell.

```sh
export DEVKITPRO=/opt/devkitpro
cmake -S . -B build-switch -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DNFSU_RACE_BUILD_TAG=drag-grid-r42 \
  -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF \
  -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF
cmake --build build-switch --target nfsu_switch_race_nro
ctest --test-dir build-switch --output-on-failure \
  -R '^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders)$'
```

Output: `build-switch/switch/nfsu_switch_race.nro`. Offline shader compilation
and RomFS embedding are part of the target. The downloaded NRO uses the project icon and standard runtime metadata.
A different SDK can produce a different binary.

For desktop tools and all retail checks, first install the exercised Python
packages into the standalone interpreter CMake will use:

```sh
python -m pip install -r requirements-dev.txt
```


```sh
cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=/path/to/your/pc/game
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNFSU_RETAIL_ROOT=/path/to/your/pc/game
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Use an ASCII retail path; quote it when it contains spaces. The standalone
Python configure finds must provide the packages in requirements-dev.txt.
The optional original-PC live recorders additionally require frida17.5.1;
it is not needed for ordinary NRO play. A configure without
NFSU_RETAIL_ROOT skips retail validation and is not a complete gate.
Desktop tools do not substitute for Switch GPU/runtime acceptance.

## Netloader

With the target console's Netloader open:

```sh
nxlink -a SWITCH_IP -s NFSU_switch.nro
```

Only the NRO is transferred; keep the game data on SD. For two consoles, both
Netloaders must be ready. Do not start two PC log servers on the same port.
