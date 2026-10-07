# Handling, tyre and RPM continuation

Issue #251 continuation from accepted baseline `743ebc0`, with #324 and
#250/#275 as explicit dependencies. This candidate corrects the original
service-brake wheel pin and records its previously unresolved feedback
producer. Complete original driving parity and hardware feel remain open.

## Original execution boundary

The input identities are the `speed.exe`, `GLOBALB.BUN` and stock 240SX
record identities already recorded in `HANDLING_GAPS.md`. Both native tools
authenticate them and bracket execution with complete retail content and
metadata manifests. They use the tracked `world_body_trace.Image` PE mapper
and `physics256_native.Machine`. No retail process is launched or modified.
Synthetic x87 control word `0x007F` does not prove the live environment.

`tools/handling_brake_compare.py` executes the actual completed engine
update `0x004757D0..0x004759FE` with synthetic wheel spins, original stock
setup, first/second/reverse gear and prior latch flags. Its strut call is an
explicit logged stub. It compares the original wheel pin with the actual
port helper through `handling_brake_compare.cpp`, including both sides and
equality of the binary32 gate thresholds. The original locked-wheel
derivative consumer is executed separately without substituting torque or
feedback calculations: locked wheels take its zero-output path.

Recovered chain:

- `0x00474B4B..0x00474B6F`: wheel brake axis is the maximum of processed
  service brake and half the separate handbrake axis.
- `0x004757F0..0x00475845`: axis strictly above float `0.95` and absolute
  wheel spin strictly below float `30` rad/s set `engine+0xB8+4k` and set
  `engine+0x24+4k` to zero. The flag persists until axis is strictly below
  float `0.2`. Equality does not set or clear it.
- `0x0047557A..0x00475582`: a set flag bypasses that wheel's angular
  derivative calculation; output at `0x00475797` is zero.
- `0x00475847..0x004759FE`: completed wheel state supplies engine RPM and
  gear, clamped at idle/max. The accepted export/HUD chain remains direct.

The port evaluates the original gates on float copies of its double wheel
state/input. It preserves the latch for subsequent forces and applies the
pin after integration and before completed engine RPM. Reset clears it.
The tyre force calculation still uses the existing approximation when a
wheel is locked; the latch alone does not recover that force calculation.

`VehicleWheelTelemetry::brake_locked` is part of canonical record identity.
The existing wheel contact u32 is now a flags word: bit0 contact, bit1 brake
latch. Size and unlatched bytes stay identical. Two equal present wheel
spins with different latches encode differently and produce different RPM
on the next partial-brake/throttle step. This is tested explicitly.

The old `VEHICLE.md` sentence saying the exact pin is absent, and the old
unconditional no-moving-idle regression, are superseded by this correction.
The original gate depends on wheel spin, not body speed: tyre slip means
there is no universal body-speed threshold for its idle transition.
Near-full service brake can correctly show idle while the body is moving.
The half-axis handbrake cannot activate this pin by itself.

## Feedback producer found; integration remains open

The previous `VEHICLE.md`/`HANDLING_GAPS.md` unknown writer of
`engine+0x74+8k` now has a bounded source witness. Engine vtable `0x006C847C`
virtual `+0x14` points to `0x00475D00..0x0047623E`; stores
`0x004761BC/0x004761C2` populate `+0x74/+0x78` for each wheel. The angular
consumer at `0x0047576B..0x00475781` reads the first component before its
division by the original inertia. `0x00474D18` invokes the producer.

`tools/handling_feedback_native.py` executes the producer and original
matrix/trig/force helpers without external hooks. Its assumptions are
explicit: stock geometry, synthetic flat support/load, identity body
rotation, zero yaw/steering, no boost and fixed synthetic wheel/body speeds.
It binds matrix dispatch `0x006D4AC4` to original scalar `0x005ABDB5`;
the live CPU-selected implementation is unverified. Declared helper ranges
contain unused padding; the report lists every executed instruction address.

This identifies and executes the producer, but does not establish full
car/mode/contact/surface/device gates, force application across heading and
terrain, actual live wheel-engine transients or original scheduler cadence.
Those must be resolved before replacing longitudinal stiffness, inertia,
engine-brake scale, ideal traction control or brake torque wholesale.
No guessed grip multiplier or needle follower is introduced here.

## Scenario matrix and reproduction

`tools/handling_scenarios.cpp` uses actual VehicleModel and recovered1
steering on synthetic flat ground. Its context mirrors the existing
`recovered_matrix.cpp`; direct scripted input excludes live pad filtering
and display timing. Every step records speed, yaw, lateral speed, completed
engine/wheel RPM, gear, wheel spins and rear force/sliding state.

| Scenario | Port before/after evidence | Original evidence boundary |
| --- | --- | --- |
| Acceleration then coast | Exact sequence equivalence outside the pin | Torque/ratio consumers; complete live coupling unverified |
| Service brake | Low-spin latch, moving idle and stopped hold | Actual pin, flag persistence, derivative bypass and completed RPM |
| Full to half brake then release/gas | Persistent lock then wheel/RPM recovery | Actual set/hold/release gates; port force transient approximate |
| Handbrake then gas | Exact sequence equivalence; no new service pin | Separate half-axis producer/max consumer |
| Corner entry/hold, reversal and release | Exact sequence equivalence | Accepted recovered steering and isolated lateral-law witness |
| Throttle corner then traction release | Exact sequence equivalence | Broad combined-demand/traction feedback remains incomplete |
| Manual up/down | Exact sequence equivalence | Existing recovered gear/RPM rules; live shift transient unverified |
| Reverse brake then gas | Signed absolute-wheel pin and release | Original reverse slot and absolute-spin gates |
| Stopped gas/brake then release | Pin prevents driven-wheel acceleration while latched | Original zero derivative and completed idle clamp |
| Pause, catch-up and reset | Existing HUD cadence tests plus reset/latch regression | Port fixed-state semantics; live original cadence unverified |

Native comparison and producer commands, from the assigned workspace with
standalone Python and desktop toolchain on PATH:

```text
g++ -std=c++17 -O2 -ffp-contract=off -Iinclude tools/handling_brake_compare.cpp build/libnfsu_vehicle.a build/libnfsu_sha256.a -o build/brake-compare.exe
python -B tools/handling_brake_compare.py --root RETAIL --probe build/brake-compare.exe --out build/brake-comparison-fresh
python -B tools/handling_feedback_native.py --root RETAIL --out build/feedback-native-fresh
g++ -std=c++17 -O2 -ffp-contract=off -Iinclude -Ibuild/generated tools/handling_scenarios.cpp build/libnfsu_vehicle.a build/libnfsu_chase_camera.a build/libnfsu_sha256.a -o build/handling-scenarios.exe
```

Use fresh output directories and require successful exit, exact row count,
no stderr/error and fresh artifacts. Native reports record input identities,
boundaries, executed addresses, port source/probe hashes and manifest result.
Before/after CSVs are measurements at their source hashes, not new test pins.
Pin owners remain code/tests. Required complete native, Release, Switch and
ASCII receipts belong to the worker checkpoint/handoff at the tested head.

Hardware driving feel, original live sequences and complete owner RPM
acceptance are explicitly unverified. This is a bounded software correction
and research checkpoint, not completion of the whole handling assignment.
