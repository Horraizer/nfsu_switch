# Event 1 on Switch (#196)

`nfsu_switch_race.nro` runs the selected 240SX through event 1's forward
track-1003 circuit for two laps. It uses `RaceSession` from #192, the #59
controls and chase camera, #157 residency, and #198's deko3d track path with
#171 native DXT. The race also uses the command cache merged in #206 and
the bounded track loader merged in #207.

## Install and use

Copy the supplied NRO to `sdmc:/switch/nfsu_switch_race.nro` and launch in
full-memory Homebrew Menu mode. Keep the existing 12 first-lap files under
`sdmc:/nfsu`. The default stock selection needs no additional retail file: the track-info table is
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
arguments are `[sdmc:/nfsu] [--live|--replay] [--panel|--profile-frame]
[--sky on|off|early] [--particles=on|--particles=off] [--tuned]
[--glows] [--car-shading on|off] [--wet-road-reflections on|off]`.
`--profile-frame` requires direct presentation.
`--sky` exists for console cost A/B runs (issue #227): `on`, the default,
draws the sky after the opaque world; `off` records no sky draws; `early` is
a diagnostic that submits the sky with the opaque pass at its list position,
as the renderer did before #227. Every result page names the mode.
RomFS holds the three checked
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
it before creating the deko3d swapchain. It draws the #64 speed, tachometer,
needle, red zone and gear at native 1280x720 after any scene upscale;
`--no-hud` disables that overlay for paired measurements. Other race status
still reaches nxlink. Use `--panel` or menu Y for the diagnostic readback
panel with race status and heap rows. After the race finishes or Plus is pressed, the
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

The first command is CTest's `nfsu_race_run_tests` and needs no game data. It
checks clock debt, recording identity and extent, invalid call order,
resource/texture rebasing, capacity refusal and mismatched cameras. The retail
run checks every step hash and the result, uploads/retires the combined table
through the neutral ledger, and validates selected race views.
`--sample-draws` also checks one view per second; `--full-draws` checks every
tick's draw commands. The retail test prints the sampled view and maximum draw
counts. No desktop trace is described as console execution evidence.

CTest runs the third command as `nfsu_race_run_retail` (label `retail`) when
the build is configured with `-DNFSU_RETAIL_ROOT=<installation>`, and a
Release configure also registers the `--full-draws` run as
`nfsu_race_run_retail_full_draws` (labels `retail` and `retail-full`).

## Drawing, residency and memory

The loader and native textures are #198's `TrackSlice`. The track and car
commands are joined with rebased upload indices and drawn in one deko3d
pass with one camera. Two depth targets are paired with the two swapchain
images to keep their writable surfaces separate. Car placement follows the
shared vehicle transform; the chase camera includes the existing track collision
hook. The car uses the same selected, painted scene as the desktop race.
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
bytes. The load HWM is an absolute in-use reading. Its delta is
`load_peak - before_load` (clamped at zero), and LOAD HEAP OVER is
`max(0, delta - 50331648)`. The absolute HWM threshold is therefore the
before-load reading plus 50331648 bytes; it is not a larger budget. The
race load delta includes car, track, course, audio, and first-frame preparation.
The renderer opens after the load sample, so its GPU allocations affect the
run HWM but not the load HWM. `nfsu_race_loader_peak_tests` measures the
complete desktop C++ allocation peak through the first frame against the
same 48 MiB ceiling. `nfsu_track_loader_peak_tests` separately measures the
track slice. Configured with `-DNFSU_RETAIL_ROOT`, CTest runs both as
`nfsu_race_loader_peak_retail` and `nfsu_track_loader_peak_retail` (label
`retail`). Console acceptance requires a photographed LOAD/MEMORY page
showing LOAD HEAP OVER 0 at the tested head.

## Performance and owner evidence

`--profile-frame` collects the Phase A timing evidence requested in
[FRAME_BUDGET_RESEARCH.md](FRAME_BUDGET_RESEARCH.md). Use the bundled replay
in direct mode. After FINISHED, the result remains frozen until Plus; D-pad
left/right selects numbered identity, replay, active-frame, CPU-stage, GPU
scene and load/memory pages. Each page repeats its build and mode label and
uses at most eight data rows of 80 ASCII columns. The replay page splits both
hashes into labeled halves. The application cannot know the SHA-256 of its
own NRO, so the coordinator records that hash with the full commit and
toolchain/deko3d version of its build. If the build tag is a full commit SHA,
the identity page prints it. CPU, GPU and memory/EMC clock readings that cannot
be queried are explicitly marked unavailable.

CPU rows give count, mean, p95 bin upper edge and exact maximum. CULL/RECORD
uses the five `RaceFrameProfile` subregions. UPDATE separates validation
from uniform writes. PRESENT separates acquire, fence wait, uniform write,
command recording, finishList and submit/flush; PARENT SUM and OTHER account
for the measured frame without adding nested child rows twice. `mallinfo()`
after present has its own row. A GPU timestamp pair brackets each profiled
scene in a per-slot report. The report is read after that slot's completion
fence, with remaining reports drained after the run. The pair measures a
pipeline span, not GPU utilization. Counter reports flush tiled cache and
the profiling build includes their overhead. The result shows completed,
invalid and dropped queries rather than treating missing data as zero.

Only active driving frames enter the aggregates. Countdown, pause, the first
resumed frame and FINISHED remain excluded; unpause resets the CPU and GPU
segments. The normal direct and panel results remain available without
`--profile-frame`. No acceptance claim follows from a desktop build or from
these counters until the coordinator photographs the Switch result pages.

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
mean, p50/p95/p99 and exact maximum, the count and share above the stated
slow-frame threshold from `RaceFrameStats`, the
mean of SIM, RES/HASH/LOG, CULL/RECORD, UPDATE, PRESENT CPU and its ACQUIRE,
FENCE, CMD and SUBMIT components, and maximum CATCH-UP ticks. It excludes
countdown, paused, first resumed and finish frames. Statistics reset on
unpause; the screen states the reset count, so a paused run is identified as
the current uninterrupted segment. Percentiles use the upper edge of 0.25 ms
fixed bins through 100 ms and display `>=100` when the selected bin overflows.
The GPU SCENE page shows its own completed-pair count, first and last game
ticks, inclusive tick span and resume count. Its distribution covers that
window, which may be much shorter than the total replay after an unpause.
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

## Tuned race selection (#237)

For the integrated demo, these commands enable the red tuned car, tyre smoke
and sparks, sky, car light glows, paint shading, wet road reflections, and the
ground shadow. The shadow is part of the race scene and has no CLI toggle.

```text
nxlink -s build-switch/switch/nfsu_switch_race.nro -- sdmc:/nfsu --live --tuned --particles=on --sky on --glows --car-shading on --wet-road-reflections on
nxlink -s build-switch/switch/nfsu_switch_race.nro -- sdmc:/nfsu --replay --profile-frame --tuned --particles=on --sky on --glows --car-shading on --wet-road-reflections on
```

Append `--tuned` to either race executable to use the viewer's red 240SX,
KIT01 bumpers and skirts, BBS_STYLE01_15_23 wheels and TRIBAL5 vinyl.
Desktop example: `nfsu_race --root X:/Games/NFSU --live --tuned` (see
`--help` for replay and frame output). Switch paired replay commands:

```text
nxlink -s nfsu_switch_race.nro -- sdmc:/nfsu --replay --profile-frame
nxlink -s nfsu_switch_race.nro -- sdmc:/nfsu --replay --profile-frame --tuned
```

Keep the first-lap files above and add these files from the game's own
installation at the same relative paths beneath `sdmc:/nfsu`:

```text
Cars/WHEELS/GEOMETRY.BIN
Cars/WHEELS/TEXTURES.BIN
Cars/240SX/VINYLS.BIN
```

Both race loaders name every unread car input and explain where to copy it.
The selection is visual only: the vehicle, input mapping, contact solver,
fixed time step, residency and replay hashes do not receive the flag.
The normal invocation continues to select the stock car. Profile pages
identify STOCK or TUNED so paired photographs cannot silently swap cars.

The race calls `tuned_240sx_request` and `tuned_240sx_skin` through the same
part resolution and skin preparation as the viewer. Per-car wheel/brake
placement and the shared animated transform handle either selection. The
race refuses tuned car commands with an unbound texture; the CPU frame path
also requires zero fallback triangles. The existing draw and memory limits
remain in force.

Tuned race loading releases parsed raw geometry before reading the vinyl
layer for composition. The common geometry parser validates every wheel
mesh but retains decoded payload only for the already-selected wheel hashes;
all directory identities and offsets remain available to its validation.
This is a retention policy, not a second decoder or part resolver. Stock
loading and viewer preparation retain their existing ownership behavior.

With `NFSU_RETAIL_ROOT` set, CTest runs `nfsu_tuned_race_replay_retail`,
`nfsu_tuned_race_draws_retail` and `nfsu_tuned_race_loader_retail`. Each uses a
fresh output directory and compares complete before/after retail manifests.
`tools/race_replay_check.py --tuned --check-driver` pins the frame set with a
reason for each view and checks every replay step, result, regenerated input
and clean trajectory. `nfsu_race_run_tests ... --full-draws --tuned` compares
every cached draw against the reference on every tick, checks animated parts
and reports stock/tuned draw costs. `nfsu_race_loader_peak_tests ... --tuned`
checks all live allocations through audio and the first frame against the
same load ceiling. The corresponding commands without `--tuned` retain the
stock replay and frame pins.

Desktop tests do not establish Switch frame time. Console acceptance needs
a photograph of the tuned car racing and paired GPU SCENE replay pages for
stock and tuned at the same build, mode and uninterrupted game-tick window.
The target remains 60 fps; there is no alternate simulation rate or 30 fps
acceptance mode.
