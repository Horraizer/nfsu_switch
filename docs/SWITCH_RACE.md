# Event 1 on Switch (#196)

`nfsu_switch_race.nro` runs the stock 240SX through event 1's forward
track-1003 circuit for two laps. It uses `RaceSession` from #192, the #59
controls and chase camera, #157 residency, and #198's deko3d track path with
#171 native DXT. This branch started at PR #198 head `1429830fd566`, while
that PR was in review. It now includes main at `17e91ad`, including #198
round 2 loader polling and the shared `HeapUsage` observation helper.

## Install and use

Copy the supplied NRO to `sdmc:/switch/nfsu_switch_race.nro` and launch in
full-memory Homebrew Menu mode. Keep the existing 12 first-lap files under
`sdmc:/nfsu`. No additional retail file is required: the track-info table is
read from the checked `Global/GLOBALB.BUN`, not the modified desktop
`GLOBALB.LZC`. The #47 manifest check runs before any game parser and names
missing, incomplete, wrong-version or unreadable files on screen.

Configure `-DNFSU_RACE_BUILD_TAG=<head-or-build-id>` for a supplied label.
Without that option, a non-local `NFSU_TRACK_BUILD_TAG` is inherited so the
track build command also labels the race NRO; otherwise the current Git
HEAD (12 characters) is used. Reconfigure after changing HEAD. Source
exports without Git need an explicit label. The panel clips labels to 12
characters; CTest checks the configured tag is present in the ELF.

The menu offers A for live driving and X for replay. With nxlink, optional
arguments are `[sdmc:/nfsu] [--live|--replay]`. RomFS holds the three checked
offline shaders and the repository's authored `tests/recordings/event1.txt`.
It contains no retail or derived game asset. The application writes no SD
file; every retail input is opened read-only.

Live controls pass the libnx state through `gamepad_buttons_from_npad` into
`RaceSession`'s existing `DriveControls`:

| Control | Action |
| --- | --- |
| Left stick / D-pad | Steer, with #59 calibration/filter |
| ZR | Throttle |
| ZL | Brake; hold at rest for #59 reverse assist |
| B | Handbrake |
| R / L | Shift up / down |
| X | Next shared chase-camera POV |
| Minus | #60 vehicle reset |
| Y | Pause or resume, also in replay; useful for photographs |
| Plus | Exit |

Release controls during the countdown to clear the shared hot-plug latch.
Disconnected pads contribute neutral input. A pause or OS suspension longer
than one second resets wall-clock debt, not simulation state. Ordinary slow
frames consume at most eight 60 Hz ticks per display and retain the debt;
no replay input or simulation step is skipped. Each tick contains the same
two 120 Hz vehicle/contact steps as desktop. Rendering never changes dt.
A long sustained overload can therefore lag wall time; the panel shows the
debt instead of claiming 60 Hz from the display rate.

The countdown, lap number, current/last lap, finish time and result come
from `race_session_hud`. The fourth desktop keyboard-help line is replaced
with Switch controls. Text lines are individually clipped to 105 columns,
so a long measurement cannot spill into the following row.

## Replay parity

Replay reads the exact bundled LF recording, verifies its SHA-256, and
feeds every run through the shared input mapping, simulation and residency.
It prints each `tick SHA256` row to stdout/nxlink. The digest input matches
`nfsu_race`: encoded session bytes, that tick's residency line including LF,
and the resident record indices in ascending little-endian order. Startup
residency lines precede the binary step digests in the sequence hash.

The screen shows the last step, the sequence hash, and a SHA-256 over all
48336 textual step rows. `EQUAL` is displayed only after all input has been
consumed, both hashes match, and the exact finish, lap times and zero-reset
result match. A partial run says `RUNNING`; a mismatch says `DIFFERENT`.

```text
event 1 track 1003 laps 2 outcome finished steps 48336 resets 0
lap 1 ticks 1616400 time 6:44.10
lap 2 ticks 1611200 time 6:42.80
total ticks 3227600 time 13:26.90
state-sha256 79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9
steps-sha256 E4D4D6E955D59F7FF11C3318BC455D08B7903CA46BBDE8E1FC3F2AC32AEA54E5
stream loads 78 releases 0 bytes-read 36471524
```

The SDK-free executable tests the very same console orchestration:

```text
nfsu_race_run_tests tests/recordings/event1.txt
nfsu_race_run_tests tests/recordings/event1.txt X:/Games/NFSU
nfsu_race_run_tests tests/recordings/event1.txt X:/Games/NFSU --sample-draws
```

The first command is in CTest and needs no game data. It checks clock debt,
recording identity and extent, invalid call order, resource/texture rebasing,
capacity refusal and mismatched cameras. The retail run checks every step
hash and the result, uploads/retires the combined table through the neutral
ledger, and validates thirteen selected race views. `--sample-draws` also
checks one view per second; `--full-draws` checks every tick's draw commands.
The Release retail check sampled 814 views across both laps, with at most
1824 combined draws; all contained the car and visible track. No desktop
trace is described as console execution evidence.

## Drawing, residency and memory

The loader and native textures are #198's `TrackSlice`. The track and car
commands are joined with rebased upload indices and drawn in one deko3d
pass with one camera and depth buffer. Car placement follows the shared
vehicle transform; the chase camera includes the existing track collision
hook. The car uses the same painted stock scene as the desktop race.
Track culling uses the same camera basis as GPU projection.

The complete list must fit 2047 draws. Far distance starts at 250 and can
fall back to 125, 62.5 or 31.25; no list is silently truncated. The panel
shows the selected far distance. The combined table has 5098 resources,
including 4126 vertex/index allocations. `DrawExecutionPlan` counts those
buffer slots and the backend uses that count, instead of the former 4096
placeholder. Bytes remain bounded by the aggregate 256 MiB GPU budget.

Residency advances from the actual car position and velocity each tick.
The recovered permanent set of this event stays loaded for both laps: 78
loads at startup, zero releases, 36471524 bytes read in selected ranges.
The app verifies that each residency step still names exactly its uploaded
set. An unsupported request outside that set stops with a diagnostic;
it never renders stale geometry while pretending a new section was loaded.

| Planned payload | Bytes | Limit |
| --- | ---: | ---: |
| CPU track retained, including native texture blocks | 27770710 | 50331648 |
| Track texture-preparation overlap (#171/#198) | 41867030 | 50331648 |
| Prepared car, including two instance arrays | 1782058 | persistent below |
| Collision course | 1433448 | persistent below |
| Car + collision | 3215506 | 67108864 |
| Reserved draw uniforms | 1048320 | 1048576 |
| Conservative combined GPU bound | 138622566 | 268435456 |

The GPU bound includes bounded image tiling, tight and padded staging, and
64 MiB for the executor, targets, descriptors, uniforms and readback. The
backend checks actual SDK image layouts and every GPU block allocation.
Staging lives through the upload fence; resources live through their last
use. The ledger test uploads all 5098 resources and retires them without a
leaked allocation. Track diagnostic collision/path copies are discarded
before loading the race course; parser and raw car inputs die after scene
preparation. The CPU native block copies follow #171's allowed retention
rule and remain until the scene closes.

These are payload plans, not a claim that all C++ allocations use offset
arenas. Scenery metadata, strings, capacity slack, command metadata and
loading scratch use the ordinary allocator. The merged loader services
HOME/exit and samples `mallinfo` at its parsing checkpoints; the race app
uses shared `HeapUsage` to keep load and run observations separate. The
load observation includes the asset check, track/car/course preparation
and initial commands. The after-load sample is taken before the renderer
is created, not just before its first frame.

deko3d allocates its GPU blocks from the same newlib heap. Therefore
MALLOC IN-USE NOW includes GPU BLOCKS. Do not add the two readings.
The renderer, GPU blocks and readback are created after the after-load
sample. In live photo 4401, 155528752 - 58887808 = 96640944 bytes of growth;
86659072 of that is GPU blocks, leaving 9981872 bytes for other allocations
such as readback, command tables and race state. The finish photo 4470 has
155614608 in-use, only 85856 bytes more than that live sample, but two
samples in different modes do not prove absence of a leak.

HEAP LOAD/RUN OBS HWM are the largest sampled in-use values, lower bounds
on the true live peak. Arena now/OBS HWM measure allocator extent, not live
bytes. LOAD HEAP OVER compares the sampled load delta with 48 MiB. The
race load delta also includes car/course preparation. The track loader's
exact desktop allocation peak is already known to exceed 48 MiB: 63602625
bytes (review 4467), 13270977 over. #204 follows #55 and will reduce that
peak; this change does not enlarge the budget or claim the loader fits.

## Performance and owner evidence

Every frame shows simulation, residency/hash/logging, cull/record, uniform
update, GPU record/submit/wait, transfer/copy, previous HUD/present and the
previous total frame against 16.67 ms. Initial upload and load time are
reported separately. Transfer/copy includes CPU copy/guard work around the
GPU readback; the nested GPU figure is just the timed GPU copy/wait. The
track and car are drawn by deko3d; readback lets the libnx framebuffer show
the measurement panel. It adds diagnostic overhead and is not a claim that
the future direct-present race renderer meets 60 FPS.

On 2026-09-27 the owner ran the coordinator's NRO built from `e65960b`.
Its SHA-256 is `45d21ebf744acd2a7dc52926f55ee0658822f6ce1668a7a67035bb56d33f51b3`.
It showed BUILD LOCAL; the staging record ties those photos to the head.
The label propagation above fixes that for subsequent builds.

- [PR #199 comment 4401](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4401)
  records live pad driving with the painted car and track visible and the
  lap timer running. At step 4002: full frame 97.20 ms, SIM 8.46 ms for six
  catch-up ticks, RES/HASH 0.82, CULL/RECORD 52.14, UPLOAD 4.48, DRAW 11.19,
  TRANSFER/COPY 8.04 (GPU 1.11), previous HUD/PRESENT 11.73. Debt was zero;
  draws were 1364 track + 94 car; GPU blocks 86659072 bytes. This is about
  10 FPS, well over the 16.67 ms budget. #55 addresses this measured cost.
- [PR #199 comment 4470](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4470)
  records the owner's completed two-lap replay: EQUAL, step 48336, total
  13:26.90 and both hashes exactly matching the desktop pins above. The
  panel directly shows the final lap 6:42.80; lap 1 6:44.10 is implied by
  the total and the app's EQUAL check, not separately read from the photo.
  Its 31.93 ms frame is after finish, when simulation and culling stopped;
  it is not a driving performance measurement. GPU blocks remain 86659072.

These establish live control and cross-architecture replay parity at
`e65960b`; they are not measurements of the merged round-2 NRO. A completed
live lap has not been photographed. Preserve that distinction when adding
new evidence to #159. The new loader/heap panel still needs its own console
photo. The next performance work is #55, then the loader peak reduction
#204; no pull request is merged by the worker.

## Performance follow-up (#55)

The race recorder now caches immutable track command templates. The
original path remains available to differential tests; visibility and draw
order are unchanged. The panel reports bounded command-cache metadata in
persistent accounting. See [SWITCH_PERFORMANCE.md](SWITCH_PERFORMANCE.md)
for the retained baseline, regression thresholds and remaining console
measurements. Prior console photos above still describe e65960b.
