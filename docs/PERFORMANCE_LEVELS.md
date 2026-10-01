# Explicit performance levels and handling telemetry

Issue #250. The race defaults to the legacy stock 240SX. `--tuned` selects
cosmetic parts and paint only. Performance setup is opt-in, independent of
that visual choice, with identical syntax on desktop and Switch:

```text
--performance-levels engine-a=1,gearbox=1,weight=1,suspension=1,tyres=1
```

This is the `performance1` evidence setup. All omitted levels are zero.
Names identify recovered tables rather than unverified retail menu labels:
`weight`, `suspension`, `engine-a`, `engine-b`, `induction`, `gearbox`,
`tyres`, `nitrous`, `aero-a`, `aero-b`, `grip`. Each accepts exactly one
integer 0..3. Unknown names, duplicate names/options, malformed values and
out-of-range levels fail explicitly. Item booleans are not CLI options.
For explicit stock, use `--performance-levels weight=0`.

`load_race_vehicle_spec` reads the original CarTypeInfo record named 240SX
from GLOBALB.BUN, applies `apply_vehicle_tuning` into a separate record,
decodes handling, validates the resulting model, then constructs the race
with that VehicleSpec. Setup runs once at race creation; reset reuses that
model and its derived shift thresholds. The source record stays read-only.
An invalid setup does not silently fall back to stock.

Zero-level setup is the recovered stock *instance* rule: it still rebuilds
box inertia and adds the second torque curve. The retail 240SX has zero
second-curve values and matching inertia, so its explicit-zero spec and
replay are identical to legacy stock. RX7 has a nonzero second curve;
`nfsu_performance_levels_retail` verifies its contribution exactly once.
This does not add RX7 race visuals or change the default car.

| Table | Fields used by the current solver | Decoded without the retail runtime rule |
| --- | --- | --- |
| Weight | Mass, longitudinal CG, rebuilt box inertia | Alternate yaw-inertia multiplier |
| Suspension | CG/wheel height, progressive spring, damping, anti-roll, bump/droop | Unknown eighth strut word |
| Engines, induction | RPM bounds, torque curve, engine braking | Unknown +0x30C |
| Gearbox | Ratios, final drives, drive split, gear count, shift fraction and rebuilt shift thresholds | Unknown gearbox words |
| Tyres | Static/sliding friction in the existing approximate friction circle | Slip, lateral-load and lateral-force scales |
| Nitrous, aero, grip | None | Blocks applied to the copied record; no nitrous, downforce or grip helper is implemented |

Steering laws, tyre stiffness balance, traction clamp, collision rules,
HUD layout and renderer filtering are unchanged. An improvement in one
decoded scalar is not a claim of retail handling parity. The solver retains
the approximations documented in VEHICLE.md and HANDLING_GAPS.md.
HUD consumers can keep using the existing read-only `RaceSession::spec()`
accessor. Its handling is the chosen, immutable model setup; no new accessor
or dependency on explicit upgrades is required for #253.

## Captures

Run from your own worktree, using a real Python and the toolchain on PATH.
The examples read retail files and write only under the ignored build tree.
Use fresh exact output names; remove only your own previous outputs before
repeating them. Bracket measurements with the repository fixture:

```powershell
$retail = 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'
cmake -DMODE=before "-DROOT=$retail" -DMANIFEST=build/250-owner-manifest.txt -P tests/retail_manifest.cmake
./build/nfsu_race.exe --root $retail --input tests/recordings/event1.txt --performance-levels weight=0 --handling-telemetry > build/250-owner-stock.txt
./build/nfsu_race.exe --root $retail --input tests/recordings/performance1.txt --performance-levels engine-a=1,gearbox=1,weight=1,suspension=1,tyres=1 --handling-telemetry > build/250-owner-upgrade.txt
cmake -DMODE=after "-DROOT=$retail" -DMANIFEST=build/250-owner-manifest.txt -P tests/retail_manifest.cmake
```

Telemetry goes to stdout, prefixed `handling-v1` (column names) and
`handling ` (CSV rows). Normal execution emits neither. Two 120 Hz substeps
are emitted every sixth driving tick, up to 2400 rows (120 driving seconds).
Countdown has no physics rows. Logging uses fixed storage and stdio,
without creating per-frame strings or growing a buffer. The sample spacing
does not measure the full 120 Hz steering response; a console trace also
includes its own stdout cost. For performance measurement omit the flag.

Rows identify the simulation tick, host display frame when available,
global collision step and substep. Raw axis/button values describe pad slot
0; both race entry points currently populate only that slot. Disconnected
raw inputs, absent host timing and zero-speed body slip are `unavailable`.
Wheel slip is unavailable without ground contact; airborne load and applied
torque remain the actual quantities the solver calculated.
Filtered steering is the actual DriveControls output supplied to that
physics step. Requested road angle is observed before the existing slew;
actual angle and world-z yaw rate are model-step-end values, before the
track adapter's body contacts or resets. Body slip is
atan2(lateral speed, forward speed). Wheel loads are N, requested/applied
drive torque is N m, wheel slip ratio/lateral slip are dimensionless, gear
uses the model's slots (reverse 0, neutral 1, first 2). Camera heading is in
radians and is the frame-end camera shared by the two substep rows.

For live input, sample age is microseconds from completion of host polling
to entry at the simulation boundary. Switch catch-up ticks reuse the outer
frame's pad sample, so its age grows. Desktop polls keyboard values before
each tick. Replay supplies logical ticks rather than a sensor clock;
its display frame and sample age are unavailable. These are host ages,
not controller sensor, presentation or total input-to-photon latency.
Polling metadata and requested road angle are excluded from legacy encoders.

## Replay evidence and hardware request

Historical captures at `c48e763`, default desktop build, commands above,
tick 602 / collision step 878 / second substep (angles rounded here):

| Quantity | Explicit stock | performance1 |
| --- | --- | --- |
| Raw steer axis | 4608 | 4096 |
| Filtered steer | -0.0595556 | -0.0405926 |
| Requested / actual road angle, rad | -0.0194796 / -0.0194796 | -0.0137197 / -0.0137197 |
| Model yaw, rad/s | -0.0264321 | -0.0185461 |
| Gear slot / engine rpm | 2 / 3863.20 | 2 / 3618.65 |
| Rear-left load, N | 2750.86 | 2516.83 |
| Rear-left requested / applied torque, N m | -580.623 / -580.623 | -610.102 / -610.102 |
| Host sample age | unavailable | unavailable |

The route controller uses distinct stock/upgraded recordings; these rows
illustrate instrumented boundaries. The negative torque is model engine
braking during that input sample. Use the same-input straight acceleration
test for a controlled comparison. The historical captures each reached the
logging cap and finished without resets. The upgrade replay was regenerated
with the route driver and then independently replayed before storing its
separate pins. Both before/after retail manifests were unchanged.

`nfsu_performance_levels_tests` pins stock/upgraded straight acceleration,
automatic gear transition steps and the synthetic upgraded race. It checks
every exposed table at levels 1..3, mass/CG/inertia, ratios/RPM/torque,
suspension and all tyre fields. `nfsu_performance_levels_retail` repeats the
checks on the actual 240SX record and checks the RX7 second curve.
`nfsu_performance_replay_retail` checks explicit-zero against the existing
stock RaceRun pins and the separate performance1 recording twice, with
logging off/on. New replay pins live in race_run.h, acceleration and shift
pins in performance_levels_tests.cpp. They change because the model receives
selected torque/ratios/mass/suspension/friction; no old pin is replaced.
The gentle route driver deliberately controls speed, so its finish time
alone is not a useful full-throttle upgrade comparison.

Switch bundles both controller recordings. `--replay` selects event1 for
legacy/explicit-zero setup and performance1 for the exact evidence setup.
Other nonzero replay configurations are refused; they remain valid in live
mode. Replay verification chooses the corresponding distinct result pins.

With the homebrew netloader waiting, the owner can capture stdout via nxlink:

```text
nxlink -a 192.168.1.56 -s build-switch-ascii/switch/nfsu_switch_race.nro --args sdmc:/nfsu --live --performance-levels weight=0 --handling-telemetry
nxlink -a 192.168.1.56 -s build-switch-ascii/switch/nfsu_switch_race.nro --args sdmc:/nfsu --live --performance-levels engine-a=1,gearbox=1,weight=1,suspension=1,tyres=1 --handling-telemetry
```

Replace `--live` with `--replay` for the corresponding deterministic capture.
The worker does not deploy. The PR CONSOLE_REQUEST records source SHA, NRO
SHA-256 and its retained location. Drive each setup from rest on the same
straight and corner: compare launch, automatic shifts, steering response,
exit acceleration and lift-off. Keep visuals identical for this comparison.
Repeat without telemetry for driving frame statistics; target remains
60 fps. Owner driving feedback and console performance remain pending until
board evidence is actually obtained, and are not desktop test passes.

## Windows Switch packaging

Desktop gates run from the real worktree under the non-ASCII workspace.
The installed SDK's `elf2nro` cannot open a RomFS directory under that path.
For Switch packaging only, use a free temporary ASCII drive alias pointing
to the same worktree, derived from PowerShell's current directory:

```powershell
if (Test-Path -LiteralPath 'N:/') { throw 'N drive already exists' }
subst N: (Get-Location).Path
```

From MSYS2, `cd /n`, configure `build-switch-ascii` using the documented
Switch Release command in SWITCH_BUILD.md, with an ASCII source build tag,
then build and run its complete CTest suite. The physical build still lives
inside the worker's own worktree. Remove only this alias after use with
`subst N: /D`; the NRO remains at its real workspace location. This is a
packaging workaround, not a claim that the SDK passed non-ASCII paths.
