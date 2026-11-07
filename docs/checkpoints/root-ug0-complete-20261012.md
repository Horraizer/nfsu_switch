# UG0 coherent completion - solo owner assignment

Owner: complete the introductory race and include Turbo/N2O label corrections.
Solo implementation; no delegated reviewers. Owner requests targeted necessary
checks instead of broad repeated matrices. Working tree: build/day20261008-root-combined.
Base: ea66fbd (N2O), including 90b6b5d (Turbo), accepted audio-all-r1.
Local work only. No upload, source push, merge or hardware acceptance this turn.

## Implemented first batch

- RaceStyle loads circuit rules and English labels from the user's GLOBALB and
  English.bin. Original 436230/436070/436940 accrual, threshold, timeout,
  multiplier, banking and publication order; fixed-size notice queue.
- UG0 RaceRun prepares scoring. Published frame inputs feed powerslide,
  hang time, grounded spin, checkpoint/lap, head start, shortcut and finish
  producers. Pause/countdown cannot accrue. Fresh RaceRun resets scoring.
- HUD_SingleRace StylePointsGroup geometry/colors/fonts and event notice.
- New-career 01_DAY movie before UG0 through the existing streaming MAD/audio
  player. Boot movies unchanged. A/B skip, Plus cancels, decoder errors surface.
- Turbo/N2O fixes remain included. No vehicle physics, accepted audio or rival
  tuning change in this batch.

## Evidence and limits

Original input speed.exe SHA256:
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
Style tuning is GLOBALB chunk34A07; caller43F240 and functions43F430/550/8B0/
9B0/A90/B50. Original native kernel and port match all223 recorded calls.
This is bounded primitive evidence, not full live-game equivalence.

Contact admission remains the existing hull/SAT adapter. Its t*m/s impulses
feed the strong-impact gate; original contact birth/class admission is not
fully reproduced. Ground height uses the port downward track query. Drafting,
near misses, traffic penalties and prestart NOS purge have no invented inputs;
their original live producers remain absent. These are open scoring limits.

FrontB event0 at11744912+AC stores01_DAY. 4E82DF selects copied setup+594C,
4E8420 binds it to the cars el placeholder (FNG object name: carsel).
MU_UG_NewCareerFMV at decoded FrontB BCF10C has640x480 Movie geometry.
Original localization resolver59FD70. Original post-race policy is recorded
in the second-batch section below; ordinary results are not shown on UG0 victory.

Tests run from this actual workspace:
- cmake --build build --target nfsu_intro_grid_viewer nfsu_race_style_tests nfsu_race_hud_tests -j6: PASS after fixing explicit nfsu_race_events link dependency.
- ctest --test-dir build -R '^nfsu_race_(hud_tests|style_retail)$' --output-on-failure:4/4PASS including retail read-only fixture.
- original kernel differential:223/223PASS, no patched original instructions.
- cmake --build build --target nfsu_intro_player_tests nfsu_intro_movie_tests -j6:PASS.
- ctest --test-dir build -R '^nfsu_intro_player_tests$' --output-on-failure:1/1PASS.
- nfsu_intro_movie_tests --inspect <retail>/Movies/01_DAY.mad: complete video/audio decodePASS.
- nfsu_intro_grid_viewer <retail> build/ug0-flow-r1/style-start90:PASS actual CPU frames/HUD and GPU package lifecycle through GO/crowd retirement. Frame visually inspected.
- nfsu_intro_grid_viewer <retail> build/ug0-flow-r1/style-full40000:FAIL player autopilot did not finish two laps. Rivals reached lap2, player lap1 at path parameter1.345. Preserve this failure; no whole-race pass. No physics source edits in this batch.
- Earlier ctest regex without _tests selected zero tests: not a pass.

Evidence: build/style-r1, build/ug0-flow-r1 and root-ug0-*.log in owned tree;
archived receipt under root build-artifacts/draw-distance-20261008-root/ug0-complete-r1.

## Second batch at ee7271c plus local loading/epilogue changes

- PC_Loading.fng in authenticated GLOBALB at230208: original PC_LOAD,
  ChromeGeneric_LoadingBar and Generic_Loading_Bar2. Generic Move activity
  animation uses authored1200Hz/1600tick timing, not an invented completion
  percentage. Loading art is released before GPU upload; error/cancel unwinds
  clear the borrowed Screen pointer. GLOBALA and GLOBALB identities checked.
- Ordinary UG0 menu entry uses this artwork during preparation instead of
  technical screen rows. Explicit profile-frame retains diagnostics.
- Original49E14D skips results for UG0;49E0C9 skips the style reward screen.
  Successful fresh-career event0 selects MU_UG_NewCareerExtroFMV at4AB78B.
  Constructor4E87E3 uses the event+5964 base and4E880A suffix _S for the
  no-owned-starter branch. Port plays02_FAN_s.mad only after first place.
  A/B skip and Plus cancels. Current continuation returns to main menu;
  original starter-car selection remains outside this implemented batch.
- Loss PostRaceMenu has Continue/Statistics/Restart (496658/4969E0). Only
  research is complete. A temporary menu draft was removed from production;
  its patch stays in ignored evidence. The existing diagnostic loss result
  page remains. No claim that the original loss menu or retry is implemented.

Additional actual checks:
- Same unchanged PC driver given120000 maximum steps finished naturally at
  step48707. All four racers completed two laps; finish continuation, shared
  planner/selector3, HUD/style and GPU package lifecycle PASS. The40000 failure
  above remains an insufficient-duration observation, not a physics failure
  and not erased. Evidence: style-complete.log and both fresh TGA frames.
- Actual PC loading mode wrote four activity frames; art-r1-loading1.png was
  visually compared with the owner's loading frame. PASS artwork composition.
- Full02_FAN_s decode PASS425 frames/14180ms and625374 audio frames44100Hz.
- Current native Release targeted build: intro_grid_viewer, frontend_tests,
  intro_player_tests, race_hud_tests, race_style_tests PASS. Targeted CTest
  intro_player_tests/frontend_retail/race_hud_tests/race_style_retail:6/6PASS,
  including retail before/after fixtures. No native full gate claim.
- Real SDK configure/build nfsu_switch_race_nro PASS tagug0-flow-r1.
  Complete SDK CTest first run52/53: texture comparison diagnostic retained
  its prior build tag, because only the race target had been rebuilt.
  Rebuilt nfsu_switch_texture_compare_nro and re-ran exactly that test:1/1PASS.
  Thus all53 checks passed across the original run and this targeted recheck;
  original failed log retained. Race NRO/shader/identity checks passed first run.
- No new NRO deployment or Switch execution yet. Native full Default/Release
  gates and hardware remain unperformed for this batch; not merge-ready.

## Still required

Original start camera, loss-menu/restart and career continuation flow; traffic
and its style inputs; actual Switch validation of the new presentation. Tachometer/acceleration deep fidelity remains the owner's
separate deferred backlog. No claim of complete UG0 or hardware acceptance.
Next: hardware-check ug0-flow-r1 presentation, then finish the researched
loss-menu/restart and start-camera paths. Local candidate is a partial batch.

## Owner continuation: finish the whole package before hardware testing

The owner rejected partial delivery: finish the introductory race first, then
perform one combined hardware check. This supersedes the prior "Next" line.
Do not deploy ug0-flow-r1 or ask for Netloader while software scope remains.
Solo sequential work; no reviewer/worker dispatch or new publication.

Current local uncommitted continuation at78aef92:
- Original PostRaceMenu/Stats packages decoded from authenticated INGAMEB.
- Original496658 circuit menu has Continue/Statistics/Restart.
-49CEE0/49D4C0/49D5B0/49DA50/49DB30/49DC20/49DD10 bind circuit stats:
 finish time, time behind, best lap, top/average speed, odometer, laps led.
- PC menu/stats selection smoke passes and four fresh pages render.
- Switch post-race overlay transition and full-session restart are now wired,
 not yet SDK verified. Only HUD GPU allocations retire after queue drain;
 the final world frame remains resident. Statistics adapter measures fixed
 physics ticks; no retail telemetry bit-parity claim.
- Remaining: complete/check menu lifecycle, original starting camera, actual
 traffic. Then one coherent native/SDK candidate and owner hardware check.
- Evidence and scratch: owned build/ug0-final-r1; no retail files changed.

## Continuation: post-race integration and original DDay start

Local software progress, not complete/hardware accepted:
- PostRaceMenu now replaces the normal UG0 loss diagnostic screen. Its stats
 page uses the circuit binding from49CEE0; Continue returns to the frontend,
 Restart unwinds the old race and constructs a fresh complete RaceRun.
- Original47C020 executed against the authenticated scene2338 descriptor
 returns0: both camera selector guards (+60,+6C) are zero. Do not invent a
 flyby for this event. The scalar-only IntroNis28 camera bank is not evidence
 that a cinematic camera was selected by this caller.
- Original47F0AC executed with the same descriptor gives countdown float bits
 3F555556 (one third of75/30 seconds), release0, scene end215/30. Native witness
 uses a declared empty old-camera-list teardown stub and supplied manager/
 race objects; not a live original gameplay execution.
- RaceRun reads the scene header without retaining another stream buffer and
 supplies the countdown to the player and every rival. Other events keep their
 existing countdown. Crowd lifetime stays tied to the authored post-GO end.
- Fresh actual PC complete-r1 run reaches finished with all4 racers/2laps;
 real result page binds lap1/lap2, fourth place, finish/gap/best/speed/distance.
- PC post-r2 menu/statistics selection PASS; screenshots inspected. Circuit
 labels replace the template's drag labels. The original position/font/art
 assets are used. Measured speed/odometer are a port statistics adapter.
- SDK build after post-menu integration PASS; countdown successor SDK build
 still pending. Final Default/Release/SDK gates and hardware remain pending.
- Traffic actor/control/activation research is active. No decorative traffic
 or unsupported always-active six-car loop has been added.

Evidence: build/ug0-final-r1/post-r2*, complete-r1*, start-scene-native.json,
nis-select-camera.txt, nis-camera-tick.txt, traffic-*.txt. Source and evidence
remain local only. Next: actual traffic lifecycle/control implementation,
then one coherent candidate's required gates, then owner hardware check.

## Traffic integration continuation (local WIP after bc7f19e)

Owner asked to finish the complete standalone event before the next hardware
check. Solo/sequential, no reviewers, no publication or upload in this turn.

Implemented but not release accepted:
- Explicit class2 asset resolution; all15 traffic models/specs loaded on PC.
- Six configurations from the authenticated UG0 descriptor, deterministic
  class2 selection without duplicates. Seed/call ordering is a port adapter.
- Original5754B0 planar TrafficMover and45E970 ordinary mode3 driver. Bounded
  original execution:54 mover cases with maximum position error below2e-6;
  60 driver snapshots match steering words/pedals. Supplied objects only;
  these are not original full-world trajectory equivalence.
- Native driver first comparison exposed missing fixture ride+270 and missing
  traffic-specific steering curve/yaw multiplication; corrected before PASS.
- Road/lane pool, one slot/s recycling, staggered path decisions, contact
  integration and immutable rendering payloads now connected to UG0.
- PC traffic-r1 refused missing SUV_SHADOWIG before simulation. Original
  texture lookup5461C0 with missing fallback disabled returns null; draw
  56F22F skips null shadow texture. The sampled SUV/TAXI/BUS assets have no named SHADOWIG
  textures; this is not an exhaustive proof for every class2 asset. Traffic rendering therefore omits this layer, not a fabricated
  replacement. Existing racer shadows remain unchanged.
- PC traffic-r2 completed1800ticks, actual traffic contacts and GPU resource/
  dynamic-prefix validation. This is an integration smoke, not full acceptance.

Unverified/incomplete: full traffic path/lifecycle adequacy, optional type4
junction choices, exact native live spawn/retire policy and hard-hit mover
replacement, rival predictive traffic avoidance. The current explicit port
adapters are not claimed as original parity. Missing ground stops traffic;
never teleports a visible actor. Complete race and screenshots still required.
Default/Release/SDK final gates and owner hardware acceptance pending.

Recovery: owned build/ug0-final-r1/traffic-* logs/native packets; ignored
build/root-traffic-*.py are diagnostic/edit scripts, never blindly rerun edits.
Next: PC full-race and visible traffic verification; fix actual failures, then
seal one coherent candidate. Current source is local-only uncommitted WIP.

## Traffic completion and recovery checks (local continuation)

- Original road policy583BB0 reads event+0C, unlike racer+0B. Fixed traffic
  policy, retained5A53C0 directional end marker+0B. Authentic54 mover/60 driver/
  7 prediction/5 road-policy witnesses saved in traffic-native-final-r1.json.
- Rival avoidance now includes actual traffic body bounds and ordinary native
  constant-velocity prediction. Ten-actor path tests and causal controls pass.
- Actual r4 run exposed reset-pending rigid-body pair refusal. Import only
  actual overlapping bodies; distant reset stays untouched. Regression passes.
- r5 full run finished all four racers/two laps but slow host driver collided
  repeatedly. After road-policy correction, r6/r7b unassisted host driver did
  not finish within120000 ticks; all rivals finished. These failures remain.
- Added explicitly assisted host-only complete scenario: ordinary reset button
  after300 stopped frames. No direct pose/lap/finish write. This does not count
  as unassisted full-race acceptance.
- r8 assisted run exposed fatal outside-index wheel contact after an impact.
  Valid outside query now uses the miss path; refused/malformed queries still
  fail. Existing32-frame bounds owner resets next frame. Fresh targeted AI
  track test PASS, including pending/reset/recovery and untouched gate timing.
- SDK race build passed. First53-test run had two failures: stale diagnostic
  NRO tag and frontend profile static-call witness. Rebuilt diagnostic target,
  updated narrow post-art call/restart witness; both targeted checks now PASS.
  Final race binary rebuild after outside-contact fix remains required.
- Complete Default inventory running (session75036), with inherited handling,
  frame/hash and static-classification failures. Do not call this full gate
  green. Source-level attribution and final inventory remain to record.
- Fresh r9 assisted race running (session58001); source remains local WIP.

Remaining fidelity limits: port spawn/retire and obstacle policy, fixed junction
choice lifetime, impacted traffic fallback model rather than native mover
replacement, no traffic-to-traffic physical solver, no native slope-tail parity.
These are explicit adapters, not original full traffic equivalence. Hardware
performance/appearance and complete owner acceptance remain unverified.
Next: finish r9 actual race, inspect visible traffic and result UI, preserve
full inventory, rebuild final NRO, ASCII scan and local commit. No publication.

## Frozen candidate follow-up

- r9 assisted complete scenario PASS after outside-query correction. All four
  racers finished two laps, result bindings and persistent GPU resources
  verified. It used25 ordinary reset inputs over62521 ticks. This is explicitly
  assisted completion, not proof of a clean unassisted lap or original traffic
  fidelity. r6/r7b unassisted failures remain preserved.
- traffic-visible-r1 CPU frame inspected: taxi, fire truck and pickup on road;
  traffic view is a diagnostic camera, HUD still belongs to the player.
  Actual race statistics page also inspected after r9 finish.
- Complete Default inventory:153/188PASS,35FAIL, no skipped retail fixture.
  Exact failures in build/ug0-final-r1/validation-inventory.json and full log.
  Prior handling/sampler/hash expectations are implicated, but not all have
  parent-runtime attribution; do not collectively call them proven inherited.
- Fresh Default outside-contact regression1/1PASS. Release full inventory is
  running once (session70501), no repeated complete matrix authorized here.
- Final SDK race rebuild-r3 PASS, current race NRO and frontend lifecycle
  targeted checks2/2PASS. Earlier53 inventory51PASS plus repaired2PASS remains
  separately recorded. Final native gate failure prevents release acceptance;
  any NRO delivered is a development candidate for the owner's requested test.
- Current tracked/new source bytes and new ASCII path names scanned clean;
  SDK automation ran from actual workspace. Final commit-tree scan still due.

No source push/merge or Netloader upload. Owner hardware check remains pending.
The accepted audio and Turbo/N2O label commits remain in this candidate.

## Final local development artifact at e6d8cca

Source frozen at e6d8cca070139a77dd8beb28acb0ffa4c1cf486b. No source push,
merge or hardware upload. This is a WIP development candidate, not a statement
that the original introductory event is fully reproduced or all gates pass.

Commands and results, from the actual owned workspace:
- cmake -S . -B build-mips-default -G Ninja -DCMAKE_BUILD_TYPE= -DNFSU_RETAIL_ROOT=<read-only PC1.4USA>: PASS.
- cmake --build build-mips-default -j6: PASS.
- ctest --test-dir build-mips-default --output-on-failure: FAIL,153/188PASS,35FAIL.
- cmake --build build-mips-default --target nfsu_ai_track_tests -j3: PASS after final outside-query delta.
- ctest --test-dir build-mips-default -R '^nfsu_ai_track_tests$' --output-on-failure:1/1PASS.
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFSU_RETAIL_ROOT=<read-only PC1.4USA>: PASS.
- cmake --build build -j6: PASS.
- ctest --test-dir build --output-on-failure: FAIL,154/193PASS,39FAIL; no skipped test. Both read-only fixtures PASS. Four additional retail-full smoke-frame assertions FAIL; full track slice PASS.
- SDK build/race package and post-race lifecycle tests: PASS as recorded above.
- nfsu_intro_grid_viewer <retail> build/ug0-final-r1/traffic-assets-final 0 traffic-assets:15/15assets PASS; failure exit now propagates in this diagnostic.
- git diff --check: PASS. Canonical Bash ASCII tracked paths/blobs and forbidden modes: all no matches, PASS.

Nine relevant preexisting physics/check source files are byte-identical to
accepted audio/HUD base ea66fbd. This is source attribution, not a parent-runtime
full-gate pass. All failing tests remain visible and unchanged; no blind pins
or criterion relaxation. Full repository release acceptance is still blocked.

Artifact recovery, relative to main repository:
build-artifacts/draw-distance-20261008-root/ug0-complete-r1-final/
- nfsu-ug0-complete-r1.nro; embedded tag ug0-complete-r1.
- candidate.json holds exact binary hash/size/source and open limits.
- receipt.json seals copied logs, native evidence and two inspected PC frames.
- validation-inventory.json records both complete failure inventories.

Outstanding: owner hardware runtime/visual/performance acceptance; unassisted
host driver completion; original traffic admission/impact/lifecycle and absent
style producers listed above; full repository gate failures. No active build
or test process remains after the final inventory. No reviewer was delegated.
