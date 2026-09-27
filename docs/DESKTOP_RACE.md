# Desktop event 1

Issue #192 integrates the merged vehicle, collision, route, controls, chase
camera, streaming and timing libraries. `nfsu_race` drives the stock 240SX
through event 1: track 1003, two laps, forward. It reads that selection from
the owner's FRONTB/GLOBALB tables and refuses a different event definition.
No retail asset is included here. The recording is controller input only.

## Run

Build with the usual desktop CMake configuration. Substitute the local
installation for `X:/Games/NFSU`. Outputs must be new paths outside that tree.

```text
build-release/nfsu_race --root X:/Games/NFSU --input tests/recordings/event1.txt
build-release/nfsu_race --root X:/Games/NFSU --input tests/recordings/event1.txt --out build-race-frames --frames 180,600,6000,11344,12459,13128,24300,35396,36510,37177,48336
build-release/nfsu_race --root X:/Games/NFSU --live
```

The replay exits 0 only after normal completion, with all requested frames
written and exactly all recorded input consumed. Missing input, trailing
input, a frame after the finish, a simulation error, an existing output, or
the 120000-tick ceiling is an error (exit 1). `--out` also writes `states.txt`:
one completed 60 Hz tick number and its SHA-256 per line, plus
`trajectory.csv` with both 120 Hz physics samples (described below). Without `--out`,
the replay writes no file. Images are 640x360, top-first RGBA TGA with the HUD.

The Windows live backend uses user32/gdi32 and displays the same CPU frame.
W/S are throttle/brake, A/D steer, Space handbrake, R manual reset, C camera,
and Escape or the window close button exits. Keys are sampled only while
the race window has focus; loss of focus sends neutral input. Releasing and
pressing R or C again is required for a second action. The window remains
on the result after the finish. Other desktop platforms support replay;
`--live` reports that the Windows backend is required.

`--live-smoke` exercises four hidden window renders and the fixed-step pump,
then exits, without sampling physical keys or writing files. It is a backend
smoke check, not a claim that a human drove the race.

## Integration and approximations

`RaceSession` owns the following fixed order. It reads no clock or file.

1. Update the recorded pad through `DriveControls` at 60 Hz. During the
   countdown, hold the grid pose, including gravity, and keep sampling pad
   transitions. This lets a neutral connected pad clear the hot-plug latch.
2. When racing, step `VehicleTrackSession` twice at 120 Hz, using the
   controller mapping's input and the car's actual forward velocity. Manual
   reset edges reach that session. It is constructed with
   `vehicle_track_first_lap_approximations()`: the upside-down recovery is
   enabled after 120 physics steps. All other collision and reset rules are
   those documented in [VEHICLE_TRACK.md](VEHICLE_TRACK.md).
3. Feed the route events from the second physics step to `step_race_timing`.
   The timing library owns the countdown, crossings, completed laps and
   finish. Flyby and pre-race menus are skipped; the session starts in the
   countdown stage. Physics first moves on the tick after GO is announced.
4. Update the recovered chase camera and cycle it on a camera-button edge.
   Its segment collision hook queries the actual track contact world; its
   reach behavior remains the approximation disclosed by
   [CONTROLS_CAMERA.md](CONTROLS_CAMERA.md).
5. The desktop samples the resulting car position and velocity into the
   incremental `TrackResidencySession`, once per 60 Hz tick. The same
   `Streamer` implementation and table-byte budget back the batch and live
   APIs. Loads read and parse that section; releases discard its cached
   load. A changed set invalidates the prepared scene. Rendering prepares
   just the current set, resolving textures across its section packs and
   TEX1003TRACK.BIN. Collision and the route remain resident, as before.

The retail streaming policy loads 78 sections, 36471524 bytes read, before
this race, and needs no release or additional load along this recording.
This is the table's track-flag policy, not a whole-track preload substituted
for streaming. The car still drives its zone look-ahead, debouncing and
request rebuilds each tick. Synthetic corridor tests compare every batch
and incremental transition, including constrained budgets and releases.

The scene uses the existing track LOD selection and the texture-row bucket
order documented in [`TRACK_MATERIALS.md`](TRACK_MATERIALS.md). It loads
nonresident Y texture sections from the same stream file when preparing the
scene. Track materials use the decoded alpha test and blend state; the car
keeps its existing reference mode. Both use the same chase camera, and
composition compares their depth buffers (car wins a tie). A regression
test covers both occlusion directions, a depth tie and empty pixels.
No opponents, traffic, sound or new retail lighting are claimed.

The HUD shows the current lap, lap count and last completed lap separately.
Unlike the retail HUD helper's three-second hold, CURRENT continues to count
the new lap immediately; LAST holds the completed one. On the finish it
shows the final lap and total. All displayed times come from race timing,
not a wall clock. At 60 Hz that library advances 67 units of 1/4000 s per
tick, as [RACE_TIMING.md](RACE_TIMING.md) explains; its clock is slightly
faster than exact wall time. Crossing times are backdated and quantized there.

Live pacing uses `RaceStepClock` at 60 Hz with a four-step catch-up cap. Slow
renders drop excess wall-clock steps, so on this CPU the live race advances
slower than real time. Replay has no wall-clock dependency or dropped steps.

## How the recording was produced

```text
build-release/nfsu_race --root X:/Games/NFSU --generate build-generated-event1.txt
```

`RaceRouteDriver` follows the decoded Catmull-Rom progress spline in
[RACE_ROUTE.md](RACE_ROUTE.md), through controller input only. It never
changes the car pose, route progress, timing state, collision, or reset
state. It connects a neutral pad through the 163 countdown ticks, then
chooses an input every six ticks (10 Hz):

- Look ahead a fixed 12 metres, approximating parameter distance with the
  current spline derivative's horizontal magnitude. Keeping that distance
  while braking avoids tightening the pursuit target into a wall at low speed.
- For event 1, shift the target laterally using the authored lane-offset
  table in `RaceRouteDriver::sample`. It is a piecewise-linear function of
  the target's wrapped spline parameter, not elapsed time or lap number.
  Offsets are metres along the target tangent's left normal. They were
  chosen from the decoded collision map and verified by the full trajectory:

  | Parameter range | Lane choice |
  | --- | --- |
  | 0.7-5.0 | Ramp right, up to 12 m, around the split road and southern median; return to zero. |
  | 7.5-8.8 | Up to 6 m left through the diagonal road's narrowing. |
  | 12.1-13.5 | Ramp right to 10 m at 12.3 and 15 m at 12.5, then return; the spline crosses the left barrier here. |
  | 21.5-24.5 | Ramp 12 m left of the long median, then reduce to 4 m before the corner. |
  | 24.5-29.3 | Hold 4 m through the corner, reach 12 m at 25.8, stay east of the trees and inside the diagonal barrier, taper from 28.9 to zero at 29.3. |

  Elsewhere the offset is zero. The table closes continuously at 30/0.
  The CLI explicitly selects `event1_forward`; synthetic courses use
  `progress_spline`. This is an event-specific authored driving line, not
  a general obstacle planner or recovered retail AI.
- Compare normalized spline tangents now and approximately 30 m ahead.
  If their Euclidean difference is `turn`, target speed is
  `max(6, 10 / (1 + 2 * turn))` m/s. This reduces speed before corners.
  Throttle switches on below target minus 0.5 m/s and off above target plus
  0.5 m/s; brake above target plus 0.7 m/s, replacing throttle. The pinned
  input brakes for 5208 ticks; brake and throttle never occur together.
- Use the signed lateral offset of the target relative to the car's
  quaternion forward axis, divided by target distance, with gain 1.8 and a
  clamp at one. Map to raw stick magnitude `27000 * magnitude + 3000`, then
  quantize toward zero in units of 256. Positive stick turns right.
  Keep the last decision for six ticks.

The original round-1 recording was not clean: review 4300 found the lap-1
rollover at steps 11411-11535 and the lap-2 tree wedge at 33175-33700.
Zero resets did not prove otherwise: the sliding upside-down car stayed
above the recovery rule's 2 m/s threshold. The new recording replaces it;
physics, collision and reset rules are unchanged. The progress spline is
only a progress reference and is not itself a guaranteed driving line.

`tests/recordings/event1.txt` has 4895 run-length records, 54142 bytes, LF
line endings pinned by `.gitattributes`, and SHA-256:

```text
CED29AB7D90F0861AFE2C0B604D0EDDFB4F769BEB067944F8E9C7DB0B892E0AC
```

Its ASCII header is `NFSU_RACE_INPUT 1`; each following row is
`ticks buttons raw_steer`. Pad 0 is connected, axis 0 is the recorded signed
16-bit steer, all other axes are zero. Button bits use [INPUT.md](INPUT.md).
Comments start with `#`. The parser caps input at 4 MiB and 120000 ticks,
rejects malformed fields, unknown button bits, non-ASCII and extra fields.

## Deterministic result and state pins

Fresh Debug and Release runs on Windows x64, MinGW GCC 13.2.0, match:

```text
event 1 track 1003 laps 2 outcome finished steps 48336 resets 0
lap 1 ticks 1616400 time 6:44.10
lap 2 ticks 1611200 time 6:42.80
total ticks 3227600 time 13:26.90
state-sha256 79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9
stream loads 78 releases 0 bytes-read 36471524
```

The sequence starts with `NFSU_RACE_STATE 1\n` and the startup residency
transition lines, each with LF. Each subsequent tick contributes its raw
32-byte SHA-256 digest, in order. The tick's hashed bytes are:

- `encode_drive_frame`: tick, both control/vehicle samples, camera and
  controller state; then both full encoded collision telemetry records.
- The progress counters, coordinate and previous percent, followed by
  stage/outcome/timer scalars and the racer's crossing, split and lap arrays.
  `RaceSession::encode` is the explicit little-endian field order; no
  struct padding or native `size_t` is hashed. This is a telemetry record,
  not a serialized checkpoint from which simulation can resume.
- That tick's `track_residency_step_line` plus LF, followed by ascending
  resident table indices as little-endian uint32 values.

The 48336 individual hashes are identical line for line between builds.
Their `states.txt` is 3420750 bytes and hashes to:

```text
E4D4D6E955D59F7FF11C3318BC455D08B7903CA46BBDE8E1FC3F2AC32AEA54E5
```

The session compiles with floating-point contraction and fast-math disabled.
The Switch build compiles the same neutral session and checks its objects
for fused operations. This task establishes desktop Debug/Release parity;
it does not claim to have driven this race on a Switch.

## Frame pins and inspection

Hashes cover complete TGA files, 921618 bytes each, including the lap HUD.
The step is the completed 1-based controller tick.

| Step | TGA SHA-256 | Changed RGB pixels | Reason |
| --- | --- | ---: | --- |
| 180 | `BB6F004E776C7EF6D38056BDBCA828C6650090FC5EF77B37A888E16DECCAC1CE` | 36719 | track alpha test and blend |
| 600 | `0E3F17052A83429C2672495761D09C108809282A71F038E209ED54FE34E4763E` | 39207 | track alpha test and blend |
| 6000 | `BF4FA0E33C5DA962A48FC531E323F15678FE99C0D6F6B744836E17CA76B038C4` | 15022 | track alpha test and blend |
| 11344 | `CB93C540C37DEA70BDD0487350BF10E18F85EC79A981C8C2DA2FE5138619E224` | 12117 | track alpha test and blend |
| 12459 | `FA86B7DADB8BB3DF966F799D5EF84FD2E14A418557E335B368A9854C27D9716F` | 23155 | track alpha test and blend |
| 13128 | `5A62C32877B43E3A18D5A8CEA4901C92D17AD8DC3071ADA6BDCE754259B0E5D1` | 29275 | track alpha test and blend |
| 24300 | `1E435F120A614BD2EDB514D50798C2BFBB1B9D921F4699CDDD5EEA73E5B152AD` | 30292 | track alpha test and blend |
| 35396 | `3EEA282A1E92C158F72C8BA869CD5B2615720C46AD481F5338CF554E40AC58D2` | 12249 | track alpha test and blend |
| 36510 | `728A441AB4DBC3E7193D8C71623DC54C6FB113D1A2511A9E77CA5CE60883903A` | 22834 | track alpha test and blend |
| 37177 | `4A1DD31DBE93AED7A296872C3EA1F9D2988E3C2389DCEE7879B2865733A4D406` | 29211 | track alpha test and blend |
| 48336 | `FE68B0CA6BB8E79503563781F22D6FEA6CE5177CFB387EFC63577D22DFCDF2BC` | 29783 | track alpha test and blend |

For each row, the changed-pixel count compares RGB values against a fresh
`origin/main` Release render of the same recording. A control build with the
new texture sources and draw order but track material state set to `off`
reproduced all 11 old TGA files byte for byte. Thus the alpha-test/blend
activation accounts for every changed pixel in these pins; it left every
TGA alpha byte and every replay state row unchanged. Debug and Release
produce identical new TGA bytes at all 11 steps.

Inspected the corner (11344, 35396), tree median (12459, 36510) and former
rollover barrier (13128, 37177) on both laps: the car follows the paved lane,
upright and clear of the trees and barrier. Frame 24300 shows the second lap
starting at CURRENT 0:00.19, LAST 6:44.10. Frame 48336 shows CURRENT and LAST
6:42.80, FINISHED 13:26.90. Opaque foliage and unresolved checkered surfaces
remain; these pins preserve the current CPU reference rendering.

## Continuous trajectory audit

`--out` writes `trajectory.csv` in the classic locale, with 17 significant
digits, LF and columns `tick,physics,lap,x,y,z,speed,up,contacts,road_wheels,resets`.
`physics` is the 1-based 120 Hz step; `tick` is the enclosing 60 Hz controller
step. Countdown does not step physics and emits no sample. Position,
horizontal speed, orientation and body-contact counts come from the existing
post-collision telemetry; `up = 1 - 2*(qx*qx + qy*qy)`. Road-wheel counts use
that physics step's wheel probes. No samples are filtered out after GO.
The CSV has 96346 samples, 11120490 bytes and SHA-256:

```text
8203932E99CDA190CEE81C6171705224AC7CC4EE910246805350605304F1D439
```

The regenerated run has zero body-contact samples, zero resets, no inversion
or tilt past 45 degrees, and no sample with all four wheels unsupported.
Minimum up is 0.9806514207882611 (11.29 degrees from vertical). After tick
600, minimum horizontal speed is 5.211024131499977 m/s: neither median stall
remains. Four road-wheel probes hit in 95854 samples, three in 232, two in
146 and one in 114; this is not a claim that every wheel remains grounded.
The checker verifies the CSV pin and also requires complete ordered pairs,
zero contacts/resets, up >= 0.98, at least one road wheel, and speed >= 5 m/s
after the launch allowance through tick 600. Thus merely finishing with zero
resets cannot pass this audit again. The fixed images additionally show the
lane choice at the previously troublesome locations on both laps.

The reproducible check uses fresh directories, verifies process success,
result lines, every state row, every frame file, the printed hashes and the
continuous trajectory checks, and
optionally regenerates the recording independently in each build:

```text
python tools/race_replay_check.py --exe build-debug/nfsu_race.exe --exe build-release/nfsu_race.exe --root X:/Games/NFSU --out build-race-check --check-driver
```

Python uses only the standard library. It resolves its recording relative
to the script; no workspace path is embedded. The script's help smoke was
also run from the actual non-ASCII-named owner workspace.

## Desktop cost and gates

Intel Core i7-12700K, Windows x64, Release, eleven requested renders over the
full replay; warm local files. Timings are observations, never hash inputs.
The program reports count, total, mean and maximum for every stage.

| Stage | Calls | Mean ms/call | Maximum ms |
| --- | ---: | ---: | ---: |
| Controls, two physics steps, route, timing, camera, record encoding | 48336 | 0.1276 | 1.079 |
| Residency decision | 48336 | 0.0019 | 0.095 |
| Hashing and per-step text output | 48336 | 0.0118 | 0.192 |
| Stream section read/parse | 39 | 0.930 | 11.34 |
| Scene merge, texture decode, draw list | 1 | 107.64 | 107.64 |
| CPU track/car render, depth composition, HUD | 11 | 111.44 | 132.77 |

Stream I/O and scene preparation above are startup costs for this run.
TGA/trajectory writing, initial collision/car loading and window blitting are outside
these stage measurements. The renderer dominates: this is a working desktop
integration, with no claim of 60 Hz rendering on this CPU. The four-frame
hidden live smoke also completed; its timing depends on concurrent load.

Validation: full desktop Debug and Release CTest, 70/70 each; full Switch
Release build and CTest, 15/15; no build warnings. This uses `origin/main`
`fe30765`, including #193's whole-tree floating-point policy and census,
which includes `nfsu_race_session`. New tests cover the pad
recording format, countdown holding gravity, connected-neutral input before
throttle, two physics steps per tick, deterministic integrated state, reset
and camera edges, camera contact, depth composition and CLI output guards.
Round 2 additionally tests unknown button bits, a refused camera segment,
an actual two-lap synthetic drive with lap/current/last/finish HUD checks,
and refusal to advance after finish without changing state. The HUD and
live clock initialization now handle failure returns.
The existing residency cases additionally compare incremental and batch
transitions. Retail verification regenerated the exact recording in both
builds and replayed it to the same result, states, eleven frames and complete
120 Hz trajectory.

The SDK build used a writable `TMPDIR`; its default sandbox `/tmp` was not
writable. Full desktop CTest was run outside the restricted sandbox because
the new path guards' canonicalization could not access freshly created
fixture directories there. The guards then passed without weakening them.
All 476 retail files were unchanged in before/after manifests comparing
relative name, size, modification time and SHA-256. All added paths and
tracked text, including the automation, pass the repository ASCII scan.
