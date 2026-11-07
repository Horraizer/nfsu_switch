# NOS and traffic owner follow-up

CHECKPOINT
role: worker
agent: Codex root, solo owner-directed implementation
updated_utc: 2026-10-14
assignment: Owner rejected NOS stretch, ambient blur, traffic routes/impacts and frame drops; supplied two videos and eleven screenshots.
phase: investigating
branch: codex/draw-distance-original
head: c7447f705b3b9137e4d61fa79ab9768ff64a8bb3
worktree: build/day20261008-root-combined
saved: Local only; no publication or upload in this follow-up.

## Confirmed evidence

- Frozen complete console log: build/nos-traffic-r2/nos-visual-r1-owner-run.log,
  SHA256 e31f480955ec59bc43cef65a8921c354dc6234ccf5ed61f57e919e7b9795d134.
- At c7447f7, first hardware run: mean present interval 17.933928 ms;
  second run: 19.229054 ms, 72 intervals above 33.333 ms, max 46.297187 ms.
  Second-run mean simulation 8.729962 ms and render recording 4.814875 ms.
  GPU timestamps were not enabled; these figures do not identify GPU cost.
- Owner media identity inventory: build/nos-traffic-r2/owner-media.json.
  Second video shows severe scenery stretch during NOS. Low-speed and stopped
  screenshots also show frame drops, so NOS alone cannot explain performance.
- Owner confirms BOTH wrong traffic routes and wrong impact response.
- Actual desktop GL execution of the current four blur shader passes produces
  normal radial blur, not the Switch vertical stretch. Evidence:
  build/nos-traffic-r2/gl-check.py, gl-result.json, gl-pass0..3.png.
  This does not validate Switch bindings, runtime inputs or camera parity.

## Local change and bounded validation

- Runtime and desktop preview use a NOS-only adapter: inactive NOS immediately
  resets the envelope and disables blur. Original arithmetic oracle is retained.
- New tests cover inactive high speed, activation, release and reactivation.
- cmake --build build --target nfsu_speed_blur_tests: PASS.
- ctest --test-dir build -R nfsu_speed_blur_tests --output-on-failure: PASS, 1/1.
- No corrected NRO exists yet; no console confirmation of the local change.

## Remaining

Find and correct Switch-only stretch; compare camera response with reference.
Traffic junction/lane selection and collision adapter remain under investigation.
Current collision uses SAT box witness and state0 impulse response, not complete
retail contact generation/hard-hit dispatch. Do not present it as full parity.
Do not repeat closed original arithmetic matrices or introduce tuning constants.
Next: inspect render target coordinate/binding state and reproduce traffic defects.
Owner retains hardware acceptance. No independent agents or reviewers requested.


## Superseding owner priority: frame drops first

Owner explicitly prioritized frame drops. NOS shape, traffic routes, hard-hit
response and style penalties are paused, not accepted or discarded. Solo work;
no reviewer/agent, publication or upload. Broad matrices were not repeated under
the owner's explicit minimal-test instruction.

Measured baseline: the second saved Switch run repeatedly needs two catch-up
simulation steps after missing the frame budget. Its contacts stage costs a
mean 2.285589 ms on sampled steps. This is CPU evidence, not a GPU timestamp.
PC decomposition identifies awake-prop terrain queries as the largest child
in the controlled intro replay. The owner route is not reproduced by this driver.

Implemented reuse of validated containing-leaf polygon candidates across a
prop's primary/fallback terrain rays. Exact scalar solver, fallback order,
vertex impulse order, rounding stores, timestep, forces and sleep rules remain.
Scratch arrays are sized once from authored hulls, with no fixed vertex cap.
Added opt-in contact child timers; Switch samples at the existing cadence and
prints only aggregate results. Children are not added again to parent time.

Evidence based on c7447f7 plus the local source diff, preserved in
build/nos-traffic-r2/perf-ab.json and its six uniquely named run logs:
- Alternating scalar/batch executables, three 6000-tick runs each.
- Median prop-motion CPU: 0.082902 -> 0.0600888 ms (27.5% lower on this PC).
- Other stage timings also vary; this is not an isolated CPU-cycle count and
  cannot be extrapolated into a console FPS percentage.
- All six race sequence, per-step prop-body sequence and three command
  signatures match. Prop signature excludes padding and pointers.
- The scalar executable used the original HEAD race_props.cpp, not a rewritten
  scalar approximation. The candidate source was restored and rebuilt after it.

Validation:
- cmake --build build --target nfsu_intro_grid_viewer nfsu_track_contact_tests -j4: PASS.
- ctest --test-dir build -R '^nfsu_(track_contact|speed_blur)_tests$' --output-on-failure: PASS, 2/2.
- Native scalar/batch retail intro replay: six completed runs, same signatures.
- cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=perf-props-r1: PASS.
- cmake --build build-switch --target nfsu_switch_race_nro -j4: PASS.
- ctest --test-dir build-switch -R '^nfsu_switch_race_(nro|embedded_shaders)$' --output-on-failure: PASS, 2/2.

Candidate: build/nos-traffic-r2/nfsu-perf-props-r1.nro, package.json records
identity and the exact source manifest. L3+R3 diagnostic overlay remains.
No console test or stable-60 claim. NOS-only gating is included from the earlier
local fix, but the Switch-only active-NOS stretch is still unresolved.
Next: repeat the owner's UG0 portable route through tunnel/gates, hotel and
Marquis theatre with console logging. Separate contacts children are now visible.
Compare the driving window and catch-up frequency; do not declare all drops fixed.


## Console lap received, 2026-10-14

At dd3fe9d7ac6b8e4467a52056e8a187f93777bc13, perf-props-r1 was uploaded
by explicit owner request and completed one UG0 lap (LAST 1:08.25, LAP 2/2).
Clean menu return and game exit; no crash. Nxlink run 71656 / PID 38616 is
terminal; do not reuse or poll the old process identity.
Frozen log: build/nos-traffic-r2/perf-props-r1-owner-lap1.log, SHA256
b8d3ca4391b72163c224984fb3e1b7371363895d21d0703278d050c62bb55877.
Analysis: build/nos-traffic-r2/perf-props-r1-owner-analysis.json.

3951 driving presentation intervals: mean 17.741101 ms (56.37 FPS equivalent),
p95 25.50 ms, p99 30.00 ms, maximum 34.128958 ms; 215 above 25 ms and four
above 33.333 ms. Previous runs had mean 17.933928 and 19.229054 ms, but their
trajectories/durations differ. Do not attribute the aggregate difference solely
to the prop optimization. Same measured portable clocks: CPU 1020 MHz, GPU
460.8 MHz, EMC 1331.2 MHz, height 540.

Sampled simulation means: rivals 2.864818 ms, player 1.685870 ms, contacts
1.590260 ms. Contact children: traffic motion 0.400714 ms, prop motion
1.136442 ms, prop contacts 0.040805 ms. Sampled render CPU: cull 1.318909 ms,
list 0.670030 ms, road reflections 1.273864 ms, visuals 1.368242 ms (children
of combine, not extra parent time). No GPU pass timestamps.
Worst periodic sample at race 0:41.71: 29.66 ms total, two simulation steps
costing 16.52 ms. Other catch-up examples at 0:28.39 and 0:32.71. These are
periodic samples, not the location of the absolute worst frame.

Stable 60 remains open. Next CPU target is rival terrain/contact work (largest
simulation stage), then repeated reflection/cube preparation. Preserve physics
and image fidelity; compare exact replay outputs before a hardware candidate.
No new upload without owner request. NOS/traffic correctness work remains queued.


## Additional owner collision report, 2026-10-14

Owner reports a rival launched away after a player impact during the latest
run, and repeats that traffic response differs from retail. Both remain open.
Source inspection confirms racer/racer and racer/traffic use the same
resolve_race_pair adapter: OBB SAT admission/separation and a projected midpoint
contact, followed by recovered state0 impulse arithmetic. The comment claiming
a tangential footprint clamp is not implemented. Original hard-hit callback and
persistent collision/recovery state are intentionally absent from this adapter.
These are confirmed fidelity gaps, not proof of the cause of this specific launch.
The aggregate performance log does not retain contact normal/point, both bodies'
pre/post momentum and orientation, so it cannot diagnose the launch by itself.
Do not hide the issue by capping upward velocity or weakening all impacts.
The prop query optimization changes neither this solver nor its coefficients.
FPS remains the owner's first priority; carry these two correctness reports into
the same physics investigation and reproduce the impact before changing response.


## 2026-10-14: physics wheel queries and asynchronous GPU totals

Owner keeps FPS first and requests original rival/traffic collision behavior.
Work remains solo, with focused checks only per owner instruction. No upload,
source push, merge or independent review was performed in this continuation.

Changes against dd3fe9d:
- Four AI wheel segments share only validated leaf candidates within one query.
  Each wheel keeps its own origin/end, solver, status, surface and arithmetic.
  No cached heights across steps, reduced physics cadence or changed force.
- Ordinary Switch racing samples the existing GPU total timer every 60
  presentations. It reads only after the existing slot fence; no new per-frame
  wait or pass barrier. Frontend overlay presentations do not record these
  queries. Offscreen profiling and explicit AB sampling retain their modes.
- GPU_PROBE prints totals on exit after pending results drain. Percentile -1
  means no samples or histogram overflow, not a finite upper bound. The sample
  tick range belongs to the current profile generation; target recreation and
  existing profile resets start a new window. Hardware timer validation remains.
- intro_grid_viewer pair-probe isolates selected UG0 racer/traffic pair response
  and writes explicit offsets, speeds, vertical translations and post-impact
  velocities to CSV. It is a diagnostic, not an original-parity assertion.

Evidence in build/physics-gpu-r2:
- wheel-batch-ab.json: four sequential 6000-tick PC runs, baseline/batch then
  batch/baseline. All car-state, prop-state and three command signatures equal.
  Mean rival CPU was 0.234101 ms baseline and 0.229656 ms candidate (1.90%).
  Individual paired reductions vary from about 0.54% to 3.23%; this is a small
  PC result, not demonstrated Switch improvement or a stable-60 result.
- selected-cars-pairs.csv: 432 isolated pair inputs on selected retail cars.
  Deep overlap with TAXI02 can move the player down 0.66254 m and the taxi up
  0.40519 m with zero applied impulse, even at rest. This proves artificial
  vertical separation in the port SAT adapter. It does not establish that the
  owner's exact flying-rival event took this path. Pitch from model reset is
  retained; overlap_x is the authored-box placement parameter, not exact
  oriented penetration depth.
- original-deep-r1/report.json and source.txt: two additional controlled
  original queued 240SX episodes at offset (1.5,0.2,0), stationary and closing
  at 30 m/s. Both produce one pair/job, no hit/application, unchanged positions
  during the response, and actor state 0 -> 2. This is not the same car/hull as
  TAXI02, nor proof that real collisions should be ignored. It reinforces the
  missing original geometry/state/scheduler integration. Full retail manifests
  match; authenticated source instructions and explicit fixture limits retained.
- Original response and current adapter inspection confirms the production
  path still uses OBB SAT admission/separation and omits original hard-hit
  state callbacks. No synthetic witness context was enabled in production,
  and no arbitrary vertical/impulse clamp was introduced.

Checks:
- cmake --build build --target nfsu_intro_grid_viewer nfsu_car_contact_tests -j4:
  PASS (existing unrelated compiler warnings retained in log).
- ctest --test-dir build -R '^nfsu_(track_contact|car_contact)_tests$'
  --output-on-failure: 2/2 PASS. The earlier track-contact target build/test
  also passed; new distinct-origin boundary/refusal cases compare scalar bits.
- SDK NRO build and two packaging/shader checks pass; package.json records the
  exact final binary and source manifest. No new hardware run yet.

Next: validate sampled GPU totals on the console, then target the demonstrated
CPU/GPU bottleneck. Collision correction must replace the SAT deep-overlap
response with original shape/contact admission and state handling, with matched
inputs, rather than changing a restitution number. Original state2 transition
and queued time search need to be carried into that adapter; whole live
scheduler, hard-hit recovery and owner collision acceptance remain OPEN.


## 2026-10-14: physics-gpu-r2 startup failure and diagnostic retry

Owner reports a crash after the intro movie, before menu, on the first
physics-gpu-r2 upload. The complete nxlink run1 log has no application text;
only upload/server/socket closure. This is a failed hardware attempt, not a
GPU result or proof of an out-of-memory failure. Keep its receipt/log/hash in
build/physics-gpu-r2. No source correction or binary replacement was made.

Owner authorized the same NRO with --frontend-startup-trace. Retry trace1
received flushed startup records and SD journal
sdmc:/nfsu-menu-startup-0001.log. It reached renderer.ready, music.start.after
and first frame.present.returned. The original crash has not reproduced under
this timing-perturbed diagnostic; it is NOT fixed. Logger remains active:
exec 35160; exact PID/start identity is in hardware-physics-gpu-r2-trace1.json.
Do not poll old terminal exec 94180 or claim the cause from its empty game log.
Next read the owner's trace1 outcome and GPU totals if a lap is completed.

Trace1 subsequently reached UG0 racing (frame600, race 0:07.47), with the
FPS overlay enabled by the owner and boost input logged. Logger remains live;
GPU summary is only available after exit. No startup fix has been claimed.


Owner clarification: Plus was pressed after the intro movie on run1.
Source confirms Plus -> IntroControl::quit -> Cancel at the intro boundary,
and Screen polling also throws Cancel on Plus during loading. Main catches
Cancel and exits normally without error text. Reclassify run1 as likely
user-requested exit, NOT a confirmed crash; the exact branch was not logged.
Keep the initial report and empty game log as history, not a known startup
regression. Trace1 remains the active GPU/CPU measurement run.


## 2026-10-14: completed physics-gpu-r2 drive and timer wiring correction

At 7d58b07 NRO physics-gpu-r2, owner trace1 completed a drive and returned to
menu, then exited. Frozen log and analysis are in build/physics-gpu-r2:
physics-gpu-r2-owner-drive.log and physics-gpu-r2-owner-analysis.json.
3627 presentation intervals: mean17.988399 ms (55.591 FPS equivalent),
p95 histogram27 ms, p99 32 ms, max36.851510 ms, 268 over25 ms, 13 over33.333333.
Prior drive was mean17.741101 ms, p95 25.5 ms, p99 30 ms; differing input/window
means no controlled performance gain or regression can be assigned.
CPU simulation mean6.957588 ms; record4.618944 ms; update0.624402 ms;
residency0.777585 ms; present CPU4.961552 ms includes command recording/waits.
Worst periodic sample at race0:30.26: work28.76 ms, simulation19.12 ms,
catch-up2. CPU stalls remain demonstrated; GPU bottleneck is still unknown.

GPU_PROBE had samples=0, invalid=0, dropped=0. Root found a concrete wiring
bug: race_main still passed scene context only when profile_frame was true.
Ordinary mode supplied null. Renderer recorded timestamps every frame under
its null-context fallback, then discarded them because default active=false.
Thus r2 did not implement the intended rare sampling and provides NO GPU
measurement. Its extra timestamp overhead has not been independently measured.

Correction physics-gpu-r3 passes scene_frame for every direct race presentation;
its sample flag now controls the cadence and active flag controls accounting.
GPU_PROBE_PROGRESS prints count/mean/max every existing periodic PERF update,
so missing samples can be detected within seconds of racing, before requesting
another complete drive. No extra waits or pass barriers were added.
New focused source-wiring check rejects the exact null-context regression and
an always-sampling mutation. It does not claim hardware timestamp verification.
SDK NRO build PASS; targeted package, embedded shaders and sampling contract
CTest3/3 PASS. No physics/render-quality change from r2; not uploaded yet.
Next hardware check must first confirm positive GPU_PROBE_PROGRESS samples
with invalid=0; only then use a complete drive for performance decisions.


## 2026-10-14: r3 hardware result and CPU reflection membership correction

At 5fb63f4, physics-gpu-r3 completed lap1 in 1:00.80 and returned to menu.
Frozen log/analysis: build/physics-gpu-r2/physics-gpu-r3-owner-drive.log and
physics-gpu-r3-owner-analysis.json. Logger exec97024 terminated normally.
3600 presentation intervals: mean17.172490 ms (58.233 FPS equivalent),
p95 19.25 ms, p99 29.25 ms, six above33.333333 ms. GPU scene span now has
60 valid samples, zero invalid/dropped: mean10.779013 ms, max12.936562 ms.
Sampling one presentation per60 does not exclude unsampled GPU spikes.
No coordinate/zone markers exist in this log; exact avenue/parking attribution
is still unverified. CPU frame at race0:39.18: sim8.04 ms, record5.63 ms,
command3.98 ms. This demonstrates a CPU budget overrun without catch-up.
The progress print was accidentally in panel mode only; final GPU data is
valid. Move it into the ordinary periodic block and guard that placement.

Owner authorized code investigation before another hardware test. Found a
repeatable CPU waste in road reflection selection: linear zone membership
was checked for every instance, including instances without reflections.
Reject reflection-ineligible instances first and reuse the membership result
for each consecutive section run. Cache lasts one query only; geometry,
transforms, draw order, zone policy and physics are unchanged. No new heap
allocation or persistent cache was added.

Desktop history at baseline5fb63f4 versus this source delta, using the existing
Release intro_grid_viewer 6000-tick profile scenario, baseline/candidate/
candidate/baseline order: reflection preparation mean0.115417 ->0.054950 ms
(52.39 percent reduction in THIS stage). All four full state sequences,
prop sequences and initial/moved/warmed command signatures match. These are
stationary render timings after the scenario, not Switch FPS or proof of the
parking root cause. Exact logs and comparison.json: build/cpu-hotpaths-r1.

Checks: native track-render build PASS; ctest --test-dir build
-R '^nfsu_track_render_tests$' --output-on-failure:1/1 PASS, including mixed
section runs, reentry and zone changes. Python switch_gpu_sample_tests.py
--root . PASS, now requiring the progress print inside the ordinary block.
SDK build PASS; Switch NRO, embedded-shader and GPU sample contract checks
3/3 PASS. ASCII blob/path scan and diff whitespace check PASS. Owner requested minimal
focused validation; no broad matrices or delegated reviews were run.

Remaining: hardware cost of this change, exact heavy-zone CPU attribution,
original collision fidelity. No new hardware acceptance or stable60 claim.
Next: finish SDK package; retain current quality and collect the next hardware
comparison only when the owner is ready. Changes and evidence remain local;
no source push, merge, publication or automatic console upload.

Packaging correction before delivery: the executable target refreshed ELF only;
the first package checks still inspected the old r3 NRO. That artifact and
receipt are preserved as stale-r3-before-package.nro and
package-before-nro-target.json in build/cpu-hotpaths-r1. Built the explicit
nfsu_switch_race_nro target, verified the embedded cpu-hotpaths-r1 tag and a
changed SHA, and reran the three package/shader/sample checks:3/3 PASS.
package.json now identifies the actual final NRO and source commit2d869cb.
No stale candidate was sent to the console. Future builds must name the NRO
target and verify the embedded tag before upload.


## 2026-10-14: combined CPU preparation batch, cpu-hotpaths-r2

Owner rejected a single small optimization and requested a coherent batch.
Solo implementation in the same owned tree; no reviewers or new assignments.
Base 8a3f113 includes the earlier section membership correction. Added:

- Lamp upload lookup searches backward with the same last-match semantics.
  Shared material/program/pipeline is prepared once for visible lamps only;
  each light retains its own identity and placement.
- Track selection captures layer and first-use bucket keys once per draw.
  Sort order still includes the original ordinal, including empty-group bucket
  ordering. Main view and all six cube views use the same corrected path.
- Cube draw dispatch partitions contiguous view-tag runs once per frame,
  retaining source indices for uniform addresses, shared view0 commands, and
  per-face order. Six faces and their original update cadence are unchanged.

The earlier section optimization plus these three changes form one candidate.
No collision-law change, quality reduction, lowered frequency, or GPU clock
change is included. Two origin-dependent contact cache attempts were slower
on desktop and removed. A smaller contact early-out attempt had no convincing
speed benefit and was also removed; src/track_contact.cpp matches HEAD.
All three attempted diffs and timings remain in build/cpu-hotpaths-r2.

Desktop measurement history: baseline8a3f113 versus this delta, Release
intro_grid_viewer with retail root, 6000 ticks and profile argument. Repeated
interleaved trials showed lower draw preparation cost. Final pair is
baseline-final.log / final-1.log; comparison.json stores hashes and analysis.
Full car-state and prop sequences and initial/moved/warmed command signatures
match. Render timing covers120 stationary frames after the drive; it does not
measure the console avenue/parking interval or full-frame FPS.
Final pair list0.0925833 ->0.0718917 ms; combine0.208533 ->0.180575 ms;
reflection child0.0575167 ->0.0441417 ms. Non-overlapping render stage sum
0.4192916333 ->0.3730836967 ms (11.02 percent less than the already optimized
r1 baseline). Children are not counted twice. No measurable physics gain is
claimed. Persistent sort-key storage costs81920 additional bytes in this
scenario, included in RaceGpuScene::cache_bytes().

Required focused validation under the owner's minimal-test workflow:
- cmake --build build --target nfsu_intro_grid_viewer: PASS, fresh dependencies
  after the private layout change; existing viewer range-loop warning remains.
- cmake --build build --target nfsu_track_contact_tests
  nfsu_draw_execution_tests nfsu_deko_draw_order_tests nfsu_track_render_tests:
  PASS. ctest --test-dir build -R
  '^(nfsu_track_contact_tests|nfsu_draw_execution_tests|nfsu_deko_draw_order_tests|nfsu_track_render_tests)$'
  --output-on-failure:4/4 PASS (native-tests-final.log).
- intro_grid_viewer <retail> <fresh-output> 6000 profile: PASS and matching
  signatures above. New lamp test checks duplicate last-match uploads and
  distinct light placements; cube test compares against the six-pass reference
  with empty/shared/mixed/invalid tags and unchanged uniform indices.
- cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=cpu-hotpaths-r2: PASS.
  cmake --build build-switch --target nfsu_switch_race_nro: PASS.
- ctest --test-dir build-switch -R
  '^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders|nfsu_switch_gpu_sample_contract)$'
  --output-on-failure:3/3 PASS (sdk-tests.log).

Package build/cpu-hotpaths-r2/nfsu-cpu-hotpaths-r2.nro has embedded tag
cpu-hotpaths-r2, SHA256
587fa973c96afacd27f8778aa20a091ed8516b7b69460226de1be3a02133f679.
Source delta, package manifest and logs are local in build/cpu-hotpaths-r2.
No upload, source push, merge or publication performed. Broad matrices were
not run under the owner's focused-validation instruction. Hardware startup,
CPU reduction and stable60 remain UNVERIFIED. Next action when owner is ready:
one combined console comparison with existing CPU and asynchronous GPU probes,
including the avenue and parking. Original collision fidelity remains open.


## 2026-10-14: cpu-hotpaths-r2 owner hardware comparison

Owner authorized upload and completed lap1 (1:13.49), returned to menu and
exited. nxlink exec70976 is terminal exit0. Exact NRO/source identity is in
build/cpu-hotpaths-r2/package.json; source d468283. Frozen owner-drive.log and
owner-analysis.json use the cpu-hotpaths-r2 prefix in that same directory.
The hardware receipt records PID/start time, upload, GPU validity and exit.

Compared with physics-gpu-r3 at5fb63f4, same540p and measured clock requests:
CPU1020000000, GPU460800000, EMC1331200000; no unpause resets. Presentation
interval mean17.172490 ->17.312733 ms (58.23 ->57.76 FPS equivalent).
Samples3600 ->4291; p95 19.25 ->20.25 ms; p99 29.25 ->29.00 ms;
max36.059531 ->33.265312 ms; over25ms share2.67 ->4.05 percent;
over33.333333ms count6 ->0. Thus stable60 is NOT achieved and the overall
run is not faster. Different input and lap duration (1:00.80 ->1:13.49)
prevent treating this as a controlled whole-frame speedup/regression test.

Whole-run mean CPU record4.610324 ->3.821014 ms (17.12 percent lower),
simulation5.950398 ->6.795998 ms. Sampled road reflection child1.279200
->0.587000 ms. Sampled prop motion0.622297 ->1.428063 ms; player/rival
physics and traffic motion did not rise in those samples. These stage probes
are sampled and not interchangeable with whole-run aggregates.
GPU72 valid samples, invalid0/dropped0, mean10.673839/max13.003333 ms;
baseline mean10.779013/max12.936562. One sample per60 presentations still
cannot exclude unsampled GPU spikes. Evidence continues to point to CPU.

Read RaceProps::advance: each awake unexpired object performs two substeps,
per-vertex terrain queries and impulses; expiry and quiet-body sleep exist.
The log does not record awake prop counts, so accumulating active debris is
only a hypothesis, not a proven defect or excuse for deleting objects.
Next code investigation: prop motion/terrain query cost and sleep eligibility,
preserving collision behavior and original lifetime policy. No new source
change or console upload in this analysis turn. No GPU or stable60 acceptance.


## 2026-10-14: spatial attribution instead of lap-time guesses

Owner clarified: avenue now good, parking approach moderately slower, theater
has hard drops. Requested investigation along the actual game route.
Visually verified both landmarks using retail track-stream-path CPU frames:
parking ramp sample(987.5,350,40.56), theater approach
(849.855,-52.370,53.27), in raw physics coordinates. Images, exact camera
paths and frame listings: build/spatial-profile-r1/parking*, theater*,
landmarks.png. These are diagnostic cameras, not the owner's driving replay.

Added intro_grid_viewer spatial-profile mode. Existing optional CPU timers
now print position, route node/fraction and per-stage costs each60 simulation
ticks, plus active prop identity/position/age/quiet/lifetime. No simulation
inputs or physics were changed. Native6000-tick state, prop and three command
signatures match the prior final-1.log. An unassisted24000-tick run completed
and crossed both neighborhoods. It is not the exact owner's route: the
parking neighborhood includes the upper road; compare height before claiming
an underground reproduction. In the parking neighborhood up to15 props were
awake; near the theater about10. No isolated theater CPU spike was reproduced
by this input. Do not extrapolate PC timings to Switch FPS.

Confirmed ongoing off-camera work: ZCS_SignNoParkingA section1202 instances5/6
and section1203 instance3 remain awake with quiet0 and lifetime0 after70s;
another active sign was1095m from the player at tick2880. Rivals can activate
these objects. This proves persistent distant simulation in this scenario,
not that it caused the owner's exact theater drop. No arbitrary deletion,
sleep threshold or lifetime change is justified by this evidence alone.

Switch CPU_SPATIAL records the same sampled frame's world position, route,
step count, player/rival/traffic/prop CPU costs and active prop summary. Printed
after the timed simulation/render/presentation; cadence remains once per60
presentations. This adds diagnostic output overhead and may miss unsampled
spikes; it does not alter physics/render quality. Position plus z separates
upper road from the underground passage. New data requires a hardware run.

Checks: native viewer build PASS;6000 and24000 spatial-profile scenarios PASS.
SDK configure cpu-spatial-r1 and explicit nfsu_switch_race_nro build PASS;
package, embedded shaders and GPU sample contract CTest3/3 PASS. Focused checks
only per owner. Package/source delta identities are in package.json in the
same evidence directory. ASCII modified bytes and names checked before commit.
Candidate prepared, NOT uploaded. Next: when owner authorizes the next drive,
collect CPU_SPATIAL through the same parking/theater route and correlate
same-frame stage costs and active bodies; stable60 and collision fidelity open.


## 2026-10-14: owner spatial run received, two CPU contributors

cpu-spatial-r1 at c71355d uploaded on owner request, nxlink exec41392,
PID40964/start2026-10-14T10:34:50.7534255Z. Terminal exit0 after menu return.
Frozen log and analysis: build/spatial-profile-r1/cpu-spatial-r1-owner-drive.log
and cpu-spatial-r1-owner-analysis.json, with SHA and all66 spatial samples.
Owner drove the underground passage: samples at x1010.597/y347.120/z43.197
and x939.998/y346.302/z41.281 confirm lower level, unlike the desktop driver.

Hardware observations, same-frame sums (not per-step averages):
- Parking four spatial samples frame1801..1981: frame15.482..16.660ms,
  one step each, props2.385..2.658ms,12 active props ALL farther than200m.
  This supports the owner's improved parking observation; unsampled dips remain.
- After parking, on the approach towards theater: frame2221 at
  x718.524/y186.257/z52.767,node26:34.277ms total,2 physics steps,
  sim20.606ms, rivals12.510ms, player2.979ms, props4.044ms,
  record5.786ms, command4.843ms. All10 active props farther than200m.
- Theater approach frame2401 x822.365/y23.634,node29:27.418ms,2steps,
  sim16.932ms,rivals7.171ms,props4.635ms.10of11 active props farther200m.
- At the theater frame2461 x854.634/y-54.702/z53.610,node29:
  18.593ms,1step,sim8.783ms,rivals3.824ms,player2.245ms,props2.177ms,
  record4.513ms,command3.269ms. No waiting for GPU is needed to exceed16.67ms.
- Near the end of the drive frame4021:16 awake props,14 farther200m,
  oldest57.82s; prop motion4.160ms for one simulation step. Startup had0
  awake props and0.006..0.013ms prop motion. Persistent distant work confirmed.

Do not reduce this to a single prop-only cause: rival physics is the largest
measured contributor in the worst spatial sample, while road draw preparation
also costs more there. Opponents are simulated at their own positions; those
positions are not logged, so no specific rival or collision leaf is blamed.
GPU66 valid samples mean10.784316/max13.052760ms, invalid0/dropped0;
unsampled GPU spikes remain possible. Presentation3977 samples mean18.123668,
p95 29ms,p99 33.25ms,max53.741302. The absolute maximum was not among spatial
samples and cannot be assigned a location. Diagnostic output adds overhead;
this drive is not a controlled comparison with the previous input.

Next code work: reduce retained prop terrain-contact cost and investigate
rival hull/terrain-query spikes with per-actor/contact evidence. Keep physics
step cadence and original behavior; do not delete distant props or suppress
necessary catch-up to manufacture60. No source fix or new upload in this
analysis turn. Stable60 and original collision fidelity remain open.


## 2026-10-14: contact CPU batch pruning candidate

Owner authorized both persistent prop and rival physics cost reductions after
cpu-spatial-r1. Solo implementation, no reviewer or delegation. Base5df447e;
source delta src/track_contact.cpp and tests/track_contact_tests.cpp. Evidence:
build/contact-cpu-r1/source-identity.json and candidate.diff identify exact
precommit source. No push/merge/publication or console upload in this step.

Changes:
- Enclose all batch endpoints (including prop fallback endpoints) once. For
  each reusable leaf, conservatively filter only the conservative algorithm's
  polygon list by that enclosing box. Its existing exact per-segment AABB
  rejection would reject every removed triangle for every segment. The float
  Moller list stays complete, including possible rounded outside-edge hits.
- Invalid/nonfinite batch bounds disable filtering. Leaf transitions rebuild
  the candidate list; refused indices and query limits keep their old checks.
  Order/ties and unbounded overflow handling remain. No permanent world/ABI
  additions; one inline128-index work list plus overflow only for dense cells.
- Reuse an already evaluated conservative crossing if both algorithms choose
  the same polygon/triangle. Compute the conservative double length only on
  an actual conservative hit, with the original ordered sum and det_sqrt.
- No step-frequency, lifetime, sleeping, collision impulse, actor range,
  rendering quality or physical tuning change. Distant props still simulate.

Rejected experiment: caching vertical direction terms did not improve the
measured drive. Removed; unproven-downward-cache.diff and cache1.log retained
under the evidence directory. No claim of hardware speedup from that attempt.

Focused validation per owner's minimal-test instruction:
cmake --build build --target nfsu_intro_grid_viewer nfsu_track_contact_tests -j 8
PASS (Release, real runtime workspace path). ctest --test-dir build -R
'^nfsu_track_contact_tests$' --output-on-failure:1/1 PASS. Existing segment
fixtures now also compare fresh/reused batch contacts bitwise, including wall
seams/nearest contacts/non-hit output. Added finite fan and dense bounded/empty
candidate cases; existing malformed/overflow cases retained. Full unrelated
matrices not run. Source/path ASCII and git diff --check PASS before commit.

At base5df447e versus the candidate.diff identified above, two interleaved
24000-tick runs each on PC Release, 23879 timed simulation steps per run:
rival physics mean0.246217 ->0.208989ms (-15.12%); player0.138513 ->0.1174835
(-15.18%); prop motion0.153947 ->0.1275135 (-17.17%). Full per-stage results,
log hashes, all state/prop/three command signatures: comparison.json. Each
run exited0 and printed PASS; every signature matches both baseline runs.
These are PC CPU measurements, not console FPS. The automatic driver is not
the owner's input and does not drive the lower parking passage.

Next: build contact-cpu-r1 NRO with spatial/GPU sampling retained, run package
checks, then measure an owner-authorized Switch drive through parking/theater.
Stable60 and original traffic/rival collision fidelity remain open; this batch
reduces contact-query work without claiming to fix collision behavior.


Package ready at source1a897395e51d5a30ccf5e4fd1da5ad1b93c42815:
build/contact-cpu-r1/nfsu-contact-cpu-r1.nro,3869361 bytes,
SHA256542cb930526192444e76e0fd74394d58a8dd5ec481d0718144bac4534f3035c9,
embedded tag contact-cpu-r1 verified. SDK configure and explicit
nfsu_switch_race_nro target PASS; package/shaders/GPU contract CTest3/3 PASS
in MSYS. Initial native-Windows CTest invocation could not resolve the MSYS
executable paths and ran no tests (exit8); preserved as
package-tests-wrong-shell.log. Correct shell rerun exit0 in package-tests.log.
Git tree ASCII/path/mode scan clean. Committed locally, not pushed. Preserved
unrelated untracked owner/session files. Package NOT uploaded, no new nxlink
started. Hardware behavior/performance unverified; next is the owner's matched
parking/theater drive. No claim of stable60 or corrected traffic collisions.


## 2026-10-14: contact-cpu-r1 owner drive and collision video

Owner opened Netloader and authorized send. Exact packaged NRO uploaded to
192.168.1.56; nxlink exec35795/PID34420/start2026-10-14T11:29:43.6330310Z,
now terminal exit0 after menu return. Receipt hardware-contact-cpu-r1-run1.json.
Frozen log contact-cpu-r1-owner-drive.log SHA256
cb68094346deb960abbcc898c6bb189829991e024b06e67111b9da6aece00d11;
analysis contact-cpu-r1-owner-analysis.json in build/contact-cpu-r1.

At source1a89739 on Switch handheld540p, same CPU/GPU/EMC clocks as prior run:
4868 presentation intervals mean16.961293ms (~58.958FPS equivalent),p95 18.5,
p99 26.25,max78.519062ms;68>25ms (1.397%),2>33.3333ms. CPU frame-work max
31.846770ms; the78.519ms presentation gap is not located by sparse spatial
samples and is not a measured78ms simulation or GPU cost. Prior spatial-r1:
mean18.123668 (~55.176FPS),p95 29ms,338/3977>25ms (8.499%),37>33.3333ms.
Different inputs and active prop counts mean this is an observed drive
comparison, NOT a controlled optimization percentage.

CPU mean sim5.810461ms vs prior7.788539; record3.897083 vs3.863819. Sampled
rivals2.390598ms, props1.176747ms (87 steps). GPU81 samples mean10.772069,
max13.545156ms,invalid0/dropped0; sparse sampling does not rule out other GPU
spikes. Physics and command preparation still produce budget overruns.

Parking is actually underground (x988.813/y346.362/z41.001): sampled frame2281
27.104ms,2steps,sim17.754,player9.252,rivals4.045,props3.269; other parking
samples15.412..16.737ms. Theater approach frame2881 x735.99/y109.627:
26.499ms,2steps,player7.660,props3.032. At theater six samples15.8..17.875ms.
Approach command preparation reaches5.263ms and record5.124ms on one step.
Persistent props still active7..9 in late route, versus up to16 last run.

Owner video2026101414583900-09283F1FC0B01C5416AE2622190758FC.mp4 is28.8s,
1280x720/30FPS capture, early lap from race time~5s to~34s/parking entrance.
It does not show theater. Extracted contact sheets owner-video-sheet.jpg and
owner-contact-sheet.jpg inspect the actual frames. Around video24..26.5s
(race29..32s) green rival receives side contacts, rises, then is thrown high
above player's roof. Overlay dips to~54FPS and recovers near60. This confirms
the outstanding rival collision defect; no exact solver cause asserted from
video. The CPU-only optimization intentionally preserved this physics, not
fixed it. Keep original-collision fidelity open. No new implementation or
upload in this analysis turn. Next: reproduce this car-pair impulse/arm case
against recovered original behavior and address residual player/contact CPU
spikes without changing step cadence. Stable60 still unproved.


## 2026-10-14: parking/theater query cost and first-contact correction

Owner requested both CPU hot spots and car collisions in one pass. Solo in
owned build/day20261008-root-combined, branch codex/draw-distance-original,
base358843b. No reviewers/agents, external publication, retail write or upload.
Evidence directory: build/parking-contact-r2. All figures below are historical
PC Release measurements of this candidate versus base358843b, not Switch FPS.

Reproduced at the console log's world coordinates using37 rays from the actual
UG0 player hull. Start leaf names31 polygons, parking192, theater approach209
across two leaves. Start fan ~10us, parking36..39us, theater42..70us before.
This explains a concrete terrain-query cost difference; it is not proof that
all frame spikes have one cause. Terrain mode is a fixed-pose microbenchmark,
not the owner's recording. No body contacts occur in these above-ground fans.

CPU changes: batch the player's four strut probes with the existing exact
segment-query batch; scalar/default ground implementations retain call order.
Reuse validated ordered polygon lists for repeated multi-leaf ray footprints,
including the existing conservative batch filter. No physics-rate, prop-lifetime,
quality, float predicate, impulse tuning or pin changes. Terrain scalar/batch
answers agree. Theater fan after ~38..49us; parking similar. PC24,000-tick
baseline and CPU-only candidate have identical state, prop and all three draw
command signatures. Player physics mean0.116325 ->0.110895ms (~4.7%); rivals
0.207617 ->0.206684ms; props0.126652 ->0.128292ms. Other small changes are noise,
not claimed gains. cpu-comparison.json records identities and exact logs.

Collision defect reproduced with actual three rival and six traffic bounds:
old final-pose SAT can choose the up axis after deep horizontal overlap and
move a car vertically by up to0.66254m in the432 pose cases. Added a bounded
pose search before impulse resolution for racer/racer, racer/traffic and
traffic/traffic. Eight bisections follow the bound recovered at original46AE60
(docs/COLLISION_SCHEDULER.md); the port still uses OBB overlap and normalized
quaternion interpolation, NOT original convex GJK, the full event scheduler,
remaining-time reintegration or original hard-hit state callback parity.
Integrated momenta are retained at the searched pose; axes/public/rigid pose
are coherent. Spawns/resets invalidate old trajectories; subsequent contacts
use already-corrected poses. Nonclosing pairs do not rewind or reposition.
Real vertical impacts remain enabled. No blanket roll/up-impulse clamp.

432 identical hull/offset/speed pose cases after the change: largest vertical
movement2.98023e-08m. This is a geometric adapter result, not a full suspension
or exact owner-video reproduction. The new horizontal deep-contact fixtures
reproduce the old vertical shift and now resolve within the eight-round time
bracket; falling-on-car and separating-overlap controls pass. Initial fixture
failure exposed the stock authored pitch: the strictly horizontal assertion
now explicitly uses identity body orientation rather than widening tolerance.
An initial retail probe incorrectly required a hit for Celica's clear0.01 gap;
it now accepts a genuine no-contact result. Failed logs retained separately.

Focused native contact/AI CTest2/2 PASS. Vehicle-track legacy test was also run:
18 failure lines, including old physical expectations and telemetry pins;
rebuilding base358843b reproduces every line exactly. No expectation/pin was
changed. cpu-tests.log and baseline-vehicle-tests.log preserve that result.
The final combined24,000-tick drive completed with PASS and no refused state.
Its signatures differ as expected from the collision behavior change; no old
pin was refreshed. Full unrelated/known-broken matrices were not rerun, per
owner instruction. All current code verification is author verification.

Next: fresh SDK dependency build and package gates for parking-contact-r2,
then owner Switch drive through parking/theater with car impacts. Console
stable60, the observed rival flight and full original collision fidelity are
not yet closed. No current Netloader-readiness assumption.


Follow-on before packaging at d99eab6: parking still spent time testing far
triangle planes in the float Moller pass. Added a certified common-origin fan
filter, preserving the exact float predicates. For each triangle, the plane
numerator is independent of ray direction. Positive rounded product/sum bounds
cover the magnitude of every float determinant in the fan. Monotonic float
reciprocal/multiply gives a lower bound on the absolute returned t. Reject only
when that bound exceeds every actual float ray length. Nonfinite bounds fall
back; zero determinant bound means every determinant is zero. Conservative
queries retain their own independent list. This does not use geometric AABB
rejection for float Moller results, which the earlier evidence forbids.

Only build this filter when the whole fan is strictly inside a single leaf.
The first experiment rebuilt filters across theater's alternating leaf sets
and regressed that microbenchmark; plane-tests.log/terrain-plane.log retained.
The final boundary path retains the prior multi-leaf optimization without that
filter or its direction-preparation overhead. On PC Release at this candidate,
interleaved control then final measurements in terrain-control.log versus
terrain-plane2.log: parking37..42 ->17..18us, with bitwise equal scalar answers;
theater remains38..49us. Start~10us. These are query costs, not frame times.

At d99eab6 versus this filter, final-drive.log and filtered-drive.log contain
identical24,000-tick state, prop and three command signatures. Rival mean
0.203073 ->0.185867ms (-8.5%); player0.108497 ->0.101198ms (-6.7%); props
0.123280 ->0.123719ms (no claimed gain). Every existing segment fixture also
compares the batched query with the unfiltered scalar path. Added endpoint/
adjacent-float and nearly parallel fan cases. All source builds use the shared
-fno-fast-math -ffp-contract=off configuration on PC and Switch.
The earlier d99eab6 SDK build succeeded but is superseded and was NOT uploaded.
Rebuild the explicit NRO target at the new source before any hardware test.


Package ready, source ca7d7fa5ec541d00b026f8e1556b18a5046453db:
build/parking-contact-r2/nfsu-parking-contact-r2.nro
3881649 bytes; SHA256 9d2dc6c372451d0da86a46a43c615d0450b1117344f4a97a12d08456121cf0c4.
Embedded tag parking-contact-r2 and NRO0 header verified. This Switch target
embeds the build tag, not a Git SHA; an initial extra SHA-embedding assertion
failed before copying anything. Source identity is bound by the clean tree,
SDK compile/link log and package.json, not by claiming an absent embedded SHA.
Fresh SDK explicit nfsu_switch_race_nro target PASS, then MSYS CTest
'^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders|nfsu_switch_gpu_sample_contract)$'
3/3 PASS (sdk-final.log). Current-head native rebuild of nfsu_ai_track_tests
and nfsu_track_contact_tests PASS, corresponding CTest2/2 PASS
(native-at-head-tests.log). Earlier filter-specific drive and microbenchmark
logs retain their own source versions. Git ASCII/path/mode scans and new
artifact path scan clean; no shell automation added. Unrelated untracked files
preserved. Two code commits are local only; no merge/push/publication.

Netloader readiness requested after this concrete package was ready. No current
upload/receiver process started. Next is this exact NRO on Switch: one lap
including parking/theater and side/rear impacts with rivals and traffic. Keep
CPU/GPU/spatial profiling and L3+R3 FPS graph. Confirm measured frame pacing
and observed collision correction; retain full original-collision gaps noted
above. Stable60 and complete original collision parity remain unverified.

Owner confirmed Netloader readiness and authorized upload. Exact package above
sent on 2026-10-14; nxlink reports starting server / server active.
Owned exec session 57177, nxlink PID34028, start UTC2026-10-14T15:06:45.789464Z.
Receiver log: build/parking-contact-r2/nxlink-parking-contact-r2-run1.log.
Receipt: build/parking-contact-r2/hardware-parking-contact-r2-run1.json.
Gameplay and measured improvement remain unverified pending the owner drive.
Next: read this owned live session/log after the lap; compare spatial CPU/GPU
samples at parking/theater and inspect impacts. Do not poll old terminal runs.

## parking-contact-r2 owner drive received 2026-10-14

At source ca7d7fa5ec541d00b026f8e1556b18a5046453db, owner Switch drive,
owned exec57177 terminal exit0. Normal menu return and exiting precede socket
closure; do not poll this session again. Frozen evidence:
build/parking-contact-r2/parking-contact-r2-owner-drive.log
SHA256 2fc1b74ad6e912264b7ac458693d61dae5a52a437ecf6196b46f4215702d7ad6.
Parsed evidence: parking-contact-r2-owner-analysis.json in same directory.
Receipt updated terminal. Same handheld540, FXAA, CPU1020/GPU460.8/EMC1331.2MHz.
4035 driving presentation intervals: mean16.682900ms (59.9416FPS equivalent),
p95=17.50ms p99=18.50ms max26.005416ms; 2>25ms (0.04957%), zero>33.333333ms.
Previous contact-cpu-r1: 4868 intervals, mean16.961293ms, p99=26.25ms,
68>25ms (1.39688%), 2>33.333333ms. Different drives/speeds/awake prop counts:
this is observed improvement, not a controlled isolated optimization ratio.
CPU mean simulation4.504591ms versus5.810461ms previously. GPU67 samples,
mean10.718814ms max12.974583ms invalid0 dropped0; sparse60-presentation cadence
cannot exclude unsampled GPU spikes. Presentation is not scanout measurement.
Coordinate-window sampled frame maxima, previous/current:
parking x850..1085 y330..360: 27.104/20.020ms (6/5 samples);
theater approach x710..770 y70..210:26.499/17.111ms (4/4 samples);
theater x790..920 y-110..50:17.875/16.749ms (6/4 samples).
No full collision outcome evidence from this log: owner observation/video
still needed for observed rival flight and traffic impact behavior. Prior
bounded geometric correction is not complete original collision parity.
No source change, rebuild, push or merge during this log analysis.


## Rival physics optimization in progress, 2026-10-14

Owner requests lower rival physics cost with unchanged behavior. Solo scope;
no worker/reviewer, no source publication. Own branch codex/draw-distance-original.
Evidence directory build/rival-physics-r1. Rejected origin-operand precomputation
retained as rejected-origin-cache.cpp and terrain-candidate.log: slower at
boundaries; not in final source. Diagnostic instrument.exe / instrument.log
separates AI terrain wheel/body costs; diagnostic ai_track edits restored.
Candidate retains four ordered validated cell lists inside one query call,
rotates cache entries by index (no array copies), and applies existing proven
rounded float plane bound across leaf crossings. Cache capacity never limits
coverage; eviction rebuilds from validated indices. No change to AI integration,
forces, cadence, steering, drivetrain, or solver decisions.
Focused track contact and AI track tests2/2 PASS including new cache eviction
versus unfiltered scalar bit comparisons. Initial24000-tick baseline/candidate
player/props/three command digests equal. Rival mean0.189835/0.176505ms on PC;
not a hardware gain claim. Full per-rival telemetry/engine/rigid/wheel hashing
added to the desktop tool for the final baseline/candidate comparison.
TEMPORARY reference build active: src/track_contact.cpp currently baseline;
restore exact build/rival-physics-r1/final-track_contact.cpp before final build!
Reference exec13372 builds tool then runs24000 ticks. After it completes,
restore source, build candidate, compare RIVAL_SEQUENCE too, then SDK/NRO.
No new package uploaded. Old nxlink57177 is terminal, do not poll it.


Rival optimization candidate RESTORED after reference build and validated.
Reference exec13372 and candidate exec10834 terminal exit0; SDKexec64260 exit0.
Baseline source ca7d7fa query versus final four-list candidate, same extended
intro_grid_viewer: 24000 steps, player/prop/rival and three command digests
identical; build/rival-physics-r1/comparison.json records the comparison.
RIVAL_SEQUENCE serializes all three rivals' telemetry and explicit rigid,
engine and wheel values on every step; same digest, not only final positions.
PC rival mean0.192517 ->0.178313ms (~7.4% less); initial independent timing
pair0.189835 ->0.176505ms (~7.0%). Do not extrapolate a hardware percentage.
The final PC run has an isolated42.519ms pair/41.874ms prop sample; not hidden,
not attributed to a proven cause, and no stable-frame guarantee from PC.
Tests: cmake --build build --target nfsu_intro_grid_viewer
nfsu_track_contact_tests nfsu_ai_track_tests -j6 PASS;
ctest --test-dir build -R '^(nfsu_track_contact_tests|nfsu_ai_track_tests)$'
--output-on-failure:2/2 PASS (candidate-full-tests.log).
SDK configure tag rival-physics-r1; explicit nfsu_switch_race_nro rebuild PASS;
NRO, embedded-shaders and GPU sample contract CTest3/3 PASS (sdk.log).
Focused gates per owner's minimal-test instruction; no full-suite claim.
Package build/rival-physics-r1/nfsu-rival-physics-r1.nro:
3881649bytes SHA256 24bb39207bfd24f086d27428fece17ec2dcea6ccfcc8913bbd27eb710056d16e.
Tag/header verified. No Git SHA embedded; clean source/build log bind source.
No source push/merge and no upload. Next: owner Netloader readiness, upload
this exact package, compare parking/theater CPU/frame pacing and behavior.
Original collision adapter limitations remain. No formula/cadence reduction.


Owner authorized rival-physics-r1 upload; exact package hash verified.
Netloader reports starting server / server active. Owned exec1340, PID42052, start UTC2026-10-14T16:49:14.1349340Z.
Receipt: build/rival-physics-r1/hardware-rival-physics-r1-run1.json.
Log: build/rival-physics-r1/nxlink-rival-physics-r1-run1.log.
Next: owner control lap, compare parking/theater with parking-contact-r2.
Hardware gain and collision behavior remain unverified; no stable60 claim.


## Rival physics owner drive received 2026-10-14

Source75b4a86ea2210001449fc0d364e540e884c15f81, owned nxlink exec1340
terminal exit0; normal menu return/exiting before socket closure. Do not poll.
Frozen build/rival-physics-r1/rival-physics-r1-owner-drive.log SHA256
258c3d685e427cc1f598dc5b9ef63923478668c0348477a17e058f26c93e8d26.
Analysis: rival-physics-r1-owner-analysis.json; receipt updated terminal.
Same handheld540/FXAA and CPU1020 GPU460.8 EMC1331.2MHz, unpause0.
4088 presentation intervals mean16.697615ms (59.8888FPS equivalent),
p95=17.75 p99=20.00 max26.478125ms;4>25ms (0.09785%),zero>33.333333ms.
Prior parking-contact-r2:4035 intervals mean16.682900ms,p99=18.50ms,
2>25ms,zero>33.333333ms. Whole-run frame pacing did NOT improve this drive.
Sampled rivals mean2.145457 ->1.869355ms (-12.87% observed, not isolated
controlled attribution); props mean0.371257 ->0.904618ms. Awake prop max5->12.
GPU68 samples mean10.814516 max13.041562 invalid0 dropped0; sparse cadence60.
Spatial sampled maxima previous/current:parking20.020/16.973ms;
theater approach17.111/24.703ms;theater16.749/16.580ms. Not all-frame maxima.
Approach frame2401 x737.392 y120.563,2steps:sim12.806 player5.207
rivals4.229 props1.990 record5.178 command4.010 frame24.703ms.
Late frame4201 x1693.719 y440.170,2steps:props5.416ms,awake12,vertices107,
far200=11,farthest957.7m,oldest48.72s;frame20.506ms.
Different manual drives/activations; no exact optimization percentage claim.
Next optimization candidate: awake-prop terrain queries and rest eligibility.
Distance alone does not authorize freezing/removing objects; preserve original
lifetime/activation and justify any rest-policy change from original evidence.
No new source edit/build/upload in this analysis. Collision parity still open.


## Prop query optimization investigation, 2026-10-14

Owner authorized remaining prop work, solo and focused validation only.
Base75b4a86ea2210001449fc0d364e540e884c15f81; local only, no publication.
Evidence directory build/prop-queries-r1. Baseline saved source and executable.
Three unsuccessful candidates preserved: rejected-interval.cpp,
rejected-direction.cpp, rejected-early.cpp. All retained player/prop/rival
and draw sequence digests on24000 ticks, but PC prop mean times were
0.135622/0.138517/0.129624ms versus baseline0.122276ms. Not shipping them.
Temporary6000-tick timing on baseline: prop total399.7651ms,
terrain queries367.16ms, contact rigid refresh7.9614ms. ~92% is terrain query.
Prop spatial diagnostics show intermittent angular motion prevents the
current quiet threshold. Original597B80/597FA0 lifecycle is a separate known
adapter debt; no distance cutoff, forced sleep, cadence or threshold change.
Current source track_contact.cpp has TEMPORARY query phase timers; restore
build/prop-queries-r1/track_contact-before.cpp before any production build.
race_props.cpp already restored exact baseline. Tool adds speed2/spin2 to
spatial logging; contact tests extend scalar-versus-paired input coverage.
Query phase diagnostic exec8895 runs6000 ticks into query-cost.log.
Next: inspect phase cost, implement only a measured safe improvement; otherwise
retain baseline and report no justified optimization. No NRO or upload yet.


Prop investigation CLOSED without a production optimization candidate.
Query diagnostic exec8895 exit0:6000 ticks; total449.3033ms with timers,
Moller229.1494ms, conservative107.1547ms;644141 rays,23898859 named entries,
2858697 conservative-filter entries; only1203 duplicate entries during list
construction. Timers perturb these costs; not an all-frame hardware measure.
Fourth lazy per-call direction cache also rejected:24000 ticks, all six
sequence/draw digests identical, but prop mean0.144595ms on PC. Contact test
33422 checks PASS. Preserved rejected-lazy.cpp/lazy.log/lazy-tests.log.
All four variants were slower than the saved baseline; no claimed gain.
Final track_contact.cpp and race_props.cpp RESTORED exact base; added paired
comparison tests preserved only in paired-comparison-tests.cpp, tracked tests
restored. Production simulation source unchanged. Only desktop spatial
speed2/spin2 diagnostics and this checkpoint remain. No new NRO is warranted
for rejected changes; latest owner-tested rival-physics-r1 remains current.
The prop rest/contact-lifecycle difference remains OPEN, documented in
TRACK_PROP_LIFECYCLE.md. Signs retain angular motion above the port adapter's
quiet threshold after70 seconds; this does not authorize tuning that threshold.
Future behavioral correction must recover original contact-age and settling
state. No arbitrary removal, forced sleep or scope expansion performed.
Native baseline tool rebuild follows to remove experimental compiled code.

Restoration verified: exec79975 build target nfsu_intro_grid_viewer exit0.
Fresh240-tick profile smoke exit0 and PASS shared RaceRun, no temporary
query/prop timing banners. comparison.json records all four rejected runs
and matching six sequence/draw digests. Restored simulation/test files equal
base; tracked ASCII/tree-mode scans and changed-byte/path scans PASS.
Only desktop spatial diagnostics and checkpoint committed locally. No push,
merge, source publication, new package, or hardware upload in this task.


## Ordered optimization batch, 2026-10-14, in progress

Owner explicitly authorized original prop settling, then static environment,
then vehicle work, toward one NRO. Solo, focused checks; no publication.
Base ad01a6e, owned combined tree. New prop_settling.h implements original
597EA0 motion states,597FA0 momentum writer and597B80 retirement predicates.
Original585E6C clock rounds fixed1/60 dt to67 source4000-Hz ticks per update.
Original420720 and420AB0 retain records until last-touch age reaches0.3f;
creation age is separate. Static support remains the existing vertex adapter,
not full original manifold generation; car overlap is the SAT contact adapter.
No arbitrary distance removal, physical tuning or cadence change.
First route24000 completed: prop PC mean0.0324942ms vs previous baseline
0.122276ms, but two zero-speed bodies retained awake due missing record aging.
An exact-pose contact-retention trial did not resolve them; replaced with
source-backed0.3s last-touch retention, not an added distance tolerance.
Native-r2 executes unchanged original predicate/writer/expiry bytes for768
synthetic cases; contact lookup/candidate maintenance and branch endpoints
are explicit boundary stubs; empty component refresh list. No live trajectory
claim. Reports build/prop-settle-r1/native-r2; test compares state, retirement,
expiry and six momentum bit patterns. Fresh candidate route exec82893 active.
Next: complete prop behavior/retention checks, freeze stage; then compare
stable bucket ordering against current render sorting and inspect car costs.


Ordered batch complete locally; Switch owner drive remains pending.
Final implementation uses individual polygon record creation/touch clocks.
Native-final:768 original x86 cases PASS, including585E6C clock execution;
prop_settling_tests matches state, retirement, expiry and momentum bit patterns.
Final native build targets intro_grid_viewer,prop_settling_tests,
stable_draw_order_tests,ai_track_tests completed. Focused CTest3/3 PASS.
Retail props-check PASS11 unique classes/178 placements:9 resting fixtures
held pose for60 updates,7 were reached and reactivated by a repeat car impact.
The remaining2 had no repeated-impact witness; no claim of exhaustive re-hit.
24000-tick final spatial-profile PASS. At ad01a6e+this batch, measured on PC
in build/prop-settle-r1/final.log: prop mean0.00548478ms versus prior baseline
0.122276ms. Final main-list mean0.019675ms. Environment stage isolated before
final per-polygon refinement: all six state/draw digests matched old sorting;
final player/rivals/three draw digests also match. Final prop digest differs
intentionally due per-polygon record creation/expiry. final-comparison.json.
Linear stable bucket scatter preserves layer,bucket,encounter ordering for
main view and cube faces, with reserved scratch and cache budget accounting.
Additional car contact winner reduction REJECTED: identical digests but no
CPU gain. vehicle_track.cpp restored exactly; rejected-car-winners.cpp and
cars.log preserve experiment. Existing accepted rival acceleration retained.
SDK explicit nfsu_switch_race_nro target built with tag settle-render-r1;
Switch race NRO, embedded shaders and GPU sample contract3/3 PASS.
Package build/settle-render-r1/nfsu-settle-render-r1.nro and manifest.json.
Targeted gates only as owner requested; no full-suite or live-original
trajectory parity claim. Vertex support/SAT/full manifold lifecycle limits
remain in TRACK_PROP_LIFECYCLE.md. No new hardware performance claim.
ASCII source/path checks and native witness smoke run from actual workspace.
All changes local only; no push, merge, publication or hardware upload.
Next: obtain current Netloader readiness, send exact package, collect owner
circle especially theatre/parking and repeat collisions with roadside props.


## settle-render-r1 full owner lap, 2026-10-15

Owner drove parking and theater and returned to menu. Exact source b6c015c,
package SHA542d1a68acf2e3e36c1ff875dcc3e216ffbd8bf38ccda3ade370e5599b0574ff.
Sealed build/settle-render-r1/settle-render-r1-owner-drive-run2.log,
SHAa402b857f270f89213e3a1f4506c3d4506a00de60726433bff259d0c96824db8.
Analysis: settle-render-r1-owner-analysis-run2.json in same directory.
Receiver exec10216 ended (log exiting/socket close); no old process polling.
Same measured540p/CPU1020/GPU460.8/EMC1331.2 as rival-physics-r1.
Historical manual-lap comparison previous/current:4088/4071 presentation
intervals,68.260/67.867s,59.889/59.985FPS,p99=20.00/18.00ms,
max26.478/25.440ms,over25ms4/1,over33.333ms0/0.
All-frame CPU means sim4.702/4.111ms,record3.764/3.399ms.
Sparse GPU means10.815/10.824ms (68 samples each), no meaningful GPU gain.
Sparse prop motion mean0.905/0.179ms; draw-list mean0.617/0.326ms.
Last current spatial sample awake0/vertices0 vs previous12/107; new quiet
props no longer accumulate continuing terrain queries on this owner route.
Region sampled frame maxima previous/current:parking16.973/16.893ms,
theater approach24.703/16.900ms,theater16.580/16.474ms.
These regions have4/3/4 samples in each run, not all-frame regional maxima.
Manual inputs/impacts differ: not paired deterministic A/B or proof that all
routes sustain60FPS. One25.44ms frame remains; sparse position logging cannot
localize it. Owner runtime performance check now received; visual/behavioral
fidelity of every prop class and original full manifold remain unverified.
No source push/merge/publication or further code changes in this measurement.


## spike-trace-r1 diagnostic candidate, 2026-10-15

Owner authorized identifying the remaining25.44ms frame. Existing60-frame
CPU/GPU/spatial sampling could not locate it. Diagnostic activates only for
spike-* build tags, direct presentation, no AB. Every-frame CPU stage timers
and asynchronous scene timestamp pairs; no added GPU pass barriers/waits.
A fixed64-event buffer stores previous/current/next rows for interval OR
work>20ms. Delayed GPU joins use display-frame identity, not simulation ticks;
unknown GPU remains-1. Overflow count explicit. Dump only after race exit.
Record heap traversal, pre-begin interval gap and previous post-present tail
alongside simulation/render/update/present/fence/command times and coordinates.
This distinguishes diagnostic/logging cost, scheduling gaps, CPU and GPU
candidates; it does not prove a cause until a hardware event is captured.
Default non-spike tags retain60-frame sampling; physical/gameplay code untouched.
Focused native tests: spike context/overflow/delayed GPU identity/reset,
race_frame_stats,profile_pages3/3PASS. SDK explicit race NRO build and
race_nro/embedded_shaders/gpu_sample_contract3/3PASS. No compiler warnings.
New scripts/paths/source bytes scanned ASCII; commands run in actual workspace.
Package build/spike-trace-r1/nfsu-spike-trace-r1.nro, manifest.json contains
exact SHA and local source commit. No upload yet, no push or merge.
Next: current Netloader confirmation, same full lap via parking/theater,
return to menu, inspect SPIKE_FRAME rows. Diagnostic timing overhead is real;
use this run for attribution, not an unqualified speed comparison.


## spike-trace-r1 owner diagnosis received, 2026-10-15

Exact b0814c9 package owner drive sealed in build/spike-trace-r1/
spike-trace-r1-owner-drive.log, SHA
c5c36b773eae1bc40f65efe477cdf8f577ef4e2294e7d3187dbc7c00934593b5.
Parsed spike-trace-r1-owner-analysis.json retains all16 events and neighbors.
No buffer drops; every captured row has matched GPU identity.3880 intervals,
5>25ms,1>33.333ms; max41.648281ms. GPU3881 completed samples,max13.464010ms.
Confirmed CPU catch-up spikes: frame1363/node18 interval25.831ms,2ticks,
simulation15.201ms (rivals8.220/player2.782/pairs3.598),GPU12.240ms.
Frame3351/node29 at theater interval27.554ms,2ticks,sim16.013ms
(player8.567/rivals6.387),GPU11.940ms. Terrain/contact subphase still unknown.
Diagnostic overhead: frame1447 heap traversal2.087ms propagates into next
interval; frame3013 heap2.505ms, frame3014 pre-begin gap2.524ms.
Frames1801/3001/3121/3241 follow periodic PERF output and have prior-tail
4.557/2.440/4.470/4.489ms, plus catch-up. These are not GPU stalls.
Worst frame4016/tick4015 at node3: work41.633ms,sim wrapper33.584ms but
six measured simulation stages total3.478ms,unattributed30.106ms. Same tick
emits synchronous BOOST_STATE for L release. Leading suspect is stdout/netlog
blocking inside the simulation wrapper; printf alone not individually timed,
so this is strong localized evidence, not proven exclusive OS/network cause.
Following frame4017 executes3catch-up ticks. No arbitrary tick dropping fix.
Next correction: defer routine PERF/BOOST logging and live heap traversal out
of play, retaining bounded after-run evidence; then target remaining expensive
player/rival collision/terrain work without changing physics cadence/behavior.
Hardware reproduction needed after correction. Current run is diagnostic,
not direct speed regression proof against uninstrumented settle-render-r1.

## NOS and traffic correction in progress, 2026-10-15

Owner paused optimization and requested NOS blur, stopped traffic queues,
traffic impact behavior and rival launches. Solo/minimal checks remain.
Base b0814c9df47cd40610877cbd7fcde1d818563cdb; local changes only.
Traffic contact no longer unconditionally means permanent full brake.
Original583DA0 native bounded cases stop at accumulated record magnitude>30:
0,5,29.999,30 do not stop;30.001,50 stop. Pair history stays per other actor.
Evidence build/nos-traffic-r3/traffic-stop-native.json; original executable
identity verified. Full original contact-state/recovery parity is still open.
Native race_session/ai_track/speed_blur tests3/3PASS before end-road correction.
Six retail traffic hulls retained by physical terrain solver in traffic-impact
fixture; all old simplified bodies escape its wall. This is not car-pair parity.
No-impact traffic world reproduced permanent queues at road41 endpoint.
Old traffic-before executable was stale (different roster); it is not an exact
HEAD baseline. Rebuilt traffic-after also reproduces queue at end of road41.
Original583350 calls5832A0 on path exhaustion; sets controller+4D1.
Original5988C0 accepts that terminal flag independent of racer distance in
both controller branches. Port currently only recycles by distance and brakes
three metres short forever. Correction retires at actual authored end, not
lookahead admission; validation running. Saved original text in
build/nos-traffic-r3/traffic-manager2.txt and ug0-final-r1/traffic-road-end.txt.
NOS candidate uses explicit normalized interpolated screen UV for all four
passes. Shader compile and graphics verification outstanding; causal fix is
NOT established. Collision adapter still uses OBB witness, original convex
manifold/hard-hit state not integrated; rival launch issue remains OPEN.
No NRO produced or uploaded this turn; no source push/merge/publication.

## nos-traffic-r3 correction batch, 2026-10-15

Local implementation after b0814c9, owner-directed solo work. No publication.
Additional confirmed traffic defect: the driver read alternate_yaw_scale
(CarTypeInfo+270) instead of its steering multiplier (setup+270, record+380).
45F6E2..45F6F5 and docs/STEERING_RECOVERY.md establish that address mapping.
All six selected retail traffic records have inertia2 / steering1. Native
45E970 repeated driver execution with2 reproduces unbounded/wrapped steering;
with1 it settles. Production now reads the selected record+380 at load time,
not a stock constant and not a physics inertia change. Read failures refuse.
Evidence: steering-records.json, driver-scale-native.json, driver-full.txt
under build/nos-traffic-r3. The isolated world7200ticks previously held four
stopped road41 cars. Endpoint retirement removes that queue but exposed one
road39 car steering off road. Correct record selection removes that remaining
stall: all six active at7140, each moving11..20m/s. These are deterministic
port fixture results, not original whole-world trajectory comparisons.
Native terminal retirement4/4PASS for both controller branches, explicit
helper stubs, terminal flag toggled. First native attempt had a declared-range
end too short for ret4; recorded as unperformed, corrected and rerun.

Pair history correction: an overlapping but non-closing first neighbour
previously replaced a body's pre-step pose. Its next pair therefore lost
first-contact search and could choose the vertical minimum penetration axis.
Shared update helper now consumes history only after an applied response.
Regression retained ordinary flat, side and real falling-body collisions;
chain negative reproduces -0.474578m vertical separation, corrected0m.
This closes that causal adapter bug, not original convex-GJK or hard-hit
callback parity. Initial chain fixture mixed stance pitch and pending rigid
import, failed its flat-plane assertion twice; corrected explicit flat-pose
fixture imports both bodies before saving history. No tolerance widened.

NOS now supplies explicit normalized interpolated UV to all blur passes.
Actual desktop GLSL four-pass execution matches the old mathematically correct
reference within one8-bit channel step. Radial offsets/alpha/source timing
unchanged. This isolates a Switch coordinate-path hypothesis; desktop success
DOES NOT prove the reported console vertical smear is fixed. Scene ordering
still keeps car/HUD sharp; the standalone shader diagnostic uses a complete
captured input so its car/HUD are intentionally part of that diagnostic image.

Final native focused CTest race_session/ai_track/speed_blur3/3PASS.
1800tick actual UG0 run with three rivals and traffic completes,35 applied
pair responses, no load/step failure. Six retail traffic wall probes pass,
all old simplified bodies escape, none with retained physical terrain solver.
Native default/Release broad matrices intentionally not rerun under owner's
minimal-check instruction. Existing compiler warnings retained in logs.
SDK race package and shader/GPU-contract checks pending final rebuild at
this checkpoint. Hardware NOS/traffic/impact acceptance outstanding. Original
crash-state rollover/recovery and full convex contact geometry remain open;
this batch does not claim complete original collision behavior.

Final SDK explicit race rebuild + package/embedded-shader/GPU-contract3/3PASS.
NRO build/nos-traffic-r3/nfsu-nos-traffic-r3.nro, 3890661bytes,
SHA256 13df0760292dbd3af7af5fb32a3a1090019235f18e85156eb6835e57ac3f16fd.
Build manifest records production source hashes; no upload yet.
