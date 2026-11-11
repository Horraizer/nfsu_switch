# NFSU Switch

An experimental native Nintendo Switch port / engine reimplementation of
**Need for Speed: Underground (2003)**, using data from your own PC copy.
The engine and deko3d renderer are rebuilt in C++; the Windows executable
is not run on the console.

**[Current r42 prerelease](https://github.com/Horraizer/nfsu_switch/releases/tag/v0.2-r42)** |
**[LAN r7 archive](https://github.com/Horraizer/nfsu_switch/releases/tag/v0.2-lan-r7)** |
**[Install, controls and build](docs/GETTING_STARTED.md)** |
**[r42 changes and limits](docs/RELEASE_0_2_R42.md)** |
**[Historical 0.1 release](https://github.com/Horraizer/nfsu_switch/releases/tag/v0.1)**

## Bring your own game

Use your own **PC version 1.4 USA (North America)** with the original English
data. No retail cars, tracks, textures, music, movies, speech or executable
are included. Console disc data, Underground 2 and modified asset packs are
not supported inputs. See the installation guide for the required SD layout.

## Current development snapshot

- Underground event records and progression through final event113,
  including tournaments, Circuit, Sprint, Drag, Drift, Lap Knockout,
  timed challenges, difficulty, rewards and unlocks.
- Career profiles, garage purchases, body parts, rims, paint, decals,
  layered vinyls, performance packages, reputation and magazine presentation.
- Quick Race and separate Free Run practice with course/car/tuning/traffic
  options, original-data HUD, music, speech, opponents and traffic.
- Original-data vehicle assembly, rendering, reflections, headlights,
  shadows, audio, nitrous, particles and post-processing.
- Experimental two-player router LAN room: create/join, race settings,
  all playable cars, shared Stock/Street/Pro/Extreme performance tier,
  and each player's appearance garage. See [LAN_MULTIPLAYER](docs/LAN_MULTIPLAYER.md).
- Display settings include 540p/720p race rendering, motion blur, road
  reflections, and MPH/km/h. Diagnostic file logging is opt-in.
- r42 includes updated race endings, garage presentation, traffic impact
  recovery, turbo HUD, and startup traffic staging for the first drag of
  career tournament 9.

These are implemented paths, not a claim that every original behavior is
finished. Campaign startup/finish automation does not replace a full driven
campaign. Handheld play is the priority; frame-time drops and remaining
physics, AI, traffic, audio and interface differences need further work.

**r42 is a development prerelease.** This exact build passed desktop tests
with retail data and Switch packaging checks. Console validation of the r42
artifact and paired LAN handling is pending.

## Progress images

The following is an actual PC reference rendering of the r7 garage. It is
not a Switch GPU capture or a performance measurement.

![r7 PC garage: Unique vinyl warning](https://github.com/Horraizer/nfsu_switch/releases/download/v0.2-lan-r7/garage-unique-warning-pc.png)

Earlier Switch graphics milestone, before the current HUD/garage integration:

![Historical Switch capture](https://github.com/Horraizer/nfsu_switch/releases/download/v0.1/progress-switch-city.jpg)

## Build and report issues

The [getting-started guide](docs/GETTING_STARTED.md) covers Switch packaging,
PC tests and Netloader. The SDK build compiles shaders offline and embeds
them in the NRO. A source build with another SDK needs its own hardware check.

For issues, include the build tag `drag-grid-r42`, handheld/docked mode,
console model, event/course, reproduction steps and available logs or video.
Do not attach retail data, disc images, executables, saves or credentials.

Developer references include [Switch build](docs/SWITCH_BUILD.md),
[assets](docs/SWITCH_ASSETS.md), [campaign](docs/CAREER_CAMPAIGN_FULL.md),
[garage audit](docs/GARAGE_MENU_ORIGINAL.md), [desktop tools](docs/DESKTOP_RACE.md)
and [renderer](docs/RENDERER.md). Older dated research notes and work-state
snapshots describe their own milestones, not this release's acceptance.

## Project and third-party notices

This is an unofficial fan project, not affiliated with or endorsed by EA or
Nintendo. Need for Speed and its content belong to their respective rights
holders. Source availability does not grant rights to retail assets.

The movie decoder includes adaptations from FFmpeg n7.1 under LGPL2.1;
see [attribution](docs/INTRO_MOVIES.md) and the
[license text](third_party/ffmpeg-LGPL-2.1.txt). Other project source has no
repository-wide license grant yet; do not assume an MIT or GPL license.

[AGENTS.md](AGENTS.md) and [Work recovery](docs/WORK_RECOVERY.md) document the
project workflow. Source publication does not retroactively mark open
research, independent review or console acceptance criteria complete.

## Run

Developer diagnostic examples use your own data and separate build outputs.
They do not replace the Switch application or prove console performance.
Replace the example retail paths with your installation. The complete option
explanations remain in [CLI_REFERENCE](docs/CLI_REFERENCE.md).

```sh
build/nfsu_probe summary X:/Games/NFSU
build/nfsu_probe asset-check X:/Games/NFSU
build/nfsu_probe asset-check F:/nfsu --set stock-car
build/nfsu_probe chunks X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN 64
build/nfsu_probe meshes X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN
build/nfsu_probe obj X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN build/240sx.obj
build/nfsu_probe textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe pack-set X:/Games/NFSU/GLOBAL/GLOBALB.BUN
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/tex tga
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/dds dds
build/nfsu_probe texture-bindings X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe car-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe stock-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN
build/nfsu_probe track-region X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN
build/nfsu_probe stock-scene X:/Games/NFSU/Global/GLOBALB.BUN 240SX A X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN --place-wheels
build/nfsu_probe track-objects X:/Games/NFSU/Tracks/STREAML1RB.BUN 23011344 4168 23015540 178268
build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RB.BUN 23011344 4168 450 20 -80 900 20 -80 --far 60
build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc
build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc 0 -1200
build/nfsu_probe track-scene X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group
build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group --out build/shots/region.tga
build/nfsu_probe track-budget X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group --repeat 9
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --eye 450 20 -80 --look-at 900 20 -80 --view-far 60 --limit 2
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --mesh TRSABC_ROAD_CHOP14 --screen-scale 120
build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc
build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc 783 -8.5
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76 56.5 944 -74 56.5
build/nfsu_probe car-placement X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe vehicle-sim mixed --handling X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe vehicle-tuning X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe drive-replay --demo --root X:/Games/NFSU --render-every 120 --out build/shots/drive
build/nfsu_probe race-events X:/Games/NFSU/Global/GLOBALB.BUN X:/Games/NFSU/FrontEnd/FrontB.lzc
build/nfsu_probe track-whole X:/Games/NFSU/Tracks/STREAML1RA.BUN X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN --texture X:/Games/NFSU/Global/INGAMEB.BUN --texture X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe race-route X:/Games/NFSU/Global/GLOBALB.BUN X:/Games/NFSU/FrontEnd/FrontB.lzc X:/Games/NFSU/Tracks/TRACKB1003.lzc 1 --drive forward
build/nfsu_probe race-timing X:/Games/NFSU/Global/GLOBALB.BUN X:/Games/NFSU/FrontEnd/FrontB.lzc X:/Games/NFSU/Tracks/TRACKB1003.lzc 1 --drive forward
build/nfsu_probe vehicle-track X:/Games/NFSU/Global/GLOBALB.BUN X:/Games/NFSU/FrontEnd/FrontB.lzc X:/Games/NFSU/Tracks/TRACKB1003.lzc 1 wall
build/nfsu_probe vehicle-track --proving-ground leave
build/nfsu_probe track-stream-path X:/Games/NFSU/Tracks/STREAML1RA.BUN X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --hand-path 1003 --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN --texture X:/Games/NFSU/Global/INGAMEB.BUN --texture X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe part-markers X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN exhaust
build/nfsu_probe body-skin-inputs 240SX --source car-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN --source car-vinyls X:/Games/NFSU/Cars/240SX/VINYLS.BIN --source other X:/Games/NFSU/Cars/TEXTURES.BIN --target-texture DUMMY_SKIN1 --vinyl 0 TRIBAL5 16 no-remap --doorline plain --base 200 30 30 128 --assemble
```
