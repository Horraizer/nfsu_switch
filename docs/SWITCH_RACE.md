# Event 1 on Switch (#196)

`nfsu_switch_race.nro` runs the stock 240SX through event 1's forward
track-1003 circuit for two laps. It uses `RaceSession` from #192, the #59
controls and chase camera, #157 residency, and #198's deko3d track path with
#171 native DXT. This branch started at PR #198 head `1429830fd566`, while
that PR was in review. It must follow acceptance of #198.

## Install and use

Copy the supplied NRO to `sdmc:/switch/nfsu_switch_race.nro` and launch in
full-memory Homebrew Menu mode. Keep the existing 12 first-lap files under
`sdmc:/nfsu`. No additional retail file is required: the track-info table is
read from the checked `Global/GLOBALB.BUN`, not the modified desktop
`GLOBALB.LZC`. The #47 manifest check runs before any game parser and names
missing, incomplete, wrong-version or unreadable files on screen.

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
| Prepared car, including two instance arrays | 1781098 | persistent below |
| Collision course | 1433448 | persistent below |
| Car + collision | 3214546 | 67108864 |
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
loading scratch use the ordinary allocator. The panel separately reports
`mallinfo().uordblks` (allocated malloc space) and `.arena` (allocator
extent) before loading, after preparing the track/car/course and initial
commands, and on every displayed frame. The after-load sample precedes GPU
upload; the current sample includes renderer/readback allocations. Compare
these samples to the baseline to measure retained scene cost. Arena extent
is not peak live memory: transient loader live-allocation peaks remain
unmeasured. Actual deko3d GPU block bytes are reported too. They may overlap;
do not add them. In particular, #198 photo 4378's `svcGetInfo` process-used
number reported kernel process memory usage, not allocator live blocks.
Those values alone do not establish scene occupancy or free heap. Current malloc
figures are not a measured loader high-water mark.

## Performance and owner evidence

Every frame shows simulation, residency/hash/logging, cull/record, uniform
update, GPU record/submit/wait, transfer/copy, previous HUD/present and the
previous total frame against 16.67 ms. Initial upload and load time are
reported separately. Transfer/copy includes CPU copy/guard work around the
GPU readback; the nested GPU figure is just the timed GPU copy/wait. The
track and car are drawn by deko3d; readback lets the libnx framebuffer show
the measurement panel. It adds diagnostic overhead and is not a claim that
the future direct-present race renderer meets 60 FPS.

PR #198 comment 4378 established 26.66 ms total and 9.74 ms GPU for one
track-only view on the owner's console. It is not a #196 performance result.
The #196 live-pad and replay hardware runs remain to be done by the owner:

1. Photograph the build tag and countdown, then drive a lap with the pad.
   Capture movement, visible car/track and current/last lap time. Record the
   video or photos on #159, as #196 requests.
2. Restart into X replay. Let it reach the two-lap finish (13:26.90 simulated
   race time), then photograph `EQUAL`, both hashes and lap/result times.
   The entire step stream also prints to nxlink when connected.
3. Photograph per-stage timing, OVER/WITHIN, debt, draw counts, GPU blocks
   and malloc figures while moving. Report any error or `DIFFERENT` result.

No Switch race frame time or cross-architecture replay pass is claimed until
that evidence exists. Delivery here is the SD-card NRO package requested by
the owner; no pull request is merged by the worker.
