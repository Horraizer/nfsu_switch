# Original collision and airborne camera requests

## Provenance and scope

Read-only analysis of the owner's PC1.4 USA speed.exe, SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Ghidra decompilation was checked against the x86 branch/call instructions.
This document records the original request conditions and the local r26
integration boundary. Console appearance is not yet accepted. Evidence is in the local integration
worktree under build/engine-audio-r24/root-ghidra-crash-camera-r*.json.

## The contact-side gate

465830 reports the CURRENT impulse to465D00 before visual/audio feedback.
465D00 computes its magnitude and caps that value at50 for its crash logic.
The cap does not change the two thresholds below. This is not the retained
record's accumulated magnitude and not an MPH threshold.

The outer state transition must first be eligible:

- Current impulse magnitude is at least30.
- The participating car has actor collision state0 before this transition.
- The pair contains two kind0 car bodies, or the separately tested static
  collision class accepted by4663D0.

That transition sets collision state1 and clears the continuous-support timer.
The later camera request at466286..466329 has stricter conditions:

| Condition | Original source |
| --- | --- |
| Drift mode byte78A345 and its override7361A4 are both zero | 466286..46629A |
| BOTH bodies are kind0 cars | 4663A0, called at4662A3 |
| Current impulse magnitude is at least35, including equality | 4662AC..4662BB |
| This participant is the real human: config role1 and config+734 is zero | 424D60, called at4662C5 |
| Its current cinematic event is neither6 nor7 | 4662D1..4662DC |
| A usable road segment can be prepared | 42C4B0, called at4662E0 |

A successful branch requests cinematic event6 with arguments(6,0,1,1).
There is no random-chance check in this contact request path. It is not limited
to traffic: a sufficiently strong human-versus-rival hit can request it too.
A subsequent contact while collision state1 is retained does not repeat this
outer state0 transition. Original42A530 owns recovery from that state.

## Road eligibility

42C4B0 clears the old cached road path and finds the nearby route segment.
It requires a forward span reaching at least75 route-distance units and more
than four retained entries. If that span cannot be built, it returns false.

There are two explicit local exclusions: track1001 within40 units of
(1415,-920), and track1002 within40 units of(1290.9,-905). Do not generalize
these exclusions to every track or infer that any visually open street is
eligible. The underlying route geometry controls the check.

## Common cinematic gate

42DA40 also rejects the request when any of these applies:

- Drift byte/override or challenge byte78A346/override7361A0 is active.
- The global foreground mode is9.
- The selected event timer is valid and less than5 seconds old. This request
  passes its second flag as0, so it uses event6's timer, not the separate
  .1-second shared-timer branch.
- Race state is not4, the event ID is outside0..7, or two player views exist.
- The event6/7 enable option78A3FA is zero.4B47A0 loads it from profile758951.
- The per-view cinematic blocking table says another active event prevents it.

If accepted, it stores the event timer, selects camera constructor15 where
applicable, prepares the road-side shot and marks cinematic event6 active.
The road-side camera initializer42C6D0 case15 compares both candidate sides,
tries an alternate side when the first visibility query fails, and adjusts
height against the ground. A fixed arbitrary side offset would not reproduce
that placement.

## Additional airborne request

Actor update426930 contains a separate route to the same event6, checked on
one of every eight global frames. It does not require a new contact impulse.
At426CF2..426DF8 it requires all of these:

- Global7361E8 is zero, and the current cinematic event is neither6 nor7.
- Published support count actor+3CC is zero and view+4AC is zero.
- Published speed actor+3D4 exceeds curve7062FC at option78A3FA.
- Predictor429CF0 reports maximum clearance above sampled terrain greater
  than curve7062CC and predicted airborne duration greater than7062E4.
- Road42C4B0 succeeds, followed by the same common42DA40 event6 gate.

429CF0 samples the predicted trajectory against terrain440A40. With the
caller's final flag1, it stops at the first predicted ground intersection.
Its two output pointers receive maximum terrain clearance and duration;
the average-clearance return value is discarded by this caller. The prediction
horizon comes from curve706314. These original curve endpoints are:

| Curve | At option0 | At option255 |
| --- | --- | --- |
| 7062CC maximum-clearance threshold | 2.5 | .5 |
| 7062E4 airborne-duration threshold | 2.5 | .5 |
| 7062FC published-speed threshold | 50 | 5 |
| 706314 prediction horizon | 4 | .5 |

These are original internal values, not MPH conversions or acceptance pins.
The option's exact visible UI label and its normal profile range remain
unproven here. Local read-only curve extraction is root-camera-curves-r1.json
under build/powertrain-traffic-r25; the predictor decompilation is in
root-ghidra-crash-camera-r8.json beside the other Ghidra evidence. The later native comparison under Port status supersedes the initial
read-only predictor investigation; its supplied terrain boundary remains
explicit.

## Return path

42DA30 is the registered completion callback and calls42DDC0(1). That method
clears cinematic event state, resets world time-effect parameters and restores
the normal view through42C450/42D470 when the foreground mode permits it.
Actor update426930 also owns earlier completion. While event6 or7 is active:

- In collision state1, all four wheels supported and crash age greater than
  .5 seconds call42DDC0(1).
- Outside collision state1, any positive support count calls42DDC0(1).

If event6 remains active in collision state1 beyond3 seconds, it calls
42DDC0(0), then attempts event7/camera16 through mode, option, race-state,
view-count, cinematic-blocking and event7's own5-second cooldown checks.
Outside collision state1, the corresponding transition uses actor+4C4's
predicted-flight deadline. That deadline is current time plus half the
predicted duration when duration is at least.9, otherwise current time plus5.
These return/transition branches were cross-checked against the instructions;
they were not included in the bounded contact-request execution probe.

420050 stores time scale.4, timeout8 and callback42DA30 in the world.
420110 decrements that timeout with its unscaled update delta, restores scale1
and invokes the callback after expiry, then returns the scaled update delta.
The earlier support/age branches can end or change the shot first. Therefore8
must not be described as an unconditional eight-second visible shot; exact
live scheduler cadence and rendered timing remain hardware acceptance work.

## Port status

The r26 implementation connects RaceCrashCamera to the actual car-pair queue,
retaining the current-impulse threshold and new collision-state transition.
Static props and track contacts do not substitute for a car-pair request.
RaceCrashRoad clones the human planner context and extends a private chain;
it neither changes the rival planner decisions nor consumes its live RNG.
Free driving decodes its own direction-oriented context once at load time.
The shot is ahead of the car at the endpoint selected by42C6D0, with the
opposite side of start+2 retained for the later shot. Width virtuals use raw
road edges, two units inwards, rather than propagated driver clearance.

Both live applications enable the camera for ordinary single-player driving.
The live applications select kCrashCameraDefaultIntensity. The original
4AA200 initializer and4B47A0 publication now independently verify that
default in camera_presentation_native.py; its fixture owns the value. Recorded legacy diagnostic sessions default
to camera-off and retain their prior fixture identities. Drift and drag do
not currently enable this road-shot adapter. Other original cinematic modes,
split-screen ownership and view-blocking tables are outside the live modes
currently exposed by this port.

World dt comes from one owner and reaches the player, rivals, traffic, props,
steering, route/world timers and ordinary camera update. The display/input
cadence remains unchanged. Selector1/3 and props recompute slice counts from
the scaled interval. Explicit end-of-frame route publication avoids modulo
misalignment when slice counts change. Texture/neon/flame presentation uses
a scaled visual clock, and existing smoke ages across dt transitions.
Music and UI processing retain their real-time ownership; no untraced audio
pitch change is added.

The chase controller keeps following underneath the cinematic override.
Support/age, second-shot transition, real-time timeout, finish/disabled mode
and reset restore its view and world scale. POV/rear input remains owned by
the chase controller. The original focus-distance spring and depth-of-field
rendering are not implemented; the random focus parameter is not camera roll.
The contact geometry and vertical terrain service remain the port's
collision-world adapters; the vertical query includes the original first-pass
query.z+1 admission height. Visibility uses original457300/457180 road-strip
intersections, not a collision ray. The renderer adapter consumes the
cinematic heading/pitch words as well as its eye and FOV. These boundaries prevent a claim of complete
retail rendered parity before the console comparison.

Native mover, predictor and road-controller comparisons are recorded under
build/crash-camera-r26. The mover executes4854E0/485C00 up to the renderer
boundary with supplied registration. The predictor executes465150 and429CF0
with supplied flat terrain. Road cases execute42C6D0 over initialized retail
roads with supplied terrain and endpoint projection. The constructor-side
selection matrix also exercises supplied visibility results independently;
road-visible-report-r1 executes the actual457300/457180 predicate. They cover
both direction choices and the visibility fallback, not a complete native
race. The initial road attempt exposed an incorrect endpoint and edge-width
interpretation; the corrected matrix report is the evidence of record.
The first predictor attempt exposed the landing-sample duration store; only
predict-native-r2 is a passing comparison. The first mover comparisons exposed
duplicated-point Catmull-Rom rounding; view-native-r6 is the passing result.

Regression coverage is nfsu_crash_camera_tests: current-contact admission,
cooldown/support/shot/timeout boundaries, actual queued car impact, common
world clocks for three rivals, variable slice publication, normal return and
smoke aging across a time-scale transition, and post-race distance using
physics time rather than presentation frame count. This is local evidence, not a
hardware acceptance verdict. The local checkpoint records the required
Default, Release and Switch gate results once those runs have completed.

## Gate execution evidence

The controlled original post-transition contact slice466286..466329 and the
full common42DA40 gate were also executed. The successful camera-gates-r2
report under build/powertrain-traffic-r25 covers threshold equality and timer
boundaries as well as role, body kind, active event, modes, option and player
view count. Road eligibility is a declared supplied result; the camera GPU
constructor is a declared observed boundary. The first attempt failed because
the synthetic active-view registry was absent. That failed attempt was not
counted as a matching result. The request checks are verified; rendered camera
placement and callback cadence remain unverified by this bounded probe.
