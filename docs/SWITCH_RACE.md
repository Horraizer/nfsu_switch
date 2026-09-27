# Event 1 on Switch (#196)

`nfsu_switch_race.nro` runs the stock 240SX through event 1's forward
track-1003 circuit for two laps. It uses `RaceSession` from #192, the #59
controls and chase camera, #157 residency, and #198's deko3d track path with
#171 native DXT. The race also uses the command cache merged in #206 and
the bounded track loader merged in #207.

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
exports without Git need an explicit label. The menu and nxlink performance
lines clip labels to 12 characters; CTest checks the configured tag in the ELF.

The menu offers A for live driving, X for replay and Y to toggle the
diagnostic panel. Direct presentation is the default. With nxlink, optional
arguments are `[sdmc:/nfsu] [--live|--replay] [--panel]`. RomFS holds the three checked
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
| Y | Pause or resume, also in replay |
| Plus | Show the result screen; press again there to exit |

Release controls during the countdown to clear the shared hot-plug latch.
Disconnected pads contribute neutral input. A pause or OS suspension longer
than one second resets wall-clock debt, not simulation state. Ordinary slow
frames consume at most eight 60 Hz ticks per display and retain the debt;
no replay input or simulation step is skipped. Each tick contains the same
two 120 Hz vehicle/contact steps as desktop. Rendering never changes dt.
A long sustained overload can therefore lag wall time; the nxlink performance
line shows the debt instead of claiming 60 Hz from the display rate.

The menu and loading progress use the libnx framebuffer. Direct mode closes
it before creating the deko3d swapchain. It shows no HUD during the race;
the countdown, lap, speed, gear and pause state go only to nxlink until the
GPU HUD in #64 lands. Use `--panel` or menu Y for live-driving evidence now:
that mode keeps the framebuffer and the original readback panel with the
race HUD and heap rows. After the race finishes or Plus is pressed, the
renderer closes and a framebuffer result screen remains until Plus is
pressed again. It contains run statistics that can be photographed without
nxlink stdout. Loading, result and error text is clipped to 105 columns.

## Replay parity

Replay reads the exact bundled LF recording, verifies its SHA-256, and
feeds every run through the shared input mapping, simulation and residency.
It prints each `tick SHA256` row to stdout/nxlink. The digest input matches
`nfsu_race`: encoded session bytes, that tick's residency line including LF,
and the resident record indices in ascending little-endian order. Startup
residency lines precede the binary step digests in the sequence hash.

Stdout/nxlink prints the result, the sequence hash and a SHA-256 over all
textual step rows. The result screen shows the mode, outcome, both lap
times, total, both hashes, last step and `REPLAY EQUAL`, `RUNNING` or
`DIFFERENT`. `REPLAY EQUAL` appears only after all input has been consumed,
both hashes match, and the exact finish, lap times and zero-reset result
match. A mismatch prints `REPLAY DIFFERENT`. `nfsu_race_run_tests` pins the
recording extent, results and hashes; its retail invocation prints the
current values.

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
ledger, and validates selected race views. `--sample-draws` also checks one
view per second; `--full-draws` checks every tick's draw commands. The retail
test prints the sampled view and maximum draw counts. No desktop trace is
described as console execution evidence.

## Drawing, residency and memory

The loader and native textures are #198's `TrackSlice`. The track and car
commands are joined with rebased upload indices and drawn in one deko3d
pass with one camera. Two depth targets are paired with the two swapchain
images to keep their writable surfaces separate. Car placement follows the
shared vehicle transform; the chase camera includes the existing track collision
hook. The car uses the same painted stock scene as the desktop race.
Track culling uses the same camera basis as GPU projection.

The complete list must fit the checked draw limit. Far distance starts at 250 and can
fall back to 125, 62.5 or 31.25; no list is silently truncated. Nxlink samples
show the selected far distance. `DrawExecutionPlan` counts the combined
table's buffer allocations and the backend uses that count. The retail
`nfsu_race_run_tests` invocation validates the current resource and allocation
counts against the aggregate GPU budget.

Residency advances from the actual car position and velocity each tick.
The recovered permanent set of this event stays loaded for both laps; the
retail `nfsu_race_run_tests` invocation prints its loads, releases and bytes read.
The app verifies that each residency step still names exactly its uploaded
set. An unsupported request outside that set stops with a diagnostic;
it never renders stale geometry while pretending a new section was loaded.

The memory plan includes retained native track texture blocks, texture
preparation overlap, the prepared car, collision course and two frame uniform
slots. The retail `nfsu_race_run_tests` invocation prints the current payload
sizes and GPU bound, and enforces their budgets. The Switch race NRO enforces
its load and renderer budgets. The GPU budget
includes bounded image tiling, tight and padded staging, plus room for the
executor, targets, descriptors and frame slots. The backend checks actual SDK
image layouts and every GPU block allocation. Staging lives through the upload
fence; resources live through their last use. The retail ledger test uploads
and retires the combined table without a leaked allocation. Track diagnostic
collision/path copies are discarded
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
MALLOC IN-USE NOW includes GPU BLOCKS. Do not add the two readings. The
`--panel` screen and nxlink `PERF` samples report these heap observations.
The renderer and GPU blocks are created after the after-load sample. At
`e65960b`, the owner's [live photo 4401](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4401)
showed 155528752 - 58887808 = 96640944 bytes of growth; 86659072 was GPU
blocks, leaving 9981872 for other allocations such as readback, command
tables and race state. At the same head, [finish photo 4470](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4470)
showed 155614608 in-use, 85856 more than the live sample. Two samples in
different modes do not prove absence of a leak.

HEAP LOAD/RUN OBS HWM are the largest sampled in-use values, lower bounds
on the true live peak. ARENA NOW/OBS HWM measure allocator extent, not live
bytes. LOAD HEAP OVER compares the sampled load delta with 48 MiB. The
race load delta also includes car/course preparation. #204 merged as PR #207
at `3158b37`; `nfsu_track_loader_peak_tests` measures the exact desktop
allocation peak and enforces the load ceiling. The race's broader load observation
still needs its own console measurement.

## Performance and owner evidence

The first frame, every 120th frame and the first finished frame print `PERF`
samples to stdout/nxlink. Direct-mode samples report CPU frame time against
16.67 ms, simulation, residency/hash/logging, cull/record, update validation
and CPU present time. Present is split into swapchain acquire, slot fence
wait, uniform/command recording and submit. The GPU may work on the previous
frame while the CPU prepares the next. A slot's fence is waited before its
uniform or command memory is overwritten. Direct mode performs no GPU
readback or CPU pixel copy. The added frame slot memory is checked against
the same 256 MiB GPU budget. `PERF HEAP` reports load/run high-water and
arena extent observations, including LOAD HEAP OVER.

The result screen reports the number of measured driving frames, frame-time
mean, p50/p95/p99 and exact maximum, the count and share over 16.67 ms, the
mean of SIM, RES/HASH/LOG, CULL/RECORD, UPDATE, PRESENT CPU and its ACQUIRE,
FENCE, CMD and SUBMIT components, and maximum CATCH-UP ticks. It excludes
countdown, paused, first resumed and finish frames. Statistics reset on
unpause; the screen states the reset count, so a paused run is identified as
the current uninterrupted segment. Percentiles use the upper edge of 0.25 ms
fixed bins through 100 ms and display `>=100` when the selected bin overflows.
The accumulator allocates nothing per frame. Plus during a run shows the same
partial statistics. Panel mode reports its draw, transfer/copy and panel means
in place of direct-present timings.

`--panel` uses the original synchronous readback and CPU framebuffer panel.
Its `PERF PANEL FRAME` and on-screen `Previous PANEL FRAME` include readback
and panel costs. They must not be compared as direct-present frame timings.
All stage measurements are CPU timings, not GPU execution times. The samples
are single frames, not averages; their own nxlink printing follows the frame
measurement. A blocked nxlink stdout connection leaves those lines unavailable
to the coordinator; the result screen is driving-segment timing evidence.
Both modes report the bounded command-cache estimate. The result screen also shows
the last sampled frame summary after rendering stops.

[PR #206 comment 4593](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/206#issuecomment-4593)
records the pre-direct cache run. This branch includes that cache through main;
its NRO still needs a coordinator console run in both modes to establish frame
time and visual correctness.

On 2026-09-27 the owner ran the coordinator's NRO built from `e65960b`.
Its SHA-256 is `45d21ebf744acd2a7dc52926f55ee0658822f6ce1668a7a67035bb56d33f51b3`.
It showed BUILD LOCAL; the staging record ties those photos to the head.
The label propagation above fixes that for subsequent builds.

- At `e65960b`, [PR #199 comment 4401](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4401)
  records the owner's live pad drive with the painted car and track visible and the
  lap timer running. At step 4002: full frame 97.20 ms, SIM 8.46 ms for six
  catch-up ticks, RES/HASH 0.82, CULL/RECORD 52.14, UPLOAD 4.48, DRAW 11.19,
  TRANSFER/COPY 8.04 (GPU 1.11), previous HUD/PRESENT 11.73. Debt was zero;
  draws were 1364 track + 94 car; GPU blocks 86659072 bytes. This is about
  10 FPS, well over the 16.67 ms budget. #55 addresses this measured cost.
- At `e65960b`, [PR #199 comment 4470](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4470)
  records the owner's completed two-lap replay: EQUAL, step 48336, total
  13:26.90 and both hashes matching `nfsu_race_run_tests`. The
  panel directly shows the final lap 6:42.80; lap 1 6:44.10 is implied by
  the total and the app's EQUAL check, not separately read from the photo.
  Its 31.93 ms frame is after finish, when simulation and culling stopped;
  it is not a driving performance measurement. GPU blocks remain 86659072.

These establish live control and cross-architecture replay parity at
`e65960b`; they are not measurements of this direct-present NRO. Preserve
that distinction when adding evidence to #159. A completed live lap has not
been photographed. The new loader/heap panel still needs its own console
photo. No pull request is merged by the worker.

## Performance follow-up (#55)

The race recorder now caches immutable track command templates. The
original path remains available to differential tests; visibility and draw
order are unchanged. Bounded command-cache metadata remains in persistent
accounting. See [SWITCH_PERFORMANCE.md](SWITCH_PERFORMANCE.md) for the
retained baseline and regression thresholds. Prior console photos above
still describe e65960b; the new direct-present build needs a combined
console timing and detailed visual check by the owner and coordinator.
