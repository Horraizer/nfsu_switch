# Handling gaps: why the demo feels heavy

Issue #248, research at `9855277` (2026-09-30). The strongest explanation is
too little turning authority: the invented speed/lock curve and front/rear
tyre balance compound each other. The car responds fairly quickly to a new
stick input, but settles into a large-radius turn. There is also a definite
expectation mismatch: the red tuned 240SX has stock physics. D-pad filtering,
stick reversals, frame delivery and the chase camera can add perceived lag.
Simply reducing mass or yaw inertia is not supported by the evidence.

This change adds research tools and this report only. It changes no runtime
default, approximation, retail numeric table or test pin. Replay results
remain owned by `RaceRun::matches_event1_replay`, `tools/race_replay_check.py`
and their CTest wrappers; their hashes, step counts and finish times are not
duplicated here. All measurements below are historical observations at the
SHA above, not new acceptance limits.

## Evidence and limits

RECOVERED means instructions or named bytes were read in this session;
APPROXIMATION means a project rule; INFERENCE means an interpretation whose
remaining assumptions are stated. The ranking is judgement about the owner's
reported feel, not a measured difference from a retail controller recording.

| Read-only source | SHA-256 |
| --- | --- |
| `speed.exe` | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |

The 240SX is record 18 at file offset 1736752. Its whole-record SHA-256 is
`212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B`.
`tools/handling_trace.py` verifies those identities and the PE section mapping,
then disassembles bounded instruction ranges with Capstone. It prints table
addresses, values and byte hashes to ignored output; it embeds no new retail
table. Only `GLOBALB.BUN` supplies handling data.

Every retail measurement and trace is bracketed by manifests of every file
and directory (relative name, size/type, modification time in nanoseconds),
compared with `cmp`. These detect writes/touches, not an adversarial content
replacement with restored metadata. All outputs are outside the installation.
The original executable was read, not launched. No original-game 0-100,
braking, cornering or step-response capture was obtained in this session.
Consequently none of the demo telemetry below is advertised as retail parity.

Published evidence does not fill that hole. [EerieGoesD's NFSU car chart](https://gamefaqs.gamespot.com/ps2/914761-need-for-speed-underground/faqs/81958)
(version dated 2025-06-14, consulted 2026-09-30) describes extracting fully
tuned menu performance floats. Those are ratings, not elapsed seconds,
physical speeds, lateral acceleration or yaw telemetry. Its stock figures
come from a wiki. It is useful evidence that stock/tuned ratings differ, but
is not a trustworthy timed baseline. Real 240SX road tests, Underground 2,
NFS World and the Global VR arcade adaptation are not measurements of this
PC executable. No sufficiently controlled published numeric baseline was
found in the searches for NFSU 240SX acceleration, top speed and steering.

## Ranked causes and decisions

Ranks describe expected impact on normal live driving; conditions can change
their order. Confidence in a code gap is separate from confidence that it
explains this particular drive. The owner supplied no recording of that drive.

| Rank | Cause and evidence | Expected feel in the likely wrong direction | Retail rule/address | Proposed fix and risk |
| --- | --- | --- | --- | --- |
| 1, high | Speed-dependent steering authority. `VehicleModel::step` invents a rational lock curve and gives both front wheels one angle. At target 25 m/s and input 0.3 the measured radius is about 190 m. | Increasing speed makes a moderate stick deflection seem ineffectual, even when yaw begins promptly. Too little lock forces early, wide turns. | Speed tables, input-history and slip-angle branches at `0x00460930..0x00460E63`; separate-wheel geometry at `0x00588240`. See trace below. | Recover the complete target/rate pipeline with mode gates, then evaluate it behind a default-off experiment. Do not drop a bare table into the model: more lock reaches the current tyres' known snap-oversteer region. High coupled risk. |
| 2, high | Lateral tyre curve and deliberately doubled rear/front linear stiffness. Decoded slip/load/lateral scales are unused. The controlled stiffness experiment raises yaw materially without changing the lock. | Strong understeer and little rotation; the driver adds lock until the eventual transition is abrupt. Peak friction alone cannot correct the shape or load sensitivity. | `0x005876F6..0x0058781F` selects/interpolates a family of load curves; `0x00587830` applies a combined-demand limit and more multipliers. | Recover curve family, argument units, static/sliding transition and coupling together. Preserve source-byte rebuild tests for any embedded tables. High risk of power/lift/brake oversteer. |
| 3, certain gap; high expectation impact | Tuned race is simulated as stock. `RaceSession` always constructs `stock_240sx_vehicle_spec()`; `--tuned` changes scene/skin requests. Stock/tuned state and trajectory files compared identical. | A visually modified car has no upgrade torque, gear ratios, weight, suspension or tyre improvements; launch and exit feel weaker than an actually upgraded original. This does not alone explain poor stock steering. | `0x00575960` copies setup and applies seven upgrade stages; #57 already implements them. | Introduce explicit performance levels independent of cosmetic parts, apply once before constructing the model, rebuild shifts. Medium risk for stock zero-level path; high for upgraded tyres on current approximations. Never infer level 3 from red paint/bodywork. |
| 4, high for D-pad/reversals; unmeasured display contribution | `DriveControls` filters digital outward motion and all returns/reversals. Switch polls once per rendered frame and reuses that sample for catch-up ticks. | A D-pad press takes time to reach useful steering. Even a stick reversal pauses on its way through centre. Long frames add input/display age. | Retail rate path `0x00460DE0` is time/history/speed dependent; byte handler alone (`0x00461840`) is not the whole steering system. Backend deadzones and mode selection remain unknown. | Instrument raw axis, filtered steer, road angle, yaw and sample/present times together; replace filtering only after target/rate reconstruction. Avoid two independent steering smoothers. Medium handling risk, low telemetry risk. |
| 5, medium/high near saturation | Engine/wheel coupling and invented ideal traction limiter; altered engine braking. The limiter uses unsaturated lateral demand and can remove all driven-wheel torque. | Pressing gas in a tight turn may not accelerate; lift-off rotation and wheelspin are suppressed. Braking/downshifting can feel sticky. In the measured steady baseline cases it never cut torque, so it is not their cause. | `0x00475530..0x004757C7` has a different speed-dependent feedback/brake rule; writer/meaning of feedback term still unresolved. | Trace feedback storage and clutch/boost multipliers before replacing the limiter. High risk: removing it alone restores spins covered by vehicle tests. |
| 6, medium; sign argues against first fix | Yaw inertia selection and angular stabilisation are incomplete. Demo uses plain inertia; retail has a scaled alternative plus conditional angular damping. | Extra inertia would delay turn-in, not fix it. Missing damping/helpers can require compensating understeer elsewhere. Wrong stabilisation can either feel wooden or spin-prone. | Selector writers `0x00474AD9`, `0x00474B00`, `0x00474C79`; inverse build `0x00584B10`; angular damping `0x00474DD0`. | Recover contact/mode gates and torque application before balancing tyres. Do not blindly halve inertia or always enable the alternate. Medium/high risk. |
| 7, medium on transitions/bumps | Force application height, static preload, strut direction, symmetric anti-roll and bump stop are approximations. | Too little transfer makes braking/turning flat and unrewarding; too much makes tyres unload or car roll. Missing speed preload changes high-speed grip. | Strut/preload rules `0x00475460`, `0x00475A00`; force application and preload argument chain incomplete. | Reproduce contact forces and support on slopes; isolate from tyre tuning. High rollover/contact risk. |
| 8, medium under braking | Invented service/handbrake torque and wheel inertia. Straight stopping is strong; no measured evidence that weak brakes cause the iron feel. | Incorrect bias dulls trail-brake rotation or locks the rear; excessive damping/engine brake can make release feel heavy. | Partial spin-dependent brake term in `0x00475667..0x00475797`; input rules in #59. | Trace brake distribution and wheel dynamics, then trail-brake/handbrake tests. Medium/high risk. |
| 9, medium visual; not a physics change | Chase camera has recovered heading lag but fixed lag factor, fixed distance, omitted roll and inferred widescreen FOV. | Following velocity and smoothing heading can hide early body rotation; wrong distance/FOV changes speed impression. | `0x00489300`, `0x0048A389..0x0048A3F0`, #59. | Compare locked/world/body/velocity views using the same state stream before retuning physics. Low physics risk. |
| 10, low in slow corners, medium at speed | Drag integration, drafting factor, downforce, rolling resistance and surface friction coverage are incomplete. | Excess drag reduces acceleration/top speed; absent aero reduces fast-corner grip; universal asphalt removes surface character. | Drag predictor `0x0045687A`; aero preload `0x004745E6`; per-surface limit `0x005878A2`. | Trace force pass and surface material mapping after steering/tyres. Medium risk; not an explanation for slow-speed input lag. |

## What the race actually feeds into the model

On Switch, `sample` in `switch/race_main.cpp` reads libnx button bits and raw
stick axes. ZR/ZL are digital gas/brake. `padUpdate` happens once per outer
frame; the catch-up loop samples that same `PadState` for up to eight 60 Hz
ticks, retaining ordinary frame debt. A long suspend pauses explicitly.
`RaceSession::advance` calls `DriveControls::update` once, then `step` twice
and the vehicle twice, and updates the camera once. Countdown holds the pose.
Desktop live input uses `RaceWindow`: W/S become gas/brake buttons, but A/D
become full-scale horizontal **axis** values, so they take the analog outward
path rather than the Switch D-pad ramp. Desktop keyboard and Switch D-pad
feel therefore cannot be treated as equivalent tests. Replay supplies
recorded `GamepadFrame`s on the same downstream path. The route driver is
only a recording generator, not an
assist on live inputs. No path-following force drives the live car.

`DriveControls` reads each axis independently, not through `InputMapper`'s
radial deadzone. Its axial calibration, source arbitration, sign inversion,
filter and reverse logic are in `src/drive_controls.cpp`. A steering button
wins while its sum is nonzero; otherwise the stick wins. The model then
applies its speed lock and road-angle slew. There is no additional camera
feedback into physics. The apparent lag from camera/rendering is separate
from the measured simulation response.

At the historical SHA, the default calibration suppresses roughly the first
9% of stick travel. A quarter-scale raw horizontal deflection becomes about
0.192 input. Analog outward steer passes through, but analog inward motion
and reversals do not. At a held speed of 25 m/s the input-only harness measured:

| Source | Centre to full input | Full right to full left |
| --- | ---: | ---: |
| Stick | 0.00833 s | 0.14167 s |
| D-pad | 0.45000 s | 0.58333 s |

These are discrete control-step timings, excluding sensor, rendering and
road-wheel/body response. They are not Switch latency measurements. A held
button at controller connection is latched out until released, disconnect
contributes neutral, and edges fire once. Those project policies can affect
the first input or reconnect, but cannot explain sustained sluggishness.

## Telemetry, with comparison boundaries

At `9855277`, run here with the default desktop libraries, flat asphalt,
stock 240SX, automatic gearbox, no nitrous:

| Manoeuvre | Measured demo result | Original comparison that can be made |
| --- | --- | --- |
| Probe `accelerate`, full throttle after initial rest | First printed sample at/above 100 km/h: 6.0833 s after gas begins | Torque curve, ratios and shift search are recovered. No original elapsed-time run; cannot claim an acceleration error percentage. |
| Probe `top-speed`, 120 s full throttle | 232.48 km/h at end; last ten seconds 232.40..232.48 km/h | Near equilibrium, not proof of the mathematical asymptote. Retail drag coefficient is known, force application/coupling incomplete; no measured retail top speed. |
| Probe `brake`, initial 100 km/h | 2.2833 s and 31.429 m to first printed speed within 0.1 km/h of zero | Includes automatic engine braking. Not an exact zero-velocity crossing or an original braking result. |
| Probe step steer, initial 15 m/s, throttle 0.4, input 0.3 | Yaw 0.1645, 0.2495, 0.2628 rad/s at 0.10, 0.25, 0.50 s; speeds 54.19, 54.54, 55.12 km/h | Free acceleration changes the lock/target during this transient. No matching original response trace. |
| Probe long corner, initial 20 m/s, throttle 0.5, fixed input 0.2 | At 50..60 s speed still climbs 139.82..142.55 km/h; mean yaw 0.035104 rad/s | This is not steady-state cornering. A constant throttle is insufficient to hold speed; use the controlled runs below. |

The probe prints speed to 0.01 km/h, yaw/angle to 0.0001 rad and positions to
millimetres; threshold timings resolve only one physics step. These rounding
and threshold choices explain why stopping numbers can differ slightly from
tests using a tighter threshold. The script retains full per-step console
logs and the precise command inputs for inspection.

`tools/handling_response.cpp` uses the existing model, the same bounded
throttle controller as `tests/vehicle_tests.cpp::run_steady`, and fixed steer
for 20 seconds. It never overwrites velocity to fake constant speed. Actual
speed therefore differs slightly from its target. At the same SHA:

| Target speed | Actual speed | Input | Wheel angle | Yaw rate | Radius | Lateral acceleration | First 90% of final yaw |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 15 m/s | 15.3383 m/s | 0.3 | 0.062648 rad | 0.262833 rad/s | 58.36 m | 0.4114 g | 0.2167 s |
| 25 m/s | 25.2918 m/s | 0.3 | 0.030319 rad | 0.133277 rad/s | 189.79 m | 0.3440 g | 0.1500 s |
| 35 m/s | 35.2080 m/s | 0.3 | 0.017173 rad | 0.067931 rad/s | 518.34 m | 0.2441 g | 0.1083 s |
| 15 m/s | 15.1627 m/s | 1.0 | 0.211818 rad | 0.866401 rad/s | 17.50 m | 1.3406 g | 0.2583 s |
| 25 m/s | 25.0946 m/s | 1.0 | 0.102360 rad | 0.440214 rad/s | 57.04 m | 1.1279 g | 0.1667 s |
| 35 m/s | 35.0685 m/s | 1.0 | 0.057650 rad | 0.225226 rad/s | 155.79 m | 0.8064 g | 0.1083 s |

Last-second yaw ranges were below 1e-12 rad/s for those baseline rows, with
no tyre sliding or drive-torque cuts. Radius is horizontal speed/yaw;
acceleration is horizontal speed*yaw divided by model gravity. A small t90
at high speed means reaching a small final response quickly; it does not
mean the car turns tightly. No comparable original skidpad/yaw measurement
was obtained.

Single-factor experiments at target 25 m/s, input 0.3, same controller:

| Scratch-only change | Yaw rad/s | t90 s | What this isolates |
| --- | ---: | ---: | --- |
| Baseline | 0.133277 | 0.1500 | Reference |
| Steering reference speed changed to 13 m/s | 0.151536 | 0.1500 | About 14% more yaw from lock alone |
| Front/rear stiffness changed to 12/18 | 0.178274 | 0.2000 | About 34% more yaw at almost the same road angle |
| Yaw inertia doubled | 0.133277 | 0.3500 | Alters transient strongly, leaves steady yaw unchanged |
| Tyre force lift changed to 0.8 | 0.134361 | 0.1500 | Under 1% yaw change in this regime |
| Traction limiter disabled | 0.133277 | 0.1500 | No effect where no torque was being cut |

These are sensitivity experiments, not proposed replacement defaults. The
12/18 experiment at target 15 m/s and full steer cannot maintain target speed:
it settles near 12.83 m/s with sustained sliding/torque cuts. Do not generalise
the moderate-input gain to full-lock safety or call that case a successful
constant-speed test. The existing spin/snap-oversteer tests remain relevant.

The event-1 replay was also run twice, stock and `--tuned`, with fresh output
directories. `cmp` found both `states.txt` and `trajectory.csv` byte-identical.
At the historical SHA, mean horizontal speed across the emitted physics
samples was 29.7344 km/h, maximum 40.3495 km/h. There were no body-contact
samples; 492 samples had fewer than four road wheels. This is a gentle route
regression, not a high-speed handling validation or a reproduction of today's
live drive. Do not use its success to reject the owner's observation.

## Retail trace: the five priority chains

### 1. Steering, beyond the input-byte handler

RECOVERED: `0x00461692` reads control +0x28C and `0x004616A0` calls
`0x00460930` with state at control +0x1F0; the returned word goes to +0x1D0.
`0x00460930` updates input history and calls `0x00460970`. That routine reads
the previous angle, calls `0x00460A80` for the angle limit, multiplies by raw
input and setup +0x270, then clamps around the previous angle using the
result of `0x00460DE0` times dt. A flag at `0x00736514` bypasses the slew
branch. Small resulting angles are zeroed; conversion back to angle units
is at `0x00460A47..0x00460A6B`. This is not the demo's rational lock rule.

The speed argument is traceable, not guessed from the table's axis. At
`0x00426224..0x00426230`, the physics constructor receives car and car+0x40.
`0x00474127` and `0x00474135` keep them separately. In `0x00474730`, physics
+0x30 is that car+0x40 subobject; `0x0047480D..0x00474859` projects velocity
onto the body forward basis and writes subobject+0x390, hence car+0x3D0.
That is the signed forward speed in the model's inferred metres/seconds
units. `0x00460A8C` reads this exact car field for the steering curves.

The main flag-clear branch uses uniform-curve descriptor `0x00705F48`, with
samples at `0x00700178`, over a forward-speed domain. The alternate uses
`0x00705F5C` / `0x007001A0`. The main branch also uses speed response
`0x00705F84`, throttle/brake inputs (`0x00460AAF..0x00460B27`), and a curve
of absolute filtered input (`0x00705F70`, `0x00460B2B..0x00460B38`).
`0x004609C4`'s setup +0x270 is record +0x380, **not** raw record +0x270
(the yaw-inertia scalar). The traced stock record's +0x380 is unity.

As a static-data comparison at this SHA/source identity: the main base
table alone evaluates to approximately 26.44, 18.125 and 11.375 degrees at
15, 25 and 35 m/s. The demo full-lock formula at those exact speeds gives
approximately 12.30, 5.90 and 3.31 degrees. The table samples' SHA-256 is
printed by the trace tool. These are base-table outputs, **not predictions
of final original road-wheel angles**: mode, input history, throttle/brake
factors, rate limiting and the following assist branch still act on them.
They support investigating lost steering authority, not substituting one
number for the complete pipeline.

RECOVERED assist evidence: `0x00460C24..0x00460D72` calls `0x00464ED0`, which
subtracts a velocity-vector angle from a stored heading word, wraps it and
returns radians. The caller converts to degrees and gates a state byte
using angle and speed thresholds (`0x0070013C`, `0x00700140`,
`0x00700148`, `0x0070014C`). It tests input/angle sign, evaluates one of two
input curves and changes the steering limit using the angle magnitude.
Calling this a sideslip/countersteer helper is an INFERENCE from that dataflow;
the branch itself is recovered. Its exact mode/device eligibility and
history-buffer semantics need completion before implementation.

`0x00460DE0` uses the change in raw input divided by dt, filtered history,
speed and absolute filtered input to calculate slew rate. It is neither a
constant road-wheel rate nor the demo's D-pad formula. `0x00588240` computes
separate left/right steering transforms from wheelbase/track separation
and an input angle. The `TwoWheelAckermanSteering` name is referenced by
its serializer at `0x005881C6`; vtable slot `0x006C834C` names the transform
routine. The complete dispatch from the player path to this class was not
established; its presence alone is not proof every mode uses it.

### 2. Tyre curve and grip helpers

RECOVERED: `0x00587713` calls the lateral evaluator with a tyre pointer,
scaled load and absolute angle. With the on-disk flag at `0x006F0868` set,
`0x00587775..0x0058781F` computes curve position as angle-in-degrees divided
by tyre +0x00, times 6. It scales load by `0x006F0870` / tyre +0x04, evaluates
adjacent load curves through pointers at `0x006F084C`, interpolates between
them, and scales by tyre +0x08 and `0x006F086C`. At index 6 or above it uses
the last curve. Flag clear uses another curve at `0x007043D8` and load.
This is substantially different from linear tan(slip)*load with the demo's
chosen axle stiffnesses. Decoding the three tyre scalars is not using them.

RECOVERED: `0x005878A2..0x005878D5` builds the static force limit from surface
table +4 (stride 0x94), tyre +0x0C, load at tyre +0x158, and multipliers at
tyre +0x178/+0x17C/+0x180 plus a global. Mode +0x3C0 == 1 adds another
factor through `0x005872B0`. `0x0058783F..0x0058786D` conditionally scales
one requested-force component before computing combined demand. These are
specific NFSU grip modifications; their producers/eligibility are not all
recovered. Do not name them universal traction control or blindly set them
to one. The dynamic/sliding branch and force application remain unfinished.

### 3. Upgrade application

Re-traced `0x00575960..0x00575A02`: copy setup, suspension, tyres, aero,
grip, engines/induction/items/nitrous, gearbox, then weight/inertia. The
inertia rebuild at `0x005762FC..0x0057635A` follows mass/dimensions even at
zero levels. See [VEHICLE_TUNING.md](VEHICLE_TUNING.md) for the seven
callees, offsets, exact apply order and test coverage. The already recovered
rule is available as `apply_vehicle_tuning`; neither race path calls it.
Cosmetic tuning requests contain no selected performance upgrade levels.

### 4. Engine/wheel coupling

Re-traced `0x00475530..0x004757C7`: recovered crank torque and drive split
are followed by wheel-spin-dependent braking and feedback. The speed curve
descriptor `0x00705E78` is evaluated at `0x00475747`; driven wheels alter
its gain using drive fraction and engine multipliers. The expression near
`0x0047576B` combines drive, brake and a wheel feedback term before division
by a constant. It is not the demo's square-root friction-budget clamp.
The producer and units of `[edi+0x74+8*k]`, clutch behaviour and the complete
state coupling are still unknown. Static torque/gear recovery cannot settle
either in-game acceleration or lift-off behaviour without that chain.

### 5. Inertia, damping and timestep

RECOVERED selector writers newly located: in `0x00474A90`, physics +0x34
is car and +0x20 is rigid body. For car +0x38 values 0 or 2, it sets body
+0x138 to 1 at `0x00474AD9`; value 1 takes `0x00474C64` and clears it at
`0x00474C79`. The subsequent car +0x3CC == 0 branch also clears it at
`0x00474B00`. Other values do not establish a new selection on this path.
The exact runtime meanings and player-state frequency of these gates remain
unknown. This narrows the old "writer not traced" statement in #58/#57.

`0x00584B51..0x00584B69` scales the yaw moment before building the alternate
inverse; `0x00585145..0x0058515B` selects it using that flag. For the stock
240SX it increases, not decreases, effective yaw inertia. The measured
inertia experiment confirms the expected effect on transient, not steady,
yaw. That is evidence against treating excessive inertia as the primary
source of the demo's large turn radius.

`0x00474DD0` contains conditional stabilisation absent from the demo.
For car +0x38 != 1 and +0x3CC < 2, `0x00474DFD..0x00474E85` takes the
magnitude of body angular velocity (+0xC0..+0xC8), evaluates pair descriptor
`0x00705E98`, multiplies by mass, negates and adds the vector-scaled result
to +0x120..+0x128. This is an angular damping contribution, including yaw,
not evidence that every player frame receives it. Subsequent alignment
branches and their gates still require tracing. A "NFS always adds yaw
damping" generalisation is weaker than these conditional instructions.

RECOVERED timing refinement: `0x00474BE4..0x00474C2D` subdivides frame dt
using `n = trunc(dt / [0x006FFF10]) + 1`, then dt/n per world update.
The divisor read here is approximately 0.0084 s. A normal 1/60 s tick gives
two 1/120 s steps. Thus the demo rate has a matching retail path, although
integrator arithmetic, ordering and other physics modes are not recovered.
Changing tick rate is not a justified handling fix.

## Complete approximation and unknown register

This register covers `VehicleApproximations`, named model rules, the race
input/camera path and the track adapter. Values remain in their headers/tests.
Effects are directional hypotheses where retail semantics remain unknown;
"low" means low relevance to normal clean-road feel, not proven correctness.

| Area / every field or rule grouped here | Expected effect if wrong; priority / remaining evidence |
| --- | --- |
| `max_steer`, `steer_speed_reference`, `steer_rate`; equal front wheel angles | Low lock gives wide turns; low rate delays angle; no Ackermann changes tight-turn scrub. Rank 1; new trace above. |
| `cornering_stiffness_front`, `cornering_stiffness_rear`; unused tyre slip/load/lateral scales | Too soft front/stiff rear gives understeer; opposite can spin. Rank 2; curve/force-state transitions incomplete. |
| `longitudinal_stiffness`, `slip_speed_floor` | Wrong slip stiffness changes launch/braking feedback; large floor damps low-speed response and alters parking grip. Rank 5, especially near zero speed. |
| Invented static-to-sliding friction-circle shape; no tyre transient/relaxation state corresponding to retail | A hard/incorrect transition makes grip either sticky or abruptly lost; no controlled original slip sweep. Rank 2. |
| `traction_limit` | Clamps drive and engine braking against requested lateral force, before saturation. Can bog power-on turns or suppress lift-off rotation. Rank 5; not active in baseline steady measurements. |
| `wheel_inertia`; implicit spin solver, explicit saturated/airborne spin, additive handbrake and lock-at-zero rule | Wrong angular response changes wheelspin, launch and brake release; too large inertia adds sluggishness. Ranks 5/8. |
| `engine_brake_scale`; suppress engine braking below idle; cut positive torque above max wheel rpm | Too much overrun makes gas release sticky and consumes rear grip; limiter shape affects gearing feel/top speed. Rank 5. |
| `auto_shift_hold_steps`; manual gear request ordering, invented automatic reverse and neutral treatment | Hold may delay needed shifts; wheel slip/shift loops can hunt without it. Reverse semantics affect manoeuvring, not fast turns. Rank 5/low. |
| Raw stock setup; missing always-applied second torque curve and inertia rebuild; no installed levels | Missing upgrades weaken a tuned car. Stock 240SX masks the two zero-level omissions; other cars need not. Rank 3. |
| Engine +0x138 divisor, boost/nitrous multiplier and nitrous block | Conditional torque can be wrong even with a decoded curve; nitrous activation/consumer not recovered. Rank 5 when used; absent in baseline. |
| Plain inverse inertia, no alternate selector, no recovered angular damping/alignment pass | Extra inertia slows transients; missing stability terms may be hidden by tyre understeer. Rank 6; recovered conditional chains above. |
| Semi-implicit Euler, quaternion normalisation, no explicit gyroscopic cross term, double/deterministic math instead of retail x87/float | Different transient/coupled roll-pitch-yaw response; highest in jumps/spins. Tick subdivision partially closed above; integrator remains APPROXIMATION. |
| `unit_scale`; force/torque/unit interpretation | A mismatched unit would grossly alter acceleration/load. Low suspicion: consistent mass, gravity and geometry support it, but the conversion is an inference. |
| `tyre_force_lift`; ground-normal strut force at contact; projected tyre basis | Small effective lever reduces transfer/roll; wrong direction on slopes injects force. Rank 7. Retail application points remain unknown. |
| Static preload argument zero; symmetric current-step anti-roll instead of sequential retail wheel loop | Missing speed support changes fast grip; simultaneous loads change transient left/right balance. Rank 7. Aero argument producer remains incomplete despite forward-speed trace. |
| `bump_stop_rate`, `probe_lift`; segment/normal rejection and suspension contact reset | Too hard stop kicks chassis; missed/extra ground support disrupts turn response over bumps. Rank 7. Excess-compression consumer/strut +0x1C semantics unresolved. |
| `brake_torque_front`, `brake_torque_rear`, `handbrake_torque` | Strong front bias reduces rotation, rear bias locks/spins; handbrake too strong spins, too weak will not rotate. Rank 8; retail brake distribution not closed. |
| `rolling_resistance`, `rolling_fade_speed`; 3D drag opposite velocity | Excess resistance dulls low throttle, excess drag dulls speed; fade affects creeping. Rank 10. Predictor coefficient known, integrated retail drag not matched. |
| Omitted drafting term car +0x47C, aero fields +0x374/+0x378/+0x37C, surface factors held at road default | Missing downforce weakens high-speed grip; missing drafting reduces close-follow acceleration; all-asphalt loses surface variation. Rank 10. |
| Model reset pose, flat support equilibrium, angular state clearing | Wrong starting height/spins cause a launch transient or reset discontinuity. Low steady-driving relevance. |
| Unconsumed record fields +0x2C4/+0x2D4/+0x2DC/+0x30C; grip +0x384..+0x38C; nitrous +0x3B0..+0x3BF; other undecoded record bytes | Could include coupling/grip terms with large effects, but unknown readers prevent predicting a sign. Do not invent semantic names. #57 candidate `0x00477936` is not proof of a grip reader. +0x380 now has a steering multiplier reader. |
| `DriveAxisCalibration::inner/outer`, per-axis mapping, raw integer quantisation | Excess deadzone suppresses small corrections; too-high outer value prevents full lock. Rank 4. Retail joystick calibration not recovered; radial `InputMapper` is not on this path. |
| `DriveSteerFilter::digital_rate/digital_rate_speed/centre_rate/analog_rate`, return-through-zero | Slow digital travel or return delays correction; outward analog default is immediate. Rank 4; retail history/rate mode selection incomplete. |
| `throttle_floor`, default linear curve rather than conditional retail curve | Floor masks engine-brake deadband but makes initial analog gas abrupt. No effect on default Switch ZR. Retail curve flag writer unresolved. |
| Digital arbitration, opposite-button cancellation, slot summation, hotplug latch, one-shot edges, sign conversion | Opposite inputs may cancel or override; hotplug can suppress first held gas. Known project policies, partly recovered arbitration; low sustained-feel relevance. |
| `reverse_assist`, `reverse_speed`, `reverse_hold_steps`, pedal swap | Unexpected reverse or delayed manoeuvring at rest; retail reverse action's physics consumer unknown. Low relevance at racing speed. |
| 60 Hz control sampling, two physics steps, catch-up sample reuse, pause/debt policy | Long rendering frames age input and hide intermediate states. Rank 4, unmeasured on today's console; deterministic replay cannot measure latency. |
| Script fixed-step input, segment-first shift edge, zero input after end; synthetic route driver | Useful deterministic experiments, no model of human timing. A slow driver recording cannot validate high-speed handling. |
| Camera `lag_factor`, fixed anchor distance/height, body-origin anchor, omitted roll/tilt, initial POV/cycle/reset seeding | Too much lag/fixed distance reduces visible agility or speed cues; no physics effect. Rank 9. Pull-back, car +0x3CC meaning and POV rebuild still unknown. |
| Camera `fov_mode`, near/far planes, `collision_margin/collision_return_rate`, deterministic angle math | Wrong FOV changes speed impression; collision moves eye abruptly or slowly. Projection axis, own-car sphere omission and world obstruction differences remain. Rank 9/low. |
| Track point-hull witness, one deepest contact per polygon, polygon order, cached pre-contact upright/speed, separation reach/failure behaviour | False/missed wall contacts or wrong impulse can feel like snagging/iron after a scrape. Low on clean asphalt, high at walls; retail witness/broad phase not reproduced. |
| Track post-model collision at each substep, kind-3 collision state, omitted restitution halving beyond known side-hit | Contact bounce/yaw and order may differ; no body hits in measured replay does not test this. Retail scheduling and additional restitution cases unknown. |
| Wheel rejection of walls; all wheel surfaces use road coefficients | Wrong slope/wall support or missing low-grip materials changes local handling. Material decoding and original wheel query remain unknown. |
| Sunk-check flag/query without retail cached polygon; out-of-bounds gate; nearest-node reset without route object; reset/grid height placement | Extra/missing reset or wrong landing pose interrupts drive. Low until off-road/stuck. Route object, cache, reset handler and reset-window semantics unknown. |
| Reset/progress/bounds order and tick phase; upside-down/stuck timers | Different recovery timing may strand or prematurely reset car. First-lap upside-down helper is project policy; no original rule recovered. |

The register does not claim every byte of speed.exe is understood. Known
untraced collision reset requests (`0x0044B4E3`, `0x00479FD5`, `0x0047ACE8`),
mode value meanings, garage-to-levels selection, throttle-curve device flag,
grip-factor producers and force integration are explicit remaining work.

## Physics round, in implementation order

1. Preserve the current replay as a legacy baseline. Add optional telemetry
   for raw pad value, sample age, filtered steer, requested/actual angle,
   yaw, slip, load, requested/applied wheel torque, gear and camera heading.
   Capture stock and explicitly selected performance levels separately.
   Obtain an original-game read-only observation/capture with known car,
   upgrades, controller, mode and patch; avoid settings/save writes under
   the protected retail installation. Until available, report comparison
   cells as unavailable instead of tuning toward real-car road tests.
2. Wire #57 through a setup factory: retail record ->
   `apply_vehicle_tuning(levels)` -> `decode_vehicle_handling` ->
   `VehicleSpec` -> model/shift thresholds. Run zero levels for stock too.
   Keep cosmetic preset selection separate. Test zero-level 240SX equivalence,
   another stock car with nonzero second torque curve, and each known level's
   mass/CG/ratios/torque/tyres/suspension before measuring upgraded telemetry.
   Carry unsupported grip/nitrous/aero/yaw fields explicitly; do not imply
   decoded-but-unused fields are simulated. Use an opt-in setup initially.
3. Complete steering target/rate/history reconstruction and device/mode
   gates from the traced functions. Add byte-rebuild tests for any new
   numeric tables, then independent target/slew tests at held speeds and
   reversal inputs. Verify the steering multiplier's setup-relative offset.
   Apply rate filtering once at the correct layer. Keep changes opt-in.
4. Recover tyre curve family and combined-slip state transitions, then
   engine feedback/clutch coupling. Recover force points, yaw selector,
   damping and grip-multiplier gates before removing the compensating
   front/rear balance and traction clamp. Measure one factor at a time,
   then the combined system: power-on, lift-off, brake-in-turn, step and
   reversing steer, full-lock, surfaces, slopes and jumps. Higher yaw alone
   is not a pass; include sideslip/spin and achievable speed.
5. Finish brakes, aero/load transfer and surface mapping; compare the five
   manoeuvres on stock and known tuned levels against original captures.
   Compare camera variants on identical states, and instrument Switch frame
   delivery before attributing residual input lag to physics. Only then
   choose a new default with owner drive evidence and deliberately reviewed
   new test pins. This research requests no pin change or console run.

## Reproduction

Run from the repository root. Output directories must be new. Put the
desktop toolchain and `cmp` on PATH (on this machine Strawberry precedes
MinGW; Git for Windows supplies `cmp`). Python is a standalone interpreter.

```bash
export PATH="/c/Strawberry/c/bin:/mingw64/bin:/usr/bin:/c/Program Files/Git/usr/bin:$PATH"
root='E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'
py='/c/ETAP 2102/ThirdParty/Python/Python384/python.exe'
"$py" tools/handling_measure.py --probe build/nfsu_probe.exe --race build/nfsu_race.exe --root "$root" --out out/handling-measure
"$py" tools/handling_trace.py --root "$root" --out out/handling-trace
g++ -std=c++17 -O2 -ffp-contract=off -fno-fast-math -Iinclude tools/handling_response.cpp build/libnfsu_drive_controls.a build/libnfsu_input.a build/libnfsu_vehicle.a build/libnfsu_sha256.a build/libnfsu_det_sqrt.a -o out/handling-response.exe
out/handling-response.exe > out/handling-response.csv
```

The response harness links existing built-in tables whose provenance/rebuild
tests already live in source. Its changed parameters are experimental
approximations, not new recovered constants. It is deliberately outside the
game's CMake targets and writes no retail data. Its CSV ends with two text
lines identifying input-only timing. The measurement script produces probe
logs, fresh paired replay outputs, per-run manifests and a JSON summary.
The trace script outputs bounded disassembly and runtime-read tables only
under the requested scratch directory. Neither those outputs nor any retail
or derived asset belongs in a commit.

## HUD shift presentation dependency (#253)

[HUD_253.md](HUD_253.md) names `HUD253_ENGINE_SHIFT_PARITY`: the port-only
frame/RPM audit shows the needle follows the same completed engine and gear
state through upshift and downshift. The rapid RPM rise and subsequent
automatic downshift therefore originate in simulation state, rather than
stale HUD data. This is a localization, not proof of a retail physics error.
Matched original stock shift sequences and runtime scheduling evidence are
still needed before changing the approximate engine feedback/clutch coupling
or automatic shift hold. Cosmetic red paint does not identify a performance
setup. The HUD work adds no smoothing or physics parameter change.
