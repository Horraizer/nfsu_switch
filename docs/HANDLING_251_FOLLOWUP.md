# Handling, tyre and RPM continuation

Issue #251 continuation from accepted baseline `743ebc0`, with #324 and
#250/#275 as explicit dependencies. The current WIP gameplay candidate
uses ordinary human selector1 engine, wheel, clutch and gearbox components.
The earlier universal selector3 service-brake pin is removed from VehicleModel;
its isolated primitive remains for the historical bounded witness. Physical
engine angular speed evolves independently of actor RPM publication. Complete
original driving parity and hardware feel remain open. Broad recording and
gameplay pin replacement remains held.

## Current gameplay integration and exact component comparisons

`src/player_powertrain.cpp` ports the common stock rear-drive component
chain with original binary32 torque units, authentic inertia and handling
fields. The wheel producer selects standing/sliding forces, common service
and state0 parking inputs, brake-capacity locking and ABS attenuation.
Five planar body/wheel constraint passes interleave with the coupled torque
correction; combined-force limiting and feedback finalization precede the
four wheel/engine/clutch derivatives and Euler updates. Wheel torque history
and sign, clutch phase, gear request, shift offsets and deterministic RNG
state persist. The original falling publication bound uses the completed
physical engine and unsplit frame interval; rising publication is direct.
DriveSession and RaceSession supply that interval after their solver slices.
The same outer publisher stores signed forward speed at biased owner+390
and speed magnitude at+394. The wheel's low-speed branch and gearbox's
differential synchronization consume these distinct cached fields on the
next solve; they are not recomputed from each intermediate body slice.
The initial port inference had the two domains reversed; isolated original
store execution and cached-field regression identify the correction.

Unequal-wheel cases also exposed a capacity-demand operand error. The push
at587585 shifts temporary stack offsets; the argument passed to587220 is
the longitudinal fraction of velocity-based friction demand, not a lateral
fraction divided by slip magnitude. The corrected producer retains the
original sampled rolling rear wheel under full service; failed comparisons
and fresh exact low/high magnitude comparisons remain in the checkpoint.

`tools/handling_player_native.py` executes actual full478570 and587830,
full gearbox construction/update and the six-component original kernel.
Its engine/gear bytes and RNG state are restored per case, with separate
pre-kernel and post-update wheel-equivalent speed fields. Owner magnitude
speed, lateral velocity and initial torque history are explicit inputs.
Geometry, longitudinal velocity, load, surface, clock and environment remain
supplied. No external replacement hooks are allowed. These are component
fixtures with fixed body input, not original driving trajectories.

`tools/handling_player_coupled_compare.py` builds a fresh portable bridge
and requires exact forces, torque, spin, engine feedback/acceleration,
physical/publication RPM and completed clutch/gear/RNG state. Reports retain
native input-report and source/binary hashes. Fresh straight, lateral and
nonzero torque-history receipts are recorded in the worker checkpoint.
`handling_player_engine_compare.py` separately authenticates every encoded
stock handling field byte against the retail record.

The lateral fixture exposed a concrete units bug:587407 converts the
16-bit angle to radians before comparing it directly with tyre setup+0.
Comparing degrees instead prematurely selected sliding on the driven axle.
The correction uses the original564CD0/564DB0 lookup and564B10 polynomial,
kept separate from body/camera trigonometry. ASCII lookup data is regenerated
by `handling_player_angles_generate.py` from the authenticated executable;
`handling_player_angle_compare.py` verifies signed/tiny vector cases and
the entire16-bit sine domain. The standing force uses authentic587740
operation order and load-curve family, including its separate conversion
to degrees. Planar yaw accumulation also preserves source add/subtract
order; grouping each cross product first changed coupled binary32 bits.

`nfsu_player_powertrain_tests` covers actor/physical isolation, unsplit falling
publication, reset and refusal preservation, torque-history/request telemetry,
multi-step clutch phases, the selector1 full-service distinction and the
source radians branch. The canonical telemetry appends persistent mechanics
to the original prefix; future-distinguishing states have different bytes.
The old vehicle tests still require attribution and correction where they
assume selector3 locking, actor RPM equal to clamped wheel RPM or instantaneous
gear changes. Their failures are preserved, not reported as a passing gate.

Remaining approximations include the3D body/contact/suspension adapter,
world-to-wheel heading/terrain construction, imposed moving-pose reset,
port subdivision cadence, simulation clock and per-vehicle fixture RNG.
Stock ordinary paths do not establish alternate descriptor assists, damage,
nitrous or simultaneous high-speed gas/service special input branches.
Full required gates, independent gameplay review and deferred hardware
acceptance remain open.

## Positive model selection and conflicting producer assumptions

`tools/handling_modes_native.py` executes actual `0x00425150` with explicit
role/config/global combinations. Role1 with config+0x734 and globals
0x736208/0x78A344/0x7361A8 cleared selects physics1. Roles2/3 under those
same cleared gates select physics3; the last two globals force physics1.
Its role proof executes the player caller0x4B6102..0x4B611B, whose literal
role1 argument is copied by config constructor0x5809CB..0x5809E8 into+4.
The actual actor config-pointer store0x425582 is followed by role consumer
0x422ACA..0x422AE9, selecting controller1. The actor-factory branch
0x42253A..0x422544 requests physics1 directly for controller1. Thus ordinary
player creation bypasses425150; the common helper agrees when called with
the same role/config. The role3/controller3 factory branch is the one that
calls425150 before426110. RideInfo construction and the full actor factory
are outside this sliced witness.
This agrees with the human1/rival3 factory evidence in
`PHYSICS_DISPATCH_256.md`, `EVENT0_ACTORS_258.md` and `CONTROLLER_293.md`.
The earlier issue324 comment8799 cleared-override mode3 statement does not
specify the role fixture and cannot identify the ordinary human force model.
The source `0x0042E230` conditionally switches both controller and physics
to3. Its direct caller0x42D877 lies in the view-state4 branch of42D520;
the comparison42D837/83A excludes other view states. The actual phase
selector42D470 chooses state2 when view+0x4AE/4AF are cleared in the
tested active phases, state4 when4AE is set alone, and state3 when4AF is
set. `handling_modes_native.py` executes this phase selector and branch
test. The meanings/producers of these view flags, the complete state
handler and live mode frequencies remain unverified. This conditional
edge does not erase the ordinary role1 creation proof.

Selector1 creates separate wheels through `0x00476A0C -> 0x005868E0`,
with vtable0x6B9544 and derivative0x587170. Its actual brake inputs are
written at0x47792A..0x477953 (common service branch) and
0x477953..0x4779F8 (state0 handbrake branch). The witness executes these
with stock setup and then the actual lock writer0x587220..0x5872A4.
That writer compares brake capacity against absolute spin and requested
force magnitude. It does not implement the selector3 threshold below.
Synthetic front spin26rad/s remains unlatched under full service/nohand,
even at requested force0, while the selector3 candidate pins it. Rear
assist, simultaneous gas/brake special branches and complete dynamic tyre
force production remain outside this witness.

The actual selector1 export0x478FBB..0x479050 also differs. It converts
completed engine angular speed at physics+0x48 -> engine+0x14 to RPM, then
limits a fall using supplied physics dt and -2.5/engine-inertia. The target
is the biased actor+0x40 pointer, so its +0x3A0 is actor+0x3E0. An increase
is direct on this branch. This is a separate source producer from427A30's
50RPM speed-synthesis helper; it does not justify reintroducing that helper
as an actual-engine player filter. Stock engine inertia is read from the
authenticated record, and the witness reports the actual native outputs.
Complete wheel/engine/gearbox coupling and actual caller cadence remain open.
The witness executes physics constructor pointer stores0x476898..0x4768AE:
physics+0x5C is the actual actor, +0x54 its physical setup, +0x58 actor+0x40.
It also executes engine constructor prefix0x46C7C0..0x46C8DF, establishing
engine+0x18 from stock physical setup+0x1F0. The subsequent angular-speed
values are explicitly synthetic completed-state inputs, not values generated
by an original integrated engine. Unbiased actor+0x3A0 canaries stay intact.
The engine constructor owner argument is the biased actor+0x40, stored at
engine+0x1C. The later witness corrects the earlier unused synthetic argument
in the RPM-only prefix fixture and asserts this identity as well.

## Selector1 engine and clutch witness

`tools/handling_engine_native.py` extends that evidence through the full
engine constructor46C7C0, including actual maximum-torque helper46C6D0.
The stock engine and transmission blocks are read through physical
setup+0x1F0/+0x1B0. Actual gearbox constructor pointer/state stores at
46D3B4..46D41D bind the engine, four wheels and biased actor owner; table
and random shift initialization after that prefix are omitted.

The actual wheel-equivalent engine speed helper46D680 uses driven-wheel
spins, the retail differential ratios and selected gear. Clutch pass46E270
distinguishes gearbox states: states0/2 transmit a bounded angular-speed
difference through retail clutch stiffness and declared clutch fraction;
state1 transmits nothing in this pass; state3 transmits actual crank torque.
Distributor46E020 subtracts the transmitted torque from engine+0x7C and
adds gear/differential-weighted torque to unlocked wheels at+0x108. Neutral
gear bypasses distribution. Both engine and wheel outputs are recorded.

Engine derivative46CF80 consumes original crank torque46CFF0 plus feedback
engine+0x7C, divided by actual engine inertia. The tool executes the original
46AA40 Euler kernel and46ABD0 update walker on a synthetic engine-only
component list. Actual update46CA90 clamps angular speed to idle/max and
clears consumed feedback, then the actual biased-actor export478FBB is run.
The witness requires distinct physical/exported RPM during a slipping
clutch fall, zero transmission in neutral/decoupled cases, crank balance
in the isolated state3 pass, finite results and feedback consumption.

The construction/reset RPM initializer478365..478394 supplies starting
engine speed. Its direct callers476EE6 and47CA1C are constructor/reset
paths; this witness does not repeatedly copy exported RPM into the engine.
Each matrix case resets its declared state independently. The helper's x87
return is captured with a documented synthetic fstp/ret trampoline, without
replacing its arithmetic. All other executed methods are original mapped
bytes and external hooks are forbidden.

This is a bounded engine/clutch witness, not a complete coupled vehicle:
wheel speeds are held fixed; gear, clutch fraction and gearbox state are
declared; the complete gearbox update46DB60, coupled force solve46E360,
wheel derivatives, contacts and body integration are absent. Time is zero
for the unrelated timed engine adaptation branch. Boost/damage are disabled.
The supplied single-kernel interval is a synthetic input, not evidence of
live cadence. `PHYSICS_DISPATCH_256.md` describes the original subdivision
and unsplit publication interval; a gameplay implementation must preserve
that distinction and complete the missing coupled bridge before acceptance.

`tools/handling_coupled_native.py` adds the authentic four wheel constructors
5868E0, wheel-inertia stores47721B..47723E, actual wheel derivative587170
and component update586DA0 to the synthetic component list. It executes
the full tyre/brake producer587330, including its internal587220 lock
call, then the original coupled torque solver46E360. The source caller
478570 starts its iteration loop with five passes at4785E1; the witness
executes that solver count while holding the declared body/contact data.
It then runs combined-demand limit587830 through587A90, stopping before
the separate force-coordinate/body-application part, and integrates the
engine and all wheel states with the original kernel and updates. The
de534ac version used a five-component list and a partial gearbox constructor.
The later revision adds full gearbox constructor46D390, original clutch
derivative46DFA0 and full gearbox update46DB60 as a sixth component.

Wheel headings are identity matrices, world velocity/load/surface are
explicit synthetic inputs, and retail wheel/tyre/brake blocks and global
tables supply the arithmetic. The scalar matrix implementation is selected
as in the selector3 witness. No force or transcendental helper is replaced,
and external hooks are forbidden. The report captures tyre forces and slip
before component updates clear them, plus completed wheel and physical/
exported RPM states. It requires finite wheel results, actual nonzero tyre
reaction, no unexpected lateral force on this straight fixture, distinct
clutch states and the front-wheel no-pin witness under full service input.

This narrows the previous missing coupled solve and tyre-producer boundary.
It still does not execute the entire478570 constraint iteration body,
derive contact velocity/load from an integrated rigid body, apply the later
wheel/body force transforms, execute the complete gearbox update or establish
live scheduling. The later six-component revision does execute the complete
gearbox update; body/constraint/cadence limits remain. It cannot validate
a complete car model or justify dropping
the isolated engine-only boundary above. A fixed body velocity can accelerate
a braking wheel toward its rolling speed; that is a fixture consequence,
not a measured whole-car acceleration under braking.

Actual factory classification476CA3..476CFB chooses the stock rear-drive
descriptor6F1098 before calling the gearbox constructor. Earlier partial
constructor fixtures supplied synthetic6F102C, which was unused by their
executed arithmetic. The corrected isolated-engine witness and a
descriptor-only corrected copy of the de534ac coupled witness preserve all
numeric row fields exactly; the worker checkpoint names the fresh receipt
pairs and comparison. This comparison excludes the later full gearbox
update extension, whose dynamic state is additional evidence.

The full constructor requires a declared synthetic SEH page, seeded random
context and game-state4. The fixture restores all constructor gearbox bytes
between cases before supplying same current/requested gear, clutch fraction
and state; inherited shift flags are not silently carried between cases.
The original shift-table constructor and random helper execute, but the
environment is explicitly synthetic. Changes in clutch engagement and
retained fully coupled state are asserted as well as engine/wheel outputs.

## Original execution boundary

The input identities are the `speed.exe`, `GLOBALB.BUN` and stock 240SX
record identities already recorded in `HANDLING_GAPS.md`. The native tools
authenticate them and bracket execution with complete retail content and
metadata manifests. They use the tracked `world_body_trace.Image` PE mapper
and `physics256_native.Machine`. No retail process is launched or modified.
Synthetic x87 control word `0x007F` does not prove the live environment.

`tools/handling_brake_compare.py` executes the selector3 completed engine
update `0x004757D0..0x004759FE` with synthetic wheel spins, original stock
setup, first/second/reverse gear and prior latch flags. Its strut call is an
explicit logged stub. It compares the original wheel pin with the actual
port helper through `handling_brake_compare.cpp`, including both sides and
equality of the binary32 gate thresholds. The original locked-wheel
derivative consumer is executed separately without substituting torque or
feedback calculations: locked wheels take its zero-output path.

Recovered selector3 chain:

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

The WIP port evaluates the selector3 gates on float copies of its double wheel
state/input. It preserves the latch for subsequent forces and applies the
pin after integration and before completed engine RPM. Reset clears it.
The tyre force calculation still uses the existing approximation when a
wheel is locked; the latch alone does not recover that force calculation.

`VehicleWheelTelemetry::brake_locked` is part of canonical record identity.
The existing wheel contact u32 is now a flags word: bit0 contact, bit1 brake
latch. Size and unlatched bytes stay identical. Two equal present wheel
spins with different latches encode differently and produce different RPM
on the next partial-brake/throttle step. This is tested explicitly.
The record has no deserializer in this repository. `DriveSession` and
`VehicleTrack` embed the canonical bytes, and replay/probe tools compare
opaque bytes/hashes. Runtime contact users read the separate bool field.
The encoding regression checks lock-only word2, contact-only word1 and
combined word3 independently, plus every byte of an unlatched record.

The old `VEHICLE.md` sentence saying the pin is absent describes the
accepted baseline. This WIP implements that selector3 pin; its default
placement and changed moving-idle regressions are not yet justified for
the human model. The selector3 gate depends on wheel spin, not body speed:
tyre slip means
there is no universal body-speed threshold for its idle transition.
Near-full service brake can show idle while the body is moving in selector3.
Its half-axis handbrake cannot activate this pin by itself. Neither result
is a universal claim about selector1 or original live player behavior.

## Feedback producer found; integration remains open

The previous `VEHICLE.md`/`HANDLING_GAPS.md` unknown selector3 writer of
`engine+0x74+8k` now has a bounded source witness. Engine vtable `0x006C847C`
virtual `+0x14` points to `0x00475D00..0x0047623E`; stores
`0x004761BC/0x004761C2` populate `+0x74/+0x78` for each wheel. The angular
consumer at `0x0047576B..0x00475781` reads the first component before its
division by the original inertia. `0x00474D18` invokes the producer.

`tools/handling_feedback_native.py` executes the selector3 producer and original
matrix/trig/force helpers without external hooks. Its assumptions are
explicit: stock geometry, synthetic flat support/load, identity body
rotation, zero yaw/steering, no boost and fixed synthetic wheel/body speeds.
It binds matrix dispatch `0x006D4AC4` to original scalar `0x005ABDB5`;
the live CPU-selected implementation is unverified. Declared helper ranges
contain unused padding; the report lists every executed instruction address.

This identifies and executes the producer, but does not establish full
car/mode/contact/surface/device gates, force application across heading and
terrain, actual live wheel-engine transients or original scheduler cadence.
The current component integration above supersedes the earlier incomplete
selector3 producer assumptions; the declared body/live boundaries remain.
No guessed grip multiplier or needle follower is introduced.

`tools/handling_feedback_chain.py` extends the selector3 witness through
actual torque-curve constructor initialization, unlocked/locked angular
derivative, original46AA40 integration kernel and completed pin/RPM update.
Its one-engine circular component list and system update callback are
synthetic; the callback uses the original46ABD0 component-update walker.
Body velocity/load are held fixed, struts are explicitly stubbed, scalar
matrix dispatch is selected and no body/contact integration is claimed.
It demonstrates zero derivative while held and one completed-pass release
delay. It does not recover a live player runtime bridge.

## Scenario matrix and reproduction

`tools/handling_scenarios.cpp` uses actual VehicleModel and recovered1
steering on synthetic flat ground. Its context mirrors the existing
`recovered_matrix.cpp`; direct scripted input excludes live pad filtering
and display timing. Every step records speed, yaw, lateral speed, completed
engine/wheel RPM, gear, wheel spins and rear force/sliding state.

| Scenario | Port before/after evidence | Original evidence boundary |
| --- | --- | --- |
| Acceleration then coast | Exploratory current gameplay CSV | Authentic engine/clutch primitives; original trajectory unverified |
| Service brake | Exploratory current gameplay CSV | Selector1 capacity/ABS and publication fixtures; body trajectory unverified |
| Full to half brake then release/gas | Exploratory current gameplay CSV | Mechanical history/phase primitives; original transient unverified |
| Handbrake then gas | Exploratory current gameplay CSV | Separate state0 rear parking path; original trajectory unverified |
| Corner entry/hold, reversal and release | Exploratory current gameplay CSV | Accepted steering plus original lateral component fixtures; heading/body trajectory unverified |
| Throttle corner then traction release | Exploratory current gameplay CSV | Coupled constraints and friction limit fixtures; trajectory unverified |
| Manual up/down | Exploratory current gameplay CSV | Clutch/gear phases and RNG; complete request cycle under original driver unverified |
| Reverse brake then gas | Exploratory current gameplay CSV | Signed mechanics; original reverse trajectory unverified |
| Stopped gas/brake then release | Exploratory current gameplay CSV | Ordinary component fixtures; stop/combined input acceptance remains open |
| Pause, catch-up and reset | Targeted physical/publication/reset tests | Port frame semantics; live original cadence unverified |

Native comparison and producer commands, from the assigned workspace with
standalone Python and desktop toolchain on PATH:

```text
g++ -std=c++17 -O2 -ffp-contract=off -Iinclude tools/handling_brake_compare.cpp build/libnfsu_vehicle.a build/libnfsu_sha256.a -o build/brake-compare.exe
python -B tools/handling_brake_compare.py --root RETAIL --probe build/brake-compare.exe --out build/brake-comparison-fresh
python -B tools/handling_feedback_native.py --root RETAIL --out build/feedback-native-fresh
python -B tools/handling_feedback_chain.py --root RETAIL --out build/feedback-chain-fresh
python -B tools/handling_modes_native.py --root RETAIL --out build/handling-modes-fresh
python -B tools/handling_engine_native.py --root RETAIL --out build/handling-engine-fresh
python -B tools/handling_coupled_native.py --root RETAIL --out build/handling-coupled-fresh
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
