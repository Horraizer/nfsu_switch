# Shadow, starter browser and drift follow-up

Owner scope: all-car shadows, traffic style penalties, native portable menu
resolution, first-car browser icons, cinematic wheels, turbo indication,
drift multiplier/camera and the drift finish sequence. Retail files remain
read-only. Git/index/publication remain held by the owner.

## Original input and evidence

The input is PC USA1.4 speed.exe, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
The original functions were inspected in the connected Ghidra project.
The decoded FrontB identity is checked by the production career loader.
See CAR_SHADOW_GPU.md, DRIFT_SCORING.md and DRIFT_FIDELITY_R96.md for the
preceding recovered producers and their limits.

## Shadows

Constructor56A130/56A919..56ABEC builds the footprint from the installed
LOD-A solid headers in slots0..11, including disabled selected models.
It does not use the handling dimensions or the union of every body kit.
Renderer56EB33..56F025 projects a four-by-four grid onto the road through
440A40, with wheel-plane fallback, per-tap validity and strip rejection.
World vertical alignment and height determine alpha/fade. The native lift
and the extra inner-point lift are already represented in the current path.

The production player, rival, traffic and cinematic placements that have
a SHADOWIG asset reach the projected shader. Each such car retains its
own texture and mip chain. The player's cinematic pose is also the pose
used for its shadow.
There is no original countdown or first-wheel-contact visibility delay.
Traffic models for which that lookup returns null have no blob layer in
the port. This does not establish that the original renders no shadow.
Most authored traffic models have no car-specific SHADOWIG texture.

Extended Ghidra inspection found an additional high-quality geometry
projection before the blob grid in56DC30. Constructor56A130 resolves
GLOBALB texture BADB4475, SHADOWRAMP;56D700 supplies a light-dependent
projection. This branch has its own detail, geometry, quality, view and
visibility gates. The port has no corresponding silhouette projection.
The full shadow audit therefore remains open, including traffic. The
exact original selected geometry, contour construction and view gates
must be restored before claiming complete fidelity. No generic blob was
introduced to imitate this missing layer.

`nfsu_car_shadow_roster_retail` exercises every playable car with its stock
and selected dressed body, plus the actual tuned UG0 grid and traffic
packages. It checks finite selected bounds, texture ownership, mip upload,
pre-contact projection, shader selection and the final height-uniform row.
`nfsu_car_shadow_projection_tests` compares the original ground/grid cases.
`nfsu_switch_shadow_height_contract` checks the actual generated shader IR.
No new shadow size, darkness or lift was applied without a demonstrated
producer mismatch. These checks do not certify identical console pixels.

## Traffic penalties

465830 accumulates the contact impulse and invokes43FBB0 on the contact
notification.43FBB0 admits sufficiently strong, recent rigid-car contacts;
if a participant is traffic, its style item receives the capped impulse
factor. The selected GLOBALB style row supplies the negative rate, strict
minimum and count policy. A small touch need not produce a displayed loss.
Negative notices precede positive notices and use the red loss presentation.

The port already followed the impulse factor and loaded tuning, but could
still trigger traffic penalties during its post-finish braking. The native
status gate excludes a finished racer. That gate is now present in the
production contact path. `nfsu_race_style_retail` checks actual negative
style admission, debit and near-pass behavior. Native contact notification
ordering remains distinct from whole-race contact-solver equivalence.

## Menu target and starter icons

The Switch front end now opens its own scene target at720 lines, yielding
1280x720 for its16:9 portable viewport. The HUD/UI was already rendered at
the output resolution. The change raises the garage geometry target;
race scene_height and its docked/portable policy are not changed.

4C2ED0 creates the first-car thumbnail browser from master_thumb and thumb.
4C3240 assigns CARSELECT_MANUFACTURER_<brand> to each runtime clone;
4C2B00/4C3120 assign the independent selected-car logo. The starter page
had suppressed placeholders without drawing these runtime clones. It now
submits them at the authored anchor, with the existing native browser
scale/fade. Navigation clamps at the ends, as4C3390 does. The real career
Trade In check also rasterizes and checks the starter browser's changing
upper row. It writes a fresh starter-ui capture beside its other captures.

## Cinematic wheels

The first drift scene samples the authored car-root motion at the original
half-rate clock. Previously only body placement used that motion; wheel
placement received the held physical car's stationary animation state.

The renderer now samples a separate visual phase from signed root travel
and each installed wheel's rolling radius. Travel is prepared once with
the scene; rendering does not integrate elapsed time or allocate a history
buffer. Repeating a frame, pausing and restarting therefore keep a stable
phase. The final visual offset remains when ordinary physical wheel motion
resumes. Brake parts retain the existing unspun placement. This is a
root-motion adapter, not a claim of complete native NIS wheel-state replay.
`nfsu_drift_hud_retail` checks movement and repeated-frame phase stability.

## Turbo and drift state

46CCC0 updates display pressure;46CFC0 supplies effective engine throttle.
The constructor derives the installed induction maximum through46C6D0.
42A6B0 returns this pressure to4A13A0;4A1460 maps it to the gauge angle.
The displayed value is not an additional physical turbo multiplier.
The current spool/decay producer was compared with isolated original x86
execution for RPM, throttle, mode and decay-time cases. The ordinary and
drag gauge-angle branches were also inspected. No arbitrary slower spool
or extra needle smoothing was introduced.

43E790 uses pending competitive drift points, performance-class targets,
speed gates and timed reduction/recovery. Promotion is strict; equality
does not advance the multiplier.43E310 applies the separate impact reset
and direction-change rules. The current producer and live score/HUD wiring
retain those distinctions. Native vectors and the production slide check
are `nfsu_drift_state_retail` and `nfsu_drift_hud_retail`.

4B47A0 selects the fourth configured POV on drift entry;42E020 includes it
in drift cycling. The selected car's own camera record supplies that high
view. The port already followed this dispatch. Camera surge, angle lag and
projection retain their existing native checks; terrain retraction and the
complete original mover still have the adapter limits in CONTROLS_CAMERA.md.
No whole-camera or whole-race bit-identity claim follows from POV agreement.

## Drift finish

435310..43531A switches a finished human to controller3; the finish action
then requests a road path at zero desired speed. Original physics continues
while that driver brakes.4A7D60 starts the authored finish HUD animation;
displaying Race Over does not mean freezing the physical car immediately.

The live Switch loop and RaceRun had restricted this continuation to races
with physical rivals. A drift race has virtual score opponents, so its car
froze. Drift now prepares a human road context and uses the same ordinary
finish driver after its result is fixed. It ignores player gas/shift input,
keeps physical motion, stops nitrous and proceeds through the existing
animated result/statistics flow. No physical drift opponents are created.
`nfsu_drift_finish_retail` completes a real input-driven drift route, checks
continued movement/braking and verifies that the recorded finish time stays
unchanged after the finish. Hardware presentation and timing remain to be
accepted on the final NRO.
