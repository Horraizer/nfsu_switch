# UG0 collision, difficulty and NOS follow-up, 2026-10-15

- Role: worker, root, solo owner-directed implementation.
- Branch: codex/draw-distance-original.
- Source baseline: be5984c45ab55ba5a0b889d3c9a4597aea91d520.
- Owner rejected the latest NOS appearance and UG0 collision/difficulty behavior.
  Drift was not reached; drift presentation remains hardware unaccepted.
- Retail remains read-only. No remote state or other worktree was changed.

## Evidence retained locally

`build/ug0-contact-ai-r1/reference-manifest.json` identifies all five supplied
Switch videos by SHA-256. One-second overview sheets for all five were viewed;
`impact5-sheet.jpg` additionally samples video 5 at 10 Hz from 15 to 18 seconds.
The second and fifth videos show large launches following traffic impacts.
Video 5 shows roughly one second held against the van before both bodies fly.
The player leads before that impact; difficulty cannot be diagnosed only from
race losses caused by collisions. No original AI difficulty parity is claimed.

At the baseline above, `contact_probe.cpp` and `run_probe.py` replay 150 frames
of head-on Eclipse collisions against six authored traffic bodies, with three
lateral offsets, on the existing proving ground. This is a port reproduction,
not an original full-trajectory oracle or an exact replay of the owner's inputs.
`contact-probe.log` contains the contact fraction, Z, before/after velocity,
impulse and angular velocity. The SUV offset-1 case holds Z at 10.3776 while
vertical velocity rises from 5.3319 to 13.2779 across repeated zero-time hits;
it later reaches Z 19.201 from the initial Z 10.4033.

The current swept adapter rewinds pose but retains end-of-step momentum and
omits the remaining-time integration after response. Original disassembly in
`response-original.txt`, `body-time-original.txt` and `time-update-original.txt`
shows 46B5E0 invoking car virtual +2C before response, then virtual +28 after it.
+2C=469390 samples pose and linear velocity (4692F0), writes mass-scaled linear
momentum; +28=46A030 snapshots through 468F70 and calls physics through 4264D0.
This establishes a missing integration boundary; it does not implement it or
prove that it is the sole cause of every owner-observed launch. Production
collision code is unchanged at this checkpoint.

`ai_catchup_update` has no production caller at this baseline. Original event0
activation/configuration still needs verification before integration. No
arbitrary rival speed adjustment was made.

## NOS diagnostic candidate

The previous GPU UV correction did not fix the owner's vertical stretching.
The current GLSL compiles and its CPU/desktop checks do not establish Switch
pixel correctness. Do not mark the blur fixed.

Added opt-in `NFSU_SWITCH_SPEED_BLUR_PROBE` (default OFF). It samples a 9x9 grid
of RGBA pixels before blur and after each of its four passes on ONE active
NOS frame. Every pixel occupies a guarded 256-byte region. The existing frame
fence protects CPU consumption; no additional CPU wait is inserted. Output is
`BLUR_PROBE`, 45 `BLUR_PIXELS` rows and `BLUR_PROBE_END`. Copy/barrier work
perturbs that one frame, so it is explicitly excluded from performance claims.
No normal-frame readback remains after that capture. Buffer guards are checked.

Candidate: `build/ug0-contact-ai-r1/nfsu-blur-probe-r1.nro`.
Identity and size: `build/ug0-contact-ai-r1/blur-probe-manifest.json`.
Build command and actual-workspace smoke: `build/ug0-contact-ai-r1/build-probe.sh`.
SDK build succeeded. Targeted NRO, actual embedded shader lookup and GPU sample
contract tests passed 3/3; log `build/ug0-contact-ai-r1/sdk-probe.log`.
Modified source/probe script bytes and new artifact names passed ASCII checks.
This diagnostic is not an AI, collision or blur fix. It has not run on hardware.

## Next

1. When current Netloader readiness is confirmed, send the sealed diagnostic
   NRO and capture one brief NOS activation. Read the per-pass GPU pixel output;
   fix the first bad boundary, then confirm the visual result on Switch.
2. Implement and test the original-backed sampled-velocity/remaining-time
   contact boundary for player, rivals and impacted traffic, including retained
   controls, force/history epochs and unchanged world/lap clocks. Do not replace
   it with a vertical-speed clamp or an arbitrary contact force reduction.
3. Verify and connect the original event0 catch-up/difficulty producer.
4. Preserve the drift work until UG0 permits its hardware acceptance.

No upload, new hardware acceptance, merge or push is claimed here. The owner
requested solo work and necessary targeted checks; no reviewer was dispatched.


## Follow-up: hardware capture and local candidate r2

Owner ran blur-probe-r1 and again rejected blur, collisions and difficulty.
The sealed upload was received and reached the menu; the session subsequently
recorded a complete one-frame BLUR_PROBE and exited. Receipt:
`build/ug0-contact-ai-r1/upload-blur-probe-r1-run1.json`; runtime log:
`nxlink-blur-probe-r1-run1.log`. No old nxlink process should be polled.

At local diagnostic head7532f2b, the hardware capture has nine distinct rows
in stage0 (scene) and stage1 (reduction). Every sampled row of stage2 is
identical, and stage3 repeats that failure. Guards PASS. The first bad
boundary is sampling/rendering the half-resolution input in the first radial
pass. It is not established whether the combined shader compiler path or a
resource/state defect causes that boundary; do not claim a proven compiler
bug. Stage4 blends the corrupt half image with the original scene.

The local r2 candidate splits reduction, radial sampling and composite into
three branch-free shaders, retaining four passes, offsets, tap weights and
alpha. Desktop GL outputs for all four passes are byte-identical to the
parent combined shader on the same saved scene. This is a controlled candidate,
not hardware proof that the stripes are fixed. Sparse one-frame GPU capture
remains enabled to verify the same boundary on Switch.

Collision correction: RaceContactPose now retains linear velocity, and the
swept solver samples it at the searched contact time before mass/momentum
publication, following original469390/4692F0. Angular momentum is retained.
The initial test incorrectly assumed the reset velocity had zero Z after
changing only orientation; the authored pitch still contributes velocity.
The corrected test compares to an otherwise identical control solve without
the injected future suspension velocity. Parent source fails this regression
with the expected failure message; successor passes. No tolerance was added.
Failed original test output remains in epoch-tests.log and epoch-test-debug.log.

At this uncommitted candidate, the 18-case local proving-ground probe completes;
SUV offset1 maximum Z changes from19.201 to11.3492 (start10.4033).
This is a port regression measurement, not native full-trajectory equivalence.
The six-body traffic-impact viewer check also passes; it is a separate finite
fixture, not an exact replay of the owner's hit. Remaining-time reintegration,
original hard-hit state/lifecycle and real owner collision acceptance stay open.

Difficulty: original event0 initialization in PairedController(event_rides=True)
sets catch-up enabled and comparison enabled. One valid supplied scene step
produces history0.35087719559669495 and speed scale0.7653199434280396.
The following scene point10 refuses at undeclared instruction00433B0A; no result
is claimed for it or later cases. Evidence: catchup-initialized.json,
catchup-live-supplied.json and corresponding scripts/logs. This is a bounded
original two-actor fixture with supplied poses, not a live four-car race.
IntroAiPlanner currently never calls ai_catchup_update and leaves
AiSpeedPolicyInput.catchup_scale at1. The history producer and actual runtime
bindings must be recovered before connecting this; do not install a constant
0.765 scale or arbitrary difficulty reduction.

Validation from the actual owned workspace:
- cmake --build build --target nfsu_ai_track_tests -j6: exit0.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_ai_track_tests|nfsu_speed_blur_tests|nfsu_car_contact_tests)$': 3/3 PASS.
- nfsu_intro_grid_viewer with retail root, unique race-r2 prefix, 300
  traffic-impact: exit0, six hull cases PASS (mode uses180 frames internally).
- gl-split-check.py versus saved parent shader: all four passes byte-identical.
- build-split.sh: real Switch SDK build exit0, seven selected package/shader/
  sample-contract tests PASS. Diagnostic option remains ON in build-switch.
- New/modified source and path ASCII scan PASS. Full matrices not rerun,
  following owner's targeted-check direction; no independent review requested.

Sealed candidate: build/ug0-contact-ai-r1/nfsu-blur-contact-r2.nro.
Exact identity, dirty source path hashes, checks and limitations are in
blur-contact-r2-manifest.json. Source patch is blur-contact-r2.patch; new
shader files remain in this owned tree. Do not overwrite the sealed artifact.
No r2 upload or hardware acceptance yet. Netloader question is pending.

Next: confirm whether r2 restores vertical variation at stage2 on Switch;
continue original history/catch-up binding and the remaining contact lifecycle.
All three owner-reported problems remain open until their runtime acceptance.

### r2 upload and history continuation

Owner confirmed current Netloader and authorized r2 upload. The exact sealed
r2 artifact was hash-checked before sending; no foreign nxlink was present.
Receipt: build/ug0-contact-ai-r1/upload-blur-contact-r2-run1.json.
Log: nxlink-blur-contact-r2-run1.log. Await one active NOS capture and owner
visual feedback. Upload is not acceptance of blur, collisions or difficulty.

Original history producer read is retained in catchup-history-original.txt
and its reproducer catchup-history-read.py. Reference430758 samples human
absolute speed against the selected route speed byte and pushes a ratio
through584270 into the ten-entry history object constructed at42B557.
Next inspect584270 and the route/category selector before porting the history
producer; preserve original timer, no-road and zero-route-speed behavior.

### r2 hardware rejection and collision code comparison

Source inspected: 91a39df6cd2e6157b5850e9b4a96e492562a7c55.
Owner completed the r2 run and rejected both collision behavior and NOS blur.
The nxlink session is terminal; do not poll or resend to that old session.
The one-frame hardware probe has distinct rows at all five stages, unlike the
prior stage2/3 row collapse. This verifies that sampled technical boundary
only; it does not override the owner's visual rejection or establish parity.

The owner requested collision code/original comparison instead of another
video. Findings and bounded native comparison are now recorded in
docs/CAR_COLLISION_COMPARISON.md. Production still uses box geometry, omits
the original strong-hit state callback/record lifecycle and remaining-time
continuation. Same-geometry isolated soft and strong cases distinguish the
callback gap from the shape-query gap. Strong native cases enter state1;
the production impulse sequence stays state0 and yields different momentum.
The original itself can add vertical/angular impulses; do not replace this
with a blanket no-rollover or vertical-velocity clamp.

No production source changed during this comparison. Evidence is local under
build/ug0-contact-ai-r1; original disassembly and probe names/limits are in the
comparison document. The earlier penetrating probe's unmapped read remains
UNKNOWN. Full matrices were not rerun for this documentation-only checkpoint.
Next implement the original geometry/time flow and orientation-aware persistent
crash lifecycle, then test player/rival and player/traffic together. Difficulty
catch-up and NOS appearance remain separate open work; no new upload is ready.


## Collision implementation in progress, after45b5022

Owner authorized implementation of the three confirmed collision gaps, solo.
Local uncommitted source in the existing owned combined worktree; no upload,
merge or publication. The blur and difficulty issues remain open and untouched.

Implemented: selected CV hull vertices and dual-feature distance witnesses;
46B260 origin-direction deep separation; generalized465D00 axes/support callback;
persistent actor state and pair records with original last-touch expiry;
42A530 recovery; continuation through existing player/selector3 body components.
The continuation preserves world clocks and route events. Failure restores the
pair actors, component state, record and seed.

Evidence: tools/contact_callback_native.py and tools/contact_state_native.py
execute authenticated PC USA1.4 original code on synthetic boundary fixtures.
Six rotated/support/gated callback cases agree within the existing2e-5 numeric
criterion; ten recovery state/timer cases match. Pins are in
 tests/car_contact_rotation_pins.inc. Neither is a whole trajectory proof.
Two targeted CTests passed after integration, including both driver continuation
cases. The six retail traffic hulls remain inside the proving-ground wall.

New hull solver is an explicit port distance implementation, not a literal
46EA50 dispatcher. Four separated retail shape fixtures agree; penetrating
witness choices differ and require fallback separation. The endpoint admission
and fixed pair ordering still differ from the original queued scheduler.
Optional actor feedback and crash race events are outside the numeric callback.
A gameplay collision probe with18 actual player/traffic pairs is running next;
then run the real UG0 route before considering an NRO. No hardware confirmation.
Full test matrices not run under the owner's targeted-validation direction.


## collision-flow-r3 sealed candidate

Local NRO: build/ug0-contact-ai-r1/nfsu-collision-flow-r3.nro
SHA256: ce689e28ba697dfbd247045b65940cbad2b4ec3de0da033d24ccbf682d46ade7
Size:3994205 bytes. Source/input/test receipt:
build/ug0-contact-ai-r1/collision-flow-r3-manifest.json.
This checkpoint postdates that receipt; production files are unchanged.

Final targeted CTests: car_contact, ai_track, race_session PASS3/3.
Switch SDK/package/shader checks: PASS7/7, sdk-contact-r3.log. Blur GPU readback
probe OFF; the rejected visual effect itself is unchanged. Native callback and
recovery reproducers rerun successfully with fresh final-r3 JSON outputs.
Hull native re-run succeeds; four separated cases pinned in ai_track_tests.
ASCII source/path/automation scan PASS; HEAD blob/path/mode scan PASS.

Expanded checks were NOT all green: vehicle_tests FAIL23 and vehicle_track_tests
FAIL18. Parent vehicle.cpp relink with the same ABI and exact observer macros
reproduces byte-identical outputs for both; parent_vehicle_check.py and paired
logs preserve this narrower attribution. Initial comparison attempts failed
from command parsing and absent observer macros; those were not gate results.
No pins refreshed. Full Default/Release matrices not run.

The18 paired gameplay probes completed without refusal. Wall fixture PASS6/6.
UG0 profile9000 and assisted18000 frames exit0 but player_laps=0; therefore no
successful full-race completion claim. Three rivals finish the assisted run.
Strong hits still cause rollovers; native whole-episode parity remains open.

Next: owner hardware collision check on this exact candidate, especially the
previous glancing impacts; retain the unported original scheduler/query epoch
limitations in docs/CAR_COLLISION_COMPARISON.md. NOS visuals and difficulty
remain separately open. No Netloader upload, remote push or merge performed.


## Active r4 correction, source after767542f (local only)

Owner asked to finish actual original collision comparison, not send r3 as
parity. No upload or remote write. Current dirty source is owned by this chat.
Reproducer tools/contact_search_native.py executes authenticated46AE60 and
468FE0 on synthetic histories with original box and authenticated retail240SX
hulls. Eight finite cases match remaining time, iteration count, fallback
shift, distance and witnesses within the existing witness criterion. This is
not a whole-game or rival/traffic-model trajectory result.

Confirmed corrections: two-flag backwards time search, last midpoint returned,
linear matrix sampling (no quaternion interpolation), squared near-query gate,
warm feature indices within a search, no second admission rejection of a job
whose final midpoint is outside the contact interval. Nonclosing admitted jobs
still separate/resume as46B5E0 does. Original source limits20 jobs and2 repeats
now bound an integrated retained-order car queue with affected-pair requery.
Traffic advances before car contact admission and rigid activation precedes
response. Queue integration has not yet passed its updated runtime checks.

Evidence in build/contact-flow-r4: native-search.json/log, search-port.log,
selector-xrefs.txt, disassembled search/sample/scheduler/traffic functions.
Initial fixture failures at466870 were missing synthetic physics objects;
corrected by real selector3 vtable objects, no source patch or function stub.
Initial CTest failures include old tests that fabricated moving poses with
zero elapsed time or required no continuation on nonclosing jobs. These
expectations contradict the measured original; replacements retain motion,
vertical-placement and nonclosing-no-impulse coverage. Full old logs retained.

Remaining: matrix-rotation native pins, integrated queue/traffic checks,
whole selected-model contact comparisons and required ABI rebuild/SDK checks.
46EA50 feature reduction remains a port geometric implementation; live event0
world selector and full car/track interleaving are not proven. No full parity
claim. Next: run targeted queue tests and actual UG0 runtime with this source.

## r4 implementation and sealed test candidate

Supersedes the earlier r4 in-progress build/test status. Production source is
local on codex/draw-distance-original after767542f. Owner remains the hardware
acceptance authority; no upload, remote push or merge was performed.

Sealed NRO: build/contact-flow-r4/nfsu-collision-order-r4.nro
SHA256: dc5a00726fd4841a1b5702df9b7bcc64fac44ce5a938d746cd189230dc649220
Size:3986013 bytes. Embedded tag:collision-order-r4.
Receipt: build/contact-flow-r4/manifest.json. This checkpoint follows the seal;
production inputs are unchanged. Earlier failed probes/builds/tests are retained.

Validation on the sealed production files:
- cmake --build build --target nfsu_ai_track_tests nfsu_race_session_tests
  nfsu_car_contact_tests nfsu_intro_grid_viewer -j6: exit0 (build-epoch.log).
- ctest --test-dir build -R
  '^(nfsu_ai_track_tests|nfsu_race_session_tests|nfsu_car_contact_tests)$'
  --output-on-failure: PASS3/3 (tests-final.log).
- tools/contact_search_native.py <retail-root> <fresh-output>: exit0,
  eight original searches and five exact matrix/velocity samples. Fresh
  search-sealed.json; C++ pins pass, including last-midpoint/fallback cases.
- tools/contact_flow_native.py <retail-root> <fresh-output>: exit0, three
  original constructed/queued240SX episodes. Fresh flow-sealed.json; pins
  compare separated positions, linear velocity and current-hit impulse.
  Initial port comparison had an incorrect synthetic alternate-inertia flag;
  corrected fixture explicitly matches the original selected inverse tensor.
- nfsu_intro_grid_viewer <retail-root> build/contact-flow-r4/ug0-epoch9000
  profile (actual arguments separate prefix ug0-epoch and count9000): exit0.
  ug0-epoch.log reports player_laps=0 and outcome=running. This is a stability
  run, not successful race completion or a console FPS measurement.
- bash build/contact-flow-r4/build-switch.sh: exit0; SDK rebuild and selected
  NRO/embedded-shader/GPU-contract/shader checks PASS7/7 (sdk-final.log).
- Modified file bytes, new path names and automation bytes ASCII PASS. Scripts
  ran from the actual workspace. git diff --check PASS before checkpoint.

Runtime additions include record expiry/count publication for42A530, shared
traffic severity totals and retained endpoint actor-up axes during body rewind.
The old separate record sum is only a compatibility input for standalone calls;
production consumes the world contact record.

Unmet acceptance: owner console collision behavior/performance; live original
scheduler selector; exact full feature-dispatch equivalence on untested hull
poses; combined car/track/prop job scheduling. Full Default/Release matrices
not run. Historical broad test failures were recorded on the prior candidate,
not reclassified as passes here. NOS blur and difficulty remain open.

Next: validate this exact candidate on the owner's reproduced impacts, while
keeping the unresolved original geometry/combined-scheduler boundaries explicit.
Do not report complete original collision parity from these finite comparisons.

## Owner correction: r4 is not an acceptance candidate

The owner rejected delivery before original collision equivalence was established.
This supersedes the preceding "Next" instruction to request owner impact tests.
Head:84701d0. Production is unchanged; existing artifacts remain evidence of
partial implementation only. No upload, remote push or merge was performed.

Task status: incomplete. Do not offer collision-order-r4 as the requested result.
Targeted native comparisons and successful packaging do not establish traffic
and rival collision behavior across the full runtime path.

Remaining implementation/evidence: original feature selection and query cache,
combined car/track/prop contact scheduling, contact record ownership and actor
publication, and representative traffic/rival impacts through continuation.
Next engineering action: inspect VehicleTrackSession::resolve_body and its AI
call sites against the original world scheduler before further implementation.
Owner console testing is not a substitute for resolving these known code gaps.

## Collision completion continuation: r5 work in progress

Owner requires completion of known code gaps before another console candidate.
Local head remains84701d0; no push, merge or upload. Dirty production is not sealed.
Original474A90 policy and474DD0 stabilization were executed in17 synthetic
states using authenticated speed.exe. tools/ai_collision_native.py reproduces
these inputs; tests/ai_collision_native.inc pins them. AI policy, gravity
selection and stabilization are now connected. Targeted AI/race tests PASS3/3.
Pair queries now retain original472830 translation cache behavior. Their cache
and session ABI was rebuilt; targeted AI/race tests PASS2/2. Cache boundary
native tests remain to be added. Shape transform now uses original binary32
operation order. A36-case original query comparison before that last change
exposed deep-overlap feature witness differences; this is unresolved evidence,
not parity. Evidence is under build/contact-complete-r5, including failed probes.
Remaining: static-world/car/prop common scheduling, original feature reduction,
actor publication and zero-duration continuation, player collision aero policy,
then representative traffic/rival full paths. No Switch candidate this round.
Next: verify player state1 policy and continuation against original instructions.


## r5 verified policy/record/continuation batch, still incomplete

Production changes now also cover player477830 aero disable, separate record
allocation before closing-velocity calculation, active-record counts before
resumed42A530, and zero-duration collision completion. Ordinary VehicleModel
step still refuses zero time unless explicitly marked contact completion.
Selector3 now calls shared47AC00 rest after component completion, including
its second refresh and EAX0 exclusion of selector1 wheel-spin mutations.
Original execution: tools/contact_lifecycle_native.py <retail-root> <fresh-dir>
checks6 player policy cases,6 AI rest cases, clean/dirty rotation cache, and
one admitted separating pair with a real zero-impulse record. PASS; output in
build/contact-complete-r5/lifecycle-native-final. AI stabilization17-case
comparison remains in ai-native. All source instructions were unmodified.

Build: cmake --build build --target nfsu_ai_powertrain_tests nfsu_ai_track_tests
nfsu_race_session_tests nfsu_player_powertrain_tests -j6: exit0.
CTest exact four targets: PASS4/4, tests-policy-final-r2.log. The prior iteration
incorrectly admitted ordinary zero intervals; that failure remains in
 tests-policy-final.log and was fixed by the explicit continuation flag.
Fresh ABI dependencies were rebuilt after each model/header change.

Six full original synthetic240SX pair episodes are in impact-native. Comparing
the port response-only boundary against post-continuation originally exposes
state2/alternate tensor and rest differences; these are distinct epochs, not a
valid whole-path parity verdict. A write trace localized the extra original
momentum damping to47AD34..47AD77, now implemented in AI rest. Hard-offset
response differs by less than one float micro-unit; other geometry differences
remain. No new tolerance or updated trajectory pin was accepted.

Remaining: common static-world/car/prop scheduling, original feature reduction,
actual selected rival/traffic hull episodes, actor published pose ownership,
complete native continuations and Switch acceptance. No NRO issued or uploaded.
This batch is local WIP, not task completion. Next: static polygon job path.

## r5 tensor and actor-epoch correction, after eeeb61a

No delivery, upload, merge or remote publication. Owner still requires known
collision gaps completed before a console candidate. Current batch is local.
The collision adapter now shares584B10/585810 cofactor inverse tensors and
binary32 matrix products with the rigid body, including interpolated matrices.
Its velocity refresh uses the original200 speed limit and Z/Y/X accumulation.
tools/contact_tensor_native.py executes eight authenticated original cases;
tests/contact_tensor_native.inc pins all tensor, momentum, V/W outputs exactly.
The same test rejects changes to the original operation order without relaxing
existing criteria. Targeted tensor/player/contact/race tests passed.

An actual moving original queued pair (explicit .016s histories, a rotated
prior matrix) confirms469390 changes rigid position/axes while actor+60/+70/+90
remain at the endpoint until474730. Evidence: moving-episode.json/log.
The selector3 continuation now receives that separate endpoint snapshot for
474DD0, for rivals and activated traffic. Four selected CTests PASS4/4 in
tests-epoch.log after a fresh dependency rebuild. Original physics refresh
vfunc34 targets the component subobject; it is not an extra outer simulation.

Static polygon probe tools/track_contact_native.py executes original473480,
472950,46B8C0 and46AE60 on nine explicit floor/wall placements. Original
thickness is1.0; the search enables the extruded shape. track-native is query
evidence; track-search-native has a mislabelled job+8 remaining value (zero):
46AE60 returns the time rather than writing that field. The tool now records
the original46B020 stack result; a fresh rerun is required before using time.
The46B070 cached normal-side read is conditional on other-body kind1; kind0
does not reuse that stored value. Do not generalize its stored pair+274 value
to a kind0 cache policy. Static world common scheduling remains unfinished.

Next: complete static job search/response integration with the car queue;
retain the unresolved feature-reduction and selected traffic/rival comparisons.


## r5 static response and support-walk continuation

Local dirty work after eeeb61a; still no NRO, upload or completion claim.
Static kind3 response now uses calculate_track_contact_response, verified
against seven original46BAE0 prepared witnesses in static-response-native.
Its .6 default polygon friction (46A910) matters for tilted bodies; upright
friction remains .1. Runtime resolve_body now uses the shared response and
rigid momentum refresh; the old fan geometry and per-slice ordering remain.
Original43FF30 flag4 scales decoded polygon vertical coordinates by4. Four
actual TRACKB1003 records verify this; physical world conversion now applies
it without changing the raw parser. The fresh track-search-r2/original.json
also fixes the previous remaining-time reporting error. Track CTest PASS.
World-response selected run:4/5 PASS. VehicleTrack remains FAIL23, including
legacy double-operation tests, drop/rest/reset behavior and trajectory pins.
No tolerance was relaxed and no failed trajectory hash was refreshed.

Original472F30 uses CV adjacency and an ordered local-maximum support walk,
not an unrestricted first-index maximum. VehicleHull/RaceContactBox now retain
neighbor lists. Raw39202 alignment must match472B00: the first selected asset
run exposed its eight-byte padding in ECLIPSE and correctly refused loading.
The parser correction passed selected-hulls-r2: all selected assets loaded;
432 pair cases completed. That run is not an original parity verdict.
Stock240SX graph is from its authenticated CV records. tools/hull_support_native.py
executes16 original cases, including ties and two start vertices. Exact C++
index/point comparisons in tests/hull_support_native.inc PASS inside AI tests.
Original735FD4 default is0, not1; the new traversal honors that default.

36 actual472950 query captures with reducer inputs/outputs are preserved by
tools/hull_features_native.py in features-native.json. The remaining46EA50
port affine reducer still differs on deep witnesses, up to1.531467 in the
fresh query-support-comparison.json. These raw overlapping witnesses precede
separation; do not infer that value is a final applied arm error. Selected
traffic/rival full paths and common static/car/prop scheduling remain open.
Next: verify all selected assets with retained graphs, then correct feature
reduction and queue geometry with original-backed final contact evidence.


## r5 ordered feature reduction and contact separation

Local source continuation after eeeb61a. Replaced the double affine minimizer
with the recovered46EA50 ordered point-edge, edge-edge and face-point dispatch.
Search direction is retained independently of the witness difference. On zero
separation the original preserves prior witnesses instead of recomputing an
arbitrary overlap minimizer. Float operation order, endpoint retention, face
projection and fallback separation now follow original store boundaries.

Evidence in build/contact-complete-r5:
- features-native.json:102 original reducer steps inside36 full472950 queries.
  The scratch reducer reconstruction matches all numeric outputs exactly after
  correcting X/Z/Y face normalization and pre-subtraction distance squares.
- query-feature-comparison.json: all36 full C++ query outputs match binary32
  original distance and both witnesses exactly. tests/hull_query_native.inc
  retains these outputs; tools/hull_query_pins.py reproduces the fixture source.
- tetra-native.json: the existing tetrahedron test used a double analytic
  distance. Actual original472950 gives1.443375587463379 and witness coordinates
  .6666667461395264; the test now checks those exact native values.
- impact-separation-comparison.json: all6 original queued pair first impulses
  match exactly, including hard head-on, hard offset, rear, side, glancing and
  a non-closing vertical case. This is a response boundary, not complete
  continuation parity. tests/hull_impact_native.inc preserves those impulses.
- tests-feature-r4.log: AI track and race session PASS2/2 after dependency rebuild.
  tests-feature-r1.log registered no tests (wrong regex), not a pass.
  tests-feature-r2.log retains the initial tetrahedron expectation failure.

Remaining: original static geometry/common queue, full selected traffic/rival
continuations, VehicleTrack failures and hardware acceptance. No new NRO or
upload; no complete collision parity claim. Next: selected actor prerequisites
and common world contact scheduling. All changes remain local and unaccepted.


## r5 selected moving continuations and force epoch

Local changes after bd2b478, not uploaded or accepted. Original46A080 refreshes
components and prepares forces before the outer4774A0/474A90 policy latch.
The port previously discarded that first prepared derivative and recomputed
with the new collision policy. Human and AI continuations now preserve the
old-policy prepared force for that first slice and refresh subsequent slices
normally. Human remaining-time subdivision follows477D56: trunc(dt/float
0.004399999976158142)+1, not the preceding solver interval.

Corrected fixture identity: selector0 is disabled, not human. Earlier selected
selector0 probes activate AI3 on impact and cannot support human continuation
claims. The new probes use selector1 for the player,3 for rivals,2 for traffic,
explicit RACING stage4, normal-mode byte78A345=0 and a zeroed human profile.
Selected physical levels are passed to original575960 after57F2B0; this checks
that physical-input boundary, not the full campaign visual/adaptive setup.

Fresh evidence in build/contact-complete-r5:
- selected-human-moving-final-1..9.json: original queued response and resumed
  dynamics for INTEGRA against three selected rivals and six traffic models.
- selected-human-moving-final-comparison.json: all first impulses and collision
  states match; maximum component difference ranges from2.384185791015625e-7
  to3.814697265625e-6. Small remaining inertia/rounding differences are not
  declared exact parity. The pose lies outside ground geometry; shared static
  world admission and simultaneous impacts are not exercised.
- build-force-epoch-r1..r5.log: dependency rebuilds completed. AI track test
  passed in tests-force-epoch-r2.log before final aero/timestep edits; rerun due.
  tests-force-epoch-r1.log found no tests and is not a pass.

Remaining: original static shape witnesses and common static/car/prop queue,
VehicleTrack failures, full selected live setup, required build checks and
hardware acceptance. No new NRO is ready. Next: preserve the regression and
replace the fan approximation using authenticated static-shape behavior.


## r5 static shape/search and retained support correction

Original473480/473A90 polygon support is now available in query_race_track_polygon.
It preserves the triangle/quad decision, strict first maximum, lane-specific
float dot order and4729AC feature invalidation when expansion changes.
The nine controlled original poses match both base and expanded distances and
witnesses exactly. Native467CA0 only expands for kind1 objects, NOT kind0 cars;
search_race_track_contact therefore keeps the thin face for car contacts.

A reproduced cached-query defect was fixed: after refreshing a retained feature,
472FBC starts at the last refreshed vertex ID. The port reset the hint to0,
which selected a different support on the shallow floor continuation. Human
sample468FE0/4692F0 interpolation now uses007F binary32 operation boundaries.
All five admitted controlled static searches now match remaining time, final
witnesses, distance and separation exactly. The four other cases are correctly
not admitted. tests/hull_polygon_native.inc stores original outputs from
track_contact_native.py, including the expansion/base/search sequence.

Evidence: build/contact-complete-r5/static-query-comparison.json,
static-search-comparison.json, static-feature-native-r1/original.json,
static-port-reducers.log and static-reference-native.log. The latter locates
body+120 initialization at468D37/468D43/468D58 and body+110 refresh at468F51.
This is the shifted hull-bounds midpoint transformed by retained body pose.
Native4663D0 excludes kind3 from the hard-hit state1 callback; no speculative
static launch impulse should be added. Existing prepared kind3 response remains.

The new static search API is not yet connected to race world scheduling. The
old fan adapter still runs in VehicleTrackSession::resolve_body. Next: original
polygon admission and unified scheduling, then the outstanding VehicleTrack
failures and hardware. No NRO is ready; task remains open. Changes local only.


## 2026-10-15 common world queue work (uncommitted after 2fa6d6b)

Owner scope remains collision recovery for rivals and traffic. No release,
upload, push or merge performed. Local changes are in this owned worktree.

- RaceSession now owns a common car/static queue and defers the old actor-local
  static fan during race motion and contact continuations. Original body
  cylinder, history-side reference and plane admission were recovered.
- Native track-admission-r1 executed nine original 468A50/469A00 cases;
  ai_track_tests pins their exact results. Targeted AI/Session tests passed.
- Controlled static-world-native-1.json executes original 420380 with retail
  INTEGRA/ECLIPSE handling/hulls and one synthetic vertical wall.
  static-world-port-r2.log exercises the production RaceSession queue: both
  selected jobs, search times, impulses, positions and linear velocities match.
  A tiny player angular-x difference remains in the inertia producer.
- The comparison exposed AI aero reading fresh wheel support instead of the
  published actor support used by 474B87. The latest source fixes that epoch.
- A 2400-tick PC UG profile run completed without runtime refusal. This is
  neither race completion nor hardware acceptance.

Remaining: mixed car/car plus static sequence, traffic continuations, original
polygon-cache/order limits, full validation and hardware acceptance. Props
remain a separate queue. Prior standalone VehicleTrack failures are not closed.
No full original-parity claim. Next: exercise the mixed collision sequence.


## 2026-10-15 continuation publication and local response tensor (uncommitted)

Owner requires completion of original-backed traffic/rival collision behavior,
solo, without another partial NRO. Work remains in this owned tree; no new
console build/upload, push or merge performed.

Confirmed and implemented:
- Original4774A0 calls4789D0 after contact continuation. Publish fresh human
  wheel support, gear, owner speeds and RPM after the resumed substeps. Old
  support4 survived a wall response and changed the next46C425 contact arm.
- AI474B87 aero reads the previously published actor support; the component's
  freshly calculated wheel flags are published later by474730.
- Original46A0C0 returns LOCAL inverse inertia for46BA50's response denominator.
  World tensor remains required by58593F for velocity refresh. Separate the
  two tensors in ContactBody; preserve generic prop boundary behavior.
- Original473619 creates an expanded polygon;468160 calls467DB0 to clear it
  after the first admission query. History refinement uses the thin polygon.
  Track this shared shape flag across pairs instead of starting every query
  on the thin face. This latest correction is being validated.

Evidence: build/contact-complete-r5/human-outer-end.txt, human-publish.txt,
all-world-front-1..9.json, all-world-back-1..9.json, feature-world-native-1.json,
flags-world-native-1.json, expansion-473480.txt, pair-process-expansion.txt.
The native runner executes original420380 and the original response/continuation
with authenticated retail hull/handling inputs and explicit two-body poses,
one wall, no ground, and bounded external service shims. It is not a whole
race/traffic spawning or performance comparison. Explicit native inertia
inputs isolate this comparison from the legacy table builder's rounding.

Before polygon flag correction, world-local-tensor-r2.log reports nine geometry
failures (first nonclosing back-wall witness, one per selected opposing model).
All eighteen final position/velocity/angular-velocity/state vectors and queue
order/time already match exactly. Do not report this as an all-pass test.
Initial build-world-local-tensor.log failed aggregate initialization; appending
new ContactBody members fixed ABI source compatibility; r2 rebuilt dependencies.

New production-queue regression: tests/race_world_contact_tests.cpp and native
pins include, registered retail CTest. tools/race_world_contact_native.py is the
reproducer, still needs evidence metadata cleanup. Remaining: current witness
check, queue record retirement and candidate lifecycle, actual traffic activation,
required full gates, runtime stability and hardware acceptance. Existing
standalone VehicleTrack failures remain unclosed. No arbitrary tolerance or
blind baseline refresh authorized. Next: read world-expansion-r1.log and fix
any remaining discrepancy before broad validation.


Polygon expansion validation: world-expansion-r1.log now reports zero failures
for the eighteen native world-queue cases, including all contact witnesses.
Correction to earlier evidence commentary: the component refresh dispatch is
469460 (body vtable+34), not46A080 (mass getter, vtable+58). Source comments fixed.
Queue record expiry moved after response processing, matching420720; an old
record touched again this frame must retain its accumulation and new timestamp.
Full Release build/gates running; no acceptance claim from these bounded pins.


2026-10-15 runtime/gates: world-runtime-r2.log completed6000 frames with all
three rivals and traffic,724 car impulses. Player route follower did not finish
(lap0); this is not owner play acceptance. No console FPS claim from PC timings.
Full Release configure was already present; full build succeeded. Plain CTest
returned156/200 passed,44 failed, no retail skips. Failure log is
build/contact-complete-r5/ctest-full-r1.log. Queue expiry edits happened during
that run; rerun affected targets after final build before any claim.
Required gates are NOT complete. No baseline refresh performed.

Created owned detached baseline at build/contact-baseline-r5, pinned2fa6d6b,
for the concrete preexisting stability/drop failures. Built and ran
nfsu_vehicle_track_tests and nfsu_stability_tests there: both fail. Baseline
has the same23 VehicleTrack assertion categories and planted-damping failure.
Drop final speed differs slightly (new1.479358324 vs parent1.472729491), so do
not call whole dynamics unchanged. Native exact collision tests use specified
inputs and do not discharge the ordinary drop criterion.

Native world reproducer now has explicit fixture selections and reports
retail SHA identities, PC=24, external boundaries and completion. Does not
export CV assets. Source parsed successfully; final smoke remains.


## r5 ordered actor traversal and actual traffic adapter

Continuation after83c6014, local only. Original468680/420487 traverses bodies
by upper-X, not canonical actor index. Corrected initial pair insertion and
retained pair orientation. Reversed traffic/player ordering now uses the
external response with the player as body B and continues both correct times.
Ordinary traffic advance moved immediately before the world queue, preserving
runtime sequence and permitting an explicit queue-boundary test.

The original reproducer no longer prescans A/B before420380. Fresh eighteen
cases are numerically unchanged. Native static fixture now decodes one cache
polygon through441B90 and lets469810 create its shape. Again all eighteen
existing outcomes are unchanged. Four reversed ECLIPSE/PANELVAN cases added.
The first reversed failure exposed a TEST defect: reusing the proving ground
quadtree omitted negative-X cells. The test now builds its wall's actual tree.
Do not describe that failure as an original plane/refinement defect.

All22 native cases completed with retail identities unchanged. The port test
now routes all six traffic models through RaceTraffic, not a rival adapter.
Exact queue order, remaining times, witnesses and final state vectors PASS.
Evidence: cache-owned-native-r1/manifest.json and captures,
traffic-adapter-test-r4.log, order-targeted-final.log (6/6, retail fixtures),
build-order-final.log. The20/22 interim failures and setup compile/name errors
remain in traffic-adapter-test-r1..r3 evidence; they are not passes.
PC6000-frame runtime completed in world-runtime-r3.log. This is not a player
finish or hardware performance verdict. Required full gates remain unresolved;
previous full Release failed44/200. Static cache160/shape48 producer ordering
and temporal lifetime, props co-scheduling, hardware and ordinary stability
remain explicit limits. Next: close the original polygon candidate producer
rather than claiming that the one-polygon queue fixture proves it.


## r5 polygon cache and grounded response follow-through

Local changes after83c6014; task remains INCOMPLETE. Original4407B0 retained
rectangle,42681B radius,160-entry admission/flag grouping and469810 cap48
before sorting now implemented. Six original cache updates and the53-wall
cap fixture match exactly (polygon-cache-native-r4.json). Build-cache-r2
succeeded; cache-targeted-r2 reports6/6, but other executables must be rebuilt
before treating them as current. Static shape lifetime follows pair references.

New grounded original fixture (floor-world-native-r1) exposes38 mismatches
across front/back SUV wall episodes. These were absent from22 no-ground cases.
Do not claim ground/collision parity. Original46B73F..46B762 explicitly refreshes
both components BEFORE and AFTER impulse; port currently refreshes only after.
Next: restore pre-impulse force refresh and distinguish remaining fixture
initialization differences from production defects. All failed evidence retained.
No current NRO, hardware acceptance, source push or merge.


## r5 component refresh correction in progress

Original46B73F..46B762 refreshes both corrected sampled bodies before impulse,
then both again before either outer continuation. Added synchronous callback
in RaceContactContext, VehicleTrack/RaceTraffic refresh adapters, deferred
actor publication, and explicit force-refresh interval. Original6F0738 is
shared between bodies, not a permanent zero or per-body interval. World now
retains the last continuation slice for the next job. Original traffic grip
is zero before first474D40 outer update; this activation epoch is retained.

build-refresh-r5 completed. Scratch air-refresh-r5:22 cases PASS unchanged.
Cold grounded cases still fail35 assertions; not accepted. Investigation
found the ORIGINAL FIXTURE omitted actor+478 initialization:4261BF copies
its zero to human grip, then477775/477AFE zero all tyre grip on first outer.
Cold front wheel axes also differ before an ordinary outer call. Therefore
floor-world-native-r1 is not an aligned ordinary-ground parity fixture.
Warm fixture now supplies explicit ordinary grip1 and an outer zero-time
initialization; raw global6FFDD8 additionally applies0.85, while existing
port tyre policy uses1. This is a separate known tyre policy gap, not proof
of another collision impulse defect. No blind tolerance/pin update.
Evidence in floor-trace-native-r4, floor-warm-native-r1..r4 and all failed
floor-*-port logs. Tiny SUV-only residuals point to the existing wheel ground
query arithmetic (first compression identical, later values differ by ulps).
Task remains INCOMPLETE. Current dirty changes are local; no NRO or upload.
Next: align the declared tyre-policy fixture inputs, verify the corrected
contact force epochs, then run coherent runtime/gates. Do not claim whole
original handling parity from a synthetic queue fixture.


## r5 grounded continuation correction

Local source after83c6014, solo owner-requested collision work, INCOMPLETE.
Confirmed and corrected traffic activation469FB0 before rewind; human46ABD0
rigid snapshot refresh; retained strut rate58867F at intervals<=.0001;
AI preload outer epoch and absence of human yaw cap; selected-plane height
440A79/projection587106. The port ground-query selection remains an adapter.
Added ordinary rear-grip4773F0/477AFE, latched per outer call and retained in
pre/post-impulse refresh. Decoded setup280/284; published owner slip is retained.
A grounded front/back SUV episode now matches original007F queue geometry,
positions, velocity, rotation and actor state exactly in floor-grip-r3.log.
This fixture explicitly uses grip1 instead of retail global0.85 and initialized
neutral/wheel bases; it is not entire handling or live race parity. Prior failed
outputs remain under build/contact-complete-r5. New test and reproducer are
race_ground_contact_pins.inc and race_ground_contact_native.py.
Next: all24 queue cases, targeted runtime with real actors, required gates,
then Switch packaging. No current NRO or hardware result; no push or merge.


### r6 verification and remaining failures

Grounded SUV both cases: exact original state and queue PASS in floor-grip-r3.
Tracked generator reproduced native results in ground-reproducer-r1. Extended
same finite floor fixture to three actual rival models plus SUV, both walls:
ground-rivals-r1, eight cases. Corrected fixture low_slip to the decoded original
constructor performance helper (previous constant1 mismatched CELICA/SUPRA).
Remaining world-ground-rivals-r3:15 scalar failures: one human ECLIPSE angular
ULP; several human CELICA/SUPRA ULPs; CELICA physical velocity differs about.007.
All22 old air cases and all queue geometries still pass. These failures remain
in the tests; no tolerance or native pin was changed to hide them.
Original owner heading478AA0 uses truncated components*65536 and564FA0 integer
arctangent, unlike velocity564DB0. Added player_heading_data.h / helper. This
removed most rear-grip mismatches. Nine original4773F0 vectors were added to
player_powertrain_tests. Their original output is rear-grip-native.json.

build-full-r6 succeeded. ctest-full-r6:154/200 pass,46fail, no retail skips.
Previous full had44fail; two added failures are request_gate captured-prefix
operands (three) and player_powertrain (three). The latter includes obsolete
small-dt compression-rate refusal: original retains rate when dt<=.0001.
Other two fail labels need diagnosis; do not blindly refresh historical pins.
New helper vectors themselves pass. Default full gates not yet run this delta.
world-runtime-r6 completed6000frames and896impulses. world-complete-r6 ran40000
frames but FAILED autopilot finish: player lap1, SUPRA lap1 stalled; two other
rivals lap2. No simulated crash/refusal. This is not live hardware acceptance.
Latest source after full build has heading helper + extended test only; direct
world-ground-rivals-r3 includes current vehicle/powertrain overrides against
fresh ABI libraries. Next: isolate CELICA grounded continuation, resolve human
ULPs, finish necessary regression updates, inspect stuck runtime. No NRO issued.


### r7 grounded response verified; live setup follow-through

Local source after83c6014, task INCOMPLETE. Original565230/5ABDAF and46C425
round arm transforms and support projection in binary32; removed accidental
double intermediates in car_contact.cpp. All22 air and8 grounded traffic/rival
queue cases now match exactly (world-ground-rivals-r4.log). Grounded rival
fixture now explicitly initializes first gear/automatic request, matching the
port fixture; old native constructor neutral was an input mismatch for CELICA.
New native output ground-rivals-active-r1 changes only that continuation.
No production AI gear policy was tuned to match a fixture.

Original-backed current request-prefix records added separately; all34 older
records remain. Corrected first-refresh epoch and retained strut-rate tests,
including a nonfinite retained-rate refusal negative. Stock handling byte mask
includes newly decoded rear grip fields; all368 encoded bytes were compared
to authenticated stock retail record, not repinned from simulation output.
Release build-full-r7 completed; ctest-full-r7:156/200 pass,44fail, no skipped
retail checks. Player powertrain, request gate and world contact targets pass.
Other failures remain unresolved; no full gate or release pass is claimed.

world-complete-r7 ran40000 frames, extended45000: all three rivals reach lap2;
player autopilot does not finish. Four explicit reset inputs in extended run.
This is not a hardware result or evidence of full traffic liveness (one parcel
van is stopped at the final sample). No NRO, push or merge this revision.

Next confirmed gap: world comparison overrides the legacy table-builder
inertia with original inputs. Runtime make_race_vehicle_spec still decodes
binary64 table inertia. Original5762FC..57635A under007F uses binary32 for
squares, sums and multiplies. Restore this runtime producer and remove the
fixture override; keep serialized retail/table reconstruction semantics.


## r8 runtime producer and console candidate, 2026-10-16

Owner task is not closed: hardware collision acceptance and general red gates
remain. Solo implementation; no reviewer delegation, source push or merge.
Runtime make_race_vehicle_spec now uses original576250..57635A binary32 mass
and inertia arithmetic. Serialized apply_vehicle_tuning / vehicle_box_inertia
remain unchanged. World tests no longer overwrite inertia with native answers;
they check the real loaded producer and then the queue/response/continuation.

Verified on the dirty successor of83c60142fb77dd7ba8cfcd20f67e24ca48fd4f05:
- world-runtime-producer-r8.log: all30 finite original world episodes PASS,
  including six traffic models, three rival models and8 floor/wall episodes.
  Queue order/time/geometry plus final position, velocity, angular velocity and
  collision state match exactly. This is NOT arbitrary-input/full-game parity.
- world-old-producer-negative-r8.log: legacy runtime producer causes126
  assertion failures, including30 producer failures; summary present, not a
  crash. No arbitrary tuning or wider tolerances used.
- runtime-inertia-r8.json: INTEGRA eight weight/item mass/inertia cases equal
  original576250..57635A, no hooks. tools/race_inertia_native.py independently
  reproduces eight retail240SX records in tests/race_inertia_native.inc.
- request-tool-final: original caller request3 / equality2 / below3. Old34
  records retained. Both new Python tools smoke-tested from actual workspace.
- world-complete-r8.log: all four racers finish at tick48907,630 pair impulses;
  test-driver player used ONE ordinary reset input after five seconds stuck.
  Budget90000; run stops at finish. Earlier40000/45000 failures preserved.
  This is assisted host completion, not unassisted driving or Switch FPS proof.

Build/gates (retail root is the authenticated local USA1.4 installation):
- cmake --build build -j6: PASS (build-full-r8.log).
- ctest --test-dir build --output-on-failure -j6:156/200 PASS,44 FAIL,
  no skipped retail checks (ctest-full-r8.log).
- cmake -S . -B build-mips-default -G Ninja -DNFSU_RETAIL_ROOT=<retail>;
  cmake --build build-mips-default -j6: PASS.
- ctest --test-dir build-mips-default --output-on-failure -j6:155/195 PASS,
  40 FAIL, no skipped retail checks (ctest-default-r8.log).
- Independent pre-change tree2fa6d6b Release full build/CTest:154/198 PASS,
  same44 failed test names (ctest-baseline-r8.log). This does NOT make those
  failed gates green. Stability/drive/replay/render/asset debt remains.
- Runtime inertia contradicts old zero-level/serialized-inertia equivalence
  assertions. Replaced ONLY these with original-backed inertia pins, keeping
  all other stock bits and deterministic controls. Final affected reruns:
  cmake --build <build> --target nfsu_performance_levels_tests
  nfsu_performance_replay_tests -j6; ctest --test-dir <build>
  --output-on-failure -R '^nfsu_performance_(levels|replay)': both configurations
  still fail3/5 at PREEXISTING synthetic replay pin/exhausted recording, after
  passing new producer assertions. No trajectory hash was blindly refreshed.
- bash build/contact-complete-r5/build-switch-r8.sh: SDK build PASS, selected
  NRO/shader/GPU contract checks7/7 PASS (sdk-r8.log).
- HEAD ASCII scan, working-file byte/path scan, forbidden tree modes and
  git diff --check PASS. First Bash scan lacked grep on PATH; rerun with
  /usr/bin succeeded. First baseline redirection used wrong relative depth;
  no build ran there; corrected run is the one recorded above.

Candidate: build/contact-complete-r5/nfsu-collision-native-r8.nro,
4063837 bytes, SHA25602ae1bd6d95919bb3ff06132d57a42e522b926a77d41f449d161a0c40144d9ac,
embedded tag collision-native-r8. candidate-r8.json records source identities.
No upload performed and current Netloader readiness is unknown.

Limits: floor fixtures explicitly use ordinary base grip1 and initialized
human neutral/rival first-automatic inputs; original ground-query selection,
full campaign setup/difficulty, props in the common queue and arbitrary live
trajectories are not established by these fixtures. Existing grip-policy and
spawn adapters remain named in source. NOS visual acceptance remains separate.
Next: owner console control of the previously rejected traffic/rival impacts;
retain logs and reconcile any remaining failure against the now-frozen source.
Full original-game equivalence and full release acceptance are NOT asserted.

## r8 console upload, 2026-10-16

Owner confirmed Netloader readiness and authorized upload in this chat.
Uploaded the immutable collision-native-r8 artifact above using nxlink -s.
Upload log reports starting server / server active; stderr empty at check.
Receipt: build/contact-complete-r5/upload-collision-native-r8-run1.json.
Log: build/contact-complete-r5/nxlink-collision-native-r8-run1.log.
Process identity: PID38460, started2026-10-16T06:23:21.9415575Z.
No prior nxlink process was running. Source remains10865bcbadf0490b1a07857715fa86f6ee68c713.
Next: capture owner traffic/rival impact drive and inspect this log.
Hardware collision acceptance remains pending; upload is not functional proof.


## Civic drift launch correction r9, 2026-10-16

Owner completed UG0 in collision-native-r8 and reported continued mismatch.
Clarification: impact sound disappears; owner deferred this and prioritized
drift startup. Audio debt recorded in root-ug0-debts-20261012.md.
The completed nxlink log contains event0 finish, zero resets, then event3
startup failure: selected car collision solid unavailable: CIVIC.
Reproduced the same failure with the existing nfsu_drift_track_check on PC.

Cause: CIVIC has two same-hash shapes in one80034020 container. The parser
read the second39200 header without16-byte alignment, overwrote selection,
and discarded the first valid shape. Original472B00 aligns every header and
appends shapes in file order;469C40 and472C60 select the first matching hash.
Corrected alignment, per-shape state and first-complete-match return.
Original loader/lookup executed on authenticated retail image without hooks:
civic-native-selection-r9.json records selected offset and geometry identity.
Two initial probe attempts refused missing executable ranges; they did not
produce a verdict. Successful run used the existing authenticated fixture.

Targeted verification only, as owner requested:
- cmake --build build --target nfsu_drift_track_check -j6: PASS.
- build/nfsu_drift_track_check <retail>: PASS, first track1301 frame, authored
  HUD animation, crowd lifecycle,1500 simulation steps and banked drift score.
- bash build/contact-complete-r5/build-switch-r9.sh: PASS, SDK build and7/7
  selected package/shader contract tests. Existing unrelated full-gate failures
  are not rerun or claimed resolved.
Candidate identity: build/contact-complete-r5/candidate-r9.json; NRO sibling
nfsu-drift-civic-r9.nro. Not uploaded. Hardware drift acceptance still pending.
Next: owner console validation of event3 on the existing CIVIC career save.

## r9 console upload, 2026-10-16

Owner confirmed Netloader readiness and authorized drift-civic-r9 upload.
Verified candidate SHA25658da9d2d6050c38dfcc8cb11f91b6e357c6aadfce4847ad5b9610d20573ee480.
No existing nxlink process was running. Upload completed; server active.
Receipt: build/contact-complete-r5/upload-drift-civic-r9-run1.json.
Log: build/contact-complete-r5/nxlink-drift-civic-r9-run1.log.
Process PID64012, started2026-10-16T06:54:31.8294454Z.
Source head d876814abc0500117e3f5b2e08bba1ad43580b68.
Next: inspect owner event3 launch and drift run; hardware acceptance pending.
Impact audio remains deferred by owner. No source push or merge performed.


## r9 hardware drift result, 2026-10-16

At d876814, owner completed event3 / track1301 on Switch with CIVIC.
Log confirms all four laps finished, drift results displayed, career reward
saved and return to menus. This confirms the Civic startup failure is resolved
on hardware. Full drift UI/original fidelity is not accepted.
Run summary and source-log digest: contact-complete-r5/hardware-drift-r9-summary.json.
The run reports one reset and a nonzero audio underrun counter; neither is
diagnosed by this record. No new investigation or runtime change authorized
by the owner's latest message: record incorrect drift-message appearance and
leave corrections deferred. Backlog updated in root-ug0-debts-20261012.md.
No running nxlink process remained at inspection; log ended after menus.
