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
build-release/nfsu_race --root X:/Games/NFSU --input tests/recordings/event1.txt --out build-race-frames --frames 180,600,6000,22000,40000,43564
build-release/nfsu_race --root X:/Games/NFSU --live
```

The replay exits 0 only after normal completion, with all requested frames
written and exactly all recorded input consumed. Missing input, trailing
input, a frame after the finish, a simulation error, an existing output, or
the 120000-tick ceiling is an error (exit 1). `--out` also writes `states.txt`:
one completed 60 Hz tick number and its SHA-256 per line. Without `--out`,
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

The scene uses the existing track LOD selection and material-group draw
order. Track and car render with the same chase camera and opaque reference
material mode, then the nearest depth wins (car wins a tie). A regression
test covers both occlusion directions, a depth tie and empty pixels. The
track's transparent foliage is consequently visibly opaque/checkered in
some frames; this integration does not recover new track material states.
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

- Look ahead `8 + 0.5 * horizontal_speed` metres, approximating parameter
  distance with the current spline derivative's horizontal magnitude.
- Use the signed lateral offset of that target relative to the car's
  quaternion forward axis, divided by target distance, with gain 1.8 and a
  clamp at one. Map to raw stick magnitude `27000 * magnitude + 3000`, then
  quantize toward zero in units of 256. Positive stick turns right.
- Cruise with throttle hysteresis: on below 9.5 m/s, off above 10.5 m/s;
  brake above 12 m/s. Keep the last decision for six ticks.

The spline briefly traverses a reset zone and is not itself a guaranteed
driving line. This finite look-ahead trajectory completes both laps on the
collision surface without entering a reset: zero resets in the pinned run.
It is a conservative completion driver, not a racing AI or a retail driving
model change.

`tests/recordings/event1.txt` has 4054 run-length records, 45391 bytes, LF
line endings pinned by `.gitattributes`, and SHA-256:

```text
0308B3B94954F7C47DA6522151D8FB90E2D1F954D65B628C5F967263A834BC82
```

Its ASCII header is `NFSU_RACE_INPUT 1`; each following row is
`ticks buttons raw_steer`. Pad 0 is connected, axis 0 is the recorded signed
16-bit steer, all other axes are zero. Button bits use [INPUT.md](INPUT.md).
Comments start with `#`. The parser caps input at 4 MiB and 120000 ticks,
rejects malformed fields, unknown button bits, non-ASCII and extra fields.

## Deterministic result and state pins

Fresh Debug and Release runs on Windows x64, MinGW GCC 13.2.0, match:

```text
event 1 track 1003 laps 2 outcome finished steps 43564 resets 0
lap 1 ticks 1497480 time 6:14.37
lap 2 ticks 1410360 time 5:52.59
total ticks 2907840 time 12:06.96
state-sha256 1FFF1EB3932B6044C81A9D5671FE6D3D45B7D82ACB8B511D2E1441519E127F0D
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

The 43564 individual hashes are identical line for line between builds.
Their `states.txt` is 3081938 bytes and hashes to:

```text
440C844BC13D8C502735120708C590B3CB7237C17254EA8A15F52CA3F5E9A6A4
```

The session compiles with floating-point contraction and fast-math disabled.
The Switch build compiles the same neutral session and checks its objects
for fused operations. This task establishes desktop Debug/Release parity;
it does not claim to have driven this race on a Switch.

## Frame pins and inspection

Hashes cover complete TGA files, 921618 bytes each, including the lap HUD.
The step is the completed 1-based controller tick.

| Step | TGA SHA-256 |
| --- | --- |
| 180 | `31FFD8366842E6B051112CB103784A875CED47AFA4EA36A1370FC9BC13A900C2` |
| 600 | `C097A3DFF189EA00B6EB43FC71EAFBD581553D2F7A4BFE80E1B2FB502204AD73` |
| 6000 | `97F1458970990349A44C50225C5239A2E20CB33327CFEE5D988EF8DC3A3B6274` |
| 22000 | `A37F1A53EFDB8E74B85AA0F298D4E8C0FD74B1F1C382B0F4A5D00961F9CCA9C4` |
| 40000 | `2476F4069F955DBCAC084CB575DB491317B1700E29106E7418A7CC83190B80BB` |
| 43564 | `2BF5E5AC93E2A6BB6D14FCF93DAF490C0D178B1CE5ECC49FF62432C08AD7ACCC` |

Inspected frames 6000, 22000 and 43564: the chase view follows the blue 240SX
on the city road; at 6000 the car follows yellow road markings between
buildings, at 22000 it approaches the broad junction near the lap boundary,
and at 43564 it is just past the line with LAP 2 / 2, LAST 5:52.59 and
FINISHED 12:06.96. The car and road meet coherently. Opaque foliage and some
unresolved checkered surfaces remain visible, especially at the junction
and finish; these pins preserve the current CPU reference rendering.

The reproducible check uses fresh directories, verifies process success,
result lines, every state row, every frame file and the printed hashes, and
optionally regenerates the recording independently in each build:

```text
python tools/race_replay_check.py --exe build-debug/nfsu_race.exe --exe build-release/nfsu_race.exe --root X:/Games/NFSU --out build-race-check --check-driver
```

Python uses only the standard library. It resolves its recording relative
to the script; no workspace path is embedded. The script's help smoke was
also run from the actual non-ASCII-named owner workspace.

## Desktop cost and gates

Intel Core i7-12700K, Windows x64, Release, six requested renders over the
full replay; warm local files. Timings are observations, never hash inputs.
The program reports count, total, mean and maximum for every stage.

| Stage | Calls | Mean ms/call | Maximum ms |
| --- | ---: | ---: | ---: |
| Controls, two physics steps, route, timing, camera, record encoding | 43564 | 0.1155 | 0.848 |
| Residency decision | 43564 | 0.0016 | 0.054 |
| Hashing and per-step text output | 43564 | 0.0111 | 0.209 |
| Stream section read/parse | 39 | 0.814 | 10.23 |
| Scene merge, texture decode, draw list | 1 | 92.95 | 92.95 |
| CPU track/car render, depth composition, HUD | 6 | 102.17 | 115.61 |

Stream I/O and scene preparation above are startup costs for this run.
TGA writing, initial collision/car loading and window blitting are outside
these stage measurements. The renderer dominates: this is a working desktop
integration, with no claim of 60 Hz rendering on this CPU. The four-frame
hidden live smoke also completed at roughly 104 ms per render.

Validation: full desktop Debug and Release CTest, 70/70 each; full Switch
Release build and CTest, 15/15; no build warnings. This uses `origin/main`
`fe30765`, including #193's whole-tree floating-point policy and census,
which includes `nfsu_race_session`. New tests cover the pad
recording format, countdown holding gravity, connected-neutral input before
throttle, two physics steps per tick, deterministic integrated state, reset
and camera edges, camera contact, depth composition and CLI output guards.
The existing residency cases additionally compare incremental and batch
transitions. Retail verification regenerated the exact recording in both
builds and replayed it to the same result, states and six frames.

The SDK build used a writable `TMPDIR`; its default sandbox `/tmp` was not
writable. Full desktop CTest was run outside the restricted sandbox because
the new path guards' canonicalization could not access freshly created
fixture directories there. The guards then passed without weakening them.
All 476 retail files were unchanged in before/after manifests comparing
relative name, size, modification time and SHA-256. All added paths and
tracked text, including the automation, pass the repository ASCII scan.
