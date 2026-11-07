# NFSU Switch

An experimental native Nintendo Switch port / engine reimplementation of
**Need for Speed: Underground (2003)**, using data from your own PC copy.
The engine and deko3d renderer are rebuilt in C++; the Windows executable
is not run on the console.

**[LAN r7 prerelease](https://github.com/Horraizer/nfsu_switch/releases/tag/v0.2-lan-r7)** |
**[Install, controls and build](docs/GETTING_STARTED.md)** |
**[r7 changes and limits](docs/RELEASE_0_2_LAN_R7.md)** |
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
- r7 restores original vinyl replacement dialogs, Unique/hood restrictions,
  and keeps the correct layer and color cursor when editing or cancelling.

These are implemented paths, not a claim that every original behavior is
finished. Campaign startup/finish automation does not replace a full driven
campaign. Handheld play is the priority; frame-time drops and remaining
physics, AI, traffic, audio and interface differences need further work.

**LAN r7 is a prerelease.** Recent garage changes passed PC checks and Switch
packaging checks; this exact artifact still needs console garage acceptance
and paired LAN timing/handling checks. It is not an independently reviewed
or hardware-accepted stable release. The existing 0.1 release and the owner's
local 0.2 build are unchanged.

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

For issues, include the build tag `garage-flow-r7`, handheld/docked mode,
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
