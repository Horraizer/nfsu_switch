# Car collision comparison, 2026-10-15

Scope: the owner's rejected `blur-contact-r2` player/rival/traffic collisions.
Source inspected: `91a39df6cd2e6157b5850e9b4a96e492562a7c55`.
This is a comparison and an implementation checkpoint, not a parity verdict.

## Reference

The reference is the owner's PC USA 1.4 `speed.exe`, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
Retail inputs were read only. Original function addresses below refer to that
image. Native probes execute its instructions through `tools/contact256_native.py`.

## Confirmed differences at the inspected baseline

| Stage | Original | Current game path | Consequence and evidence limit |
| --- | --- | --- | --- |
| Shape query | 46AE60 calls 472830/472950 and uses query status; 46B8C0 prepares normal and point from shape witnesses | `race_contact_box` discards hull vertices after computing bounds; `resolve_race_pair` uses oriented-box SAT and a projected box-center midpoint | Contact point and normal are not original witness results. This changes the angular impulse arm; the owner's exact launch is not yet reproduced from a captured input. |
| Strong hit | 465D00 changes eligible state-0 actors to state1, resets the timer and applies additional impulses; the record gate affects later response | `body()` leaves collision state/timer at defaults; production calls calculation and bare impulse application, bypassing the recovered callback and record accumulation | Isolated native comparison with identical geometry confirms different momentum and state. |
| Contact-time processing | 46B5E0 prepares/samples both bodies, adjusts positions, refreshes, resolves, refreshes again, then calls both +28 continuations | The adapter samples poses/linear velocities, resolves, separates boxes and publishes the sampled states | Original kind-0 +28 is 46A030, which snapshots and calls 4264D0 for continuation. The game adapter has no corresponding remaining-time integration. This is a structural mismatch, not yet an isolated native trajectory comparison. |
| Admission/search | 46AE60 uses shape-query flags and its contact job times, with an eight-round search and fallback path | `resolve_swept_race_pair` first requires final-pose box overlap, then bisects inside/outside box overlap | Equal iteration count does not make the algorithms equivalent. A pair that intersects between endpoints but separates by the final pose is not searched by this adapter. Original behavior for that specific input still needs a scheduler-level witness. |

The per-slice pair loop in `src/race_session.cpp`, and `meet`/`meet_external` in
`src/vehicle_track.cpp`, do not supply the missing continuation or persistent
crash lifecycle. The r2 velocity-epoch correction addresses one prerequisite;
it does not close these differences.

## Isolating the strong-hit difference

At the inspected source SHA, the local probe uses two identity-oriented cubes,
mass2, identity inverse inertia, zero angular momentum and initial state0.
Their centers are separated by2.0005 along X. Opposing X velocities are10 for
the soft case and50 for the strong case; the side case adds0.7 to center B's Y.
These are synthetic solver inputs, not retail vehicle speeds or tuning values.

The original query supplies the normal and point. The port probe then uses
those exact values and original pre-solve body snapshots, following the game's
calculation-plus-bare-impulse call sequence. Thus this comparison deliberately
removes SAT geometry differences from the strong-hit test.

Historical results at91a39df, from that isolated probe (linear momentum P):

| Case | Original P(A) | Game-sequence P(A), rounded output | Original states / game states |
| --- | --- | --- | --- |
| Soft head-on | (11.538461685, 0, 0) | (11.5385, 0, 0) | 0,0 / 0,0 |
| Strong head-on | (49.230766296, 0, 2.855769396) | (57.6923, 0, 0) | 1,1 / 0,0 |
| Strong offset | (50.806480408, 9.668626785, 3.320562840) | (50.8065, -0.170078, 0) | 1,1 / 0,0 |

The port log uses default decimal formatting: the soft result agrees at that
precision, not a claimed bitwise equality check. The strong cases demonstrate
missing behavior even when both paths receive identical contact geometry.
The original intentionally adds angular and vertical impulses in some strong
hits. Suppressing all vertical motion or all rollovers would not restore it.

Limits: forced isolated solver entry, identity orientation, two kind-0 bodies,
optional effect tail bypassed. These results do not validate admission, the
full race scheduler, recovery duration, traffic-specific eligibility, scoring,
or an entire trajectory. An earlier penetrating-input exploration produced
one unmapped-read refusal and two non-closing results; it is not evidence that
the original ignores real penetrations, and its refusal is UNKNOWN.

## Implementation order

1. Restore production contact geometry and contact-time/remaining-time flow.
   Keep the cheap broad rejection, but do not use its boxes as a substitute
   for final original geometry.
2. Generalize the authenticated strong-hit callback to actual orientations,
   vehicle eligibility and physical bounds, with persistent actor state,
   timer and contact records. Recover the exit/recovery transitions too.
3. Exercise player/rival and player/traffic contacts together, including
   glancing, opposing, repeated and already-overlapping cases. Compare the
   relevant original boundaries before a combined Switch check.

`ContactCallbackContext` explicitly supports a bounded identity-orientation,
four-wheel synthetic witness. Simply passing it from production would claim
conditions that real rotating cars do not satisfy. No such connection or
arbitrary impulse clamp was made during this comparison.

## Local evidence and remaining work

Evidence is under `build/ug0-contact-ai-r1` in the owned worktree:

- `response-original.txt`, `body-time-original.txt`, `time-update-original.txt`:
  original processing and virtual continuation disassembly.
- `collision-original-gap-comparison.py` and corresponding JSON: original
  three-case probe, authenticated executable identity and explicit limits.
- `collision-same-geometry.cpp`, `run_collision_same_geometry.py`,
  `collision-same-geometry.log`: port sequence on those native snapshots;
  compile and run completed with exit0.
- `collision-original-comparison.json`: earlier penetrating-input exploration,
  preserved including its UNKNOWN result.

The evidence files are local ignored artifacts; the observations and inputs
above are the durable text record. No production fix, new NRO, full test matrix,
independent review, push or merge is claimed by this document. Collision parity,
NOS visual parity and difficulty remain open.


## Implemented candidate: collision-flow-r3

The baseline findings above remain historical. The owner subsequently authorized
implementation in the existing combined worktree. The candidate retains the
selected CV vertices, computes convex witnesses after bounds rejection and uses
the recovered46B260 origin-direction correction for deep car/car overlap.
Props retain their separately qualified bounds adapter.

The normal-mode465D00 callback now accepts body orientation, actor up and wheel
support; production supplies actual car transforms, physical lateral bounds and
persistent pair records/random state. Record expiry follows the existing420720
last-touch clock rule. Collision state and the continuous-support recovery timer
persist across frames;42A530 recovery is wired into both player and AI sessions.
Player damping, alternate yaw and low-support torque consult this state.
Car/world angular response also uses the state1 factor.

After a searched collision, both player and selector3 components integrate the
remaining duration with the post-impact momenta. This continuation does not
advance the world clock, resample controls or emit another route/lap event.
Traffic uses its retained impact component. Failed pair continuation restores
its actor/component state, record and random seed.

### Evidence and limits

- `tools/contact_callback_native.py executable output-json`: six isolated
  original callbacks with rotated axes, differing support and a retained gate.
  `tests/car_contact_rotation_pins.inc` checks their momentum/state snapshots.
- `tools/contact_state_native.py executable output-json`: ten original42A530
  recovery boundaries, including clock values on either side of the threshold.
  The recovery pins check the exact stored state and binary32 timer.
- `tools/contact_hull_native.py retail-root output-json`: original selected
  retail CV query. `nfsu_ai_track_tests` checks four separated witness cases,
  empty convex space inside intersecting bounds and continuation for both
  driver components. The source executable is authenticated in every probe.
- PC gameplay probes exercised18 head-on/offset player-versus-traffic cases
  and the existing six-model traffic wall fixture without solver refusal.
  Strong hits can still produce rollovers; these probes do not establish
  original whole-trajectory parity or validate the owner's exact impacts.
- A9000-frame UG0 runtime/profile and an18000-frame assisted route run ended
  without a simulation error. The player did not complete a lap in either;
  those runs are stability evidence only, not successful race acceptance.

The convex distance implementation is not a literal46EA50 feature dispatcher;
penetrating native witness choices differ before fallback separation. Final-pose
admission and fixed pair order still differ from the original queued scheduler.
The actor-up publication epoch is supplied by the existing port pose adapter;
separate original actor/body publication timing is not fully recovered.
Optional crash feedback/config/race-event hooks remain outside this callback.
NOS blur and rival difficulty are unchanged by this candidate and remain open.

### Validation record

The current candidate passes the three selected car-contact, AI-track and
race-session CTests. Broader vehicle and vehicle-track tests still fail; no
pins or tolerances were refreshed. A controlled parent-vehicle-object relink,
with the same current ABI and test instrumentation, produces byte-identical
failure output for both. This comparison isolates the vehicle.cpp delta; it is
not a fresh full-parent build or a substitute for the full required matrices.
The matrices remain unrun under the owner's targeted-check direction.
Switch SDK build and seven selected package/shader checks pass. Hardware
behavior and performance remain unverified for this candidate.


## Queued car contact correction (r4, after767542f)

This section supersedes r3's final-pose/wide-contact admission, forward bracket
search and fixed one-pass car pair order. It does not supersede the feature
reduction, actor publication, optional effects or full-runtime evidence limits.

-468060 admits flag15 only.46AE60 samples a common remaining time from zero to
 the smaller body duration, distinguishes flag15 from flag14, keeps the last
 midpoint after eight queries, and invokes46B260 only for its final deep query.
 A job is not rejected a second time because that last query is separated.
-468FE0 interpolates stored matrix components, without quaternion nlerp or
 normalization.4692F0 samples linear velocity at the same remaining time.
 The short/zero-duration branch retains the integrated current pose/velocity.
-471DE0 keeps feature indices across samples and has a squared-distance early
 zero at float0.001, separate from the wrapper's two contact thresholds.
-420380 queues finite remaining times in descending order, preserves ties and
 pair insertion order, invalidates jobs touching changed bodies and re-queries
 affected pairs after continuation. Original700E5C/700E60 provide the job and
 pair-repeat limits. Car/track/prop combined scheduling is still not recovered.
-469FB0 dispatches controller+34 before response. Traffic583E80 selects the
 rigid mover at that point, even if an admitted job later has no closing hit.
 Traffic now reaches its end pose before car-pair admission. Its stop threshold
 consumes the shared pair record rather than an independently accumulated sum.
-Active car-pair records publish the signed body contact count.42A530 can enter
 state2 and leave it when records expire and all wheels are supported. Separate
 track/prop record ownership and the full original actor epoch remain open.

Reproduce authenticated original boundaries with:

```
python tools/contact_search_native.py <retail-root> <output-json>
python tools/contact_flow_native.py <retail-root> <output-json>
```

The first executes eight search/fallback fixtures and five exact matrix/velocity
samples. The second uses original constructors, retail240SX hull/handling,
registered pair admission and queued response on three explicit zero-duration
fixtures. `nfsu_ai_track_tests` checks the search, sampling and flow pins in
`car_contact_search_pins.inc`, `car_contact_sample_pins.inc` and
`car_contact_flow_pins.inc`. Episode comparisons cover separated positions,
current-hit impulse and linear velocity; they do not cover the whole ensuing
trajectory or prove every tuned/traffic model. Witness/response comparisons use
the already established numeric criteria; no broader tolerances were added.

The discarded eight-bracket penetration bound and zero-duration rewind test
were port assumptions contradicted by original46B260/468FE0. Replacement native
fixtures cover these cases, including real moving histories, retained matrices,
nonclosing jobs, continuation and no artificial vertical repositioning of a
horizontal contact. No unknown crash was counted as a successful comparison.

The live event0 scheduler selector is still unavailable. Direct executable
cross-references read00736200 and show no direct writer; that is not proof
against an indirect/configuration write. This implements the recovered zero
selector branch. The queued callback now preserves the endpoint actor up axes while469390
rewinds only the rigid body matrix. This follows the465D00 actor+90 read and
46B5E0 response-before-continuation order; the independent callback pins already
exercise distinct actor/body axes. Full original collision parity remains
unconfirmed, particularly combined car/track scheduling and untested hull poses.
