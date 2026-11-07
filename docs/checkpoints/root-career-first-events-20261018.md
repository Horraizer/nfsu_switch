# First career events, 2026-10-18

Owner scope: restore the campaign events leading to the first tournament.
Solo implementation in build/day20261008-root-combined; preserve accepted
drift-civic-r9 and all deferred HUD/impact-audio debt. Tournament inclusion
resolved by owner: events1..5 and unlock event6 only, no tournament races.

Baseline: d876814abc0500117e3f5b2e08bba1ad43580b68. Owner hardware evidence
confirms drift event3 starts and finishes; full fidelity is not asserted.
Fetched origin on2026-10-18: main2183f0e. Track stack head
origin/codex/day20261018-glm-timetrial =7d18437db01bd799bb4ac20bd759055f18b02e47.
GLM's tournament worktree has uncommitted active work; do not modify it.

Verified boundaries: career map currently starts only event3; rewards commit
only event3. IntroAiPlanner hardcodes TRACKB1003; rival grid is UG0-only.
GLM restored track loading, not campaign progression or complete drag/knockout
rules. First campaign events from retail:1 circuit1003 fwd;2 sprint1102 fwd;
3 drift1301 fwd;4 drag1210 reverse;5 knockout1003 reverse. Event6 requires
wins in all five; its three circuits are1003 fwd,1003 rev,1006 fwd.

Local candidate: ea4d472 plus an uncommitted merge of the pinned GLM head
above. Conflicts resolved; later source changes remain unstaged. This is WIP,
not a completed integration. The other GLM worktree remains untouched.

Implemented: original first-event rosters/grid markers/difficulty contexts,
per-event track selection for rivals/traffic/props, forward/reverse gap maps,
career rewards and persisted wins1..5, event6 unlock, original track labels.
Native4B3AE6 confirms traffic selector0 means no traffic; events1,2,4 have
none, event5 uses its authored nonzero density. Knockout lap policy is added
but not yet tested or integrated into all result/control paths.

Local evidence: build/career-first-events-r1. build-career-r5.log succeeded;
career tests passed including loss/reward/reload and event6 prerequisite
coverage. event1-r3.log and event2-r3.log passed900-step bounded runs.
event5-r2.log passed600 steps before reverse AI adaptation (not fidelity).
event4-r3.log fails loading a prop definition/shape; preserve this failure.
Earlier build and fixture failures remain in the same evidence directory.

Remaining: reverse AI geometry/category/traversal, drag mode rules and HUD,
knockout elimination/results, Switch generic campaign dispatch, finish/save
round trips for all modes, candidate PC runtime and required Switch packaging.
Next: diagnose event4 prop identity, finish reverse-road adapter using original
4551E0/4553D0/442330/5A7020/5A7050, then integrate mode-specific rules.
No hardware upload, source push, main merge or completed-campaign claim.


Continuation: reverse private road view and direction-aware decisions implemented;
knockout policy tested. Map1/2/4 resource selection and Switch map dispatch are
implemented but Switch not built. TRACKB1210 missing prop definitions are skipped
per original595D80, not treated as fatal (existing shapes remain required).
Event1 rivals complete two laps; event5 knockout eliminates fourth on third
racer crossing lap1. Host RouteDriver is a test fixture and gets stuck on scenery;
these runs are not human driving acceptance. Event5 one AI still gets stuck.

Confirmed sprint failure and correction: rivals crossed authored finish86.2863
percent and then stopped at the wall AFTER the finish. Missing434C37..434C42
GO transition left progress.lap=-1 on open courses. Timing and physical progress
now reset to zero at GO; crossing differences wrap only closed paths as433650.
Build r9 and race_timing_tests pass. sprint-finish-r1.log records all three rival
finishes. Earlier claim of a route through a pre-finish barrier is superseded.
Original progress-crossings.asm and stage-transition disassembly inspected.

Drag remains incomplete: originalcontroller2 uses45D800/45D970 lane control,
4603D0 human pedals plus AI steering; original425150 selects physics1 also for
rivals in drag. Current candidate still uses ordinary racing controls there.
No ready-NRO claim. Next: implement original lane state, drag driver/physics
configuration, mode HUD and finish rules; then targeted PC and Switch packaging.
Road event progress initialization (442150) is also absent in existing adapter;
raw progress is zero. It needs proper route projection before cache expiration.


Road initialization continuation: original4430B0/442980/442150 recovered.
Open spline projection now extrapolates and retains the original final segment;
node0 remains a null seed. Source/event projections match native bits on1003,
1102,1210 (288 scalar results). Cases live in career_road_native_cases.inc;
career_road_*_retail and career_retail passed with read-only manifest fixtures.
No whole-trajectory claim. Native script tools/career_road_trace.py added;
its prototype was run, the tracked entry point still needs its own smoke run.

Reverse knockout bounded run knockout-road-init-r1.log: one rival PRISS remains
stuck near803.425,365.059,49.7968. No recovery fix or full knockout acceptance.
Drag lane primitives and shared category11..16 support implemented, synthetic
road tests pass. Not runtime-wired yet. AI path tests pass. Neighbor tests found
an obsolete unsupported-category11 assertion; changed it to unsupported10,
not yet rerun. Native road initialization cases and career progression passed.
No Switch candidate built. Next: wire drag player/rival lane controls and
selector1, validate original driver branches, then mode HUD and runtime checks.


Continuation: tracked career_road_trace.py smoke now passed all three tracks;
neighbor invalid-category10 test passed, ai_driver_tests3885 checks passed.
Human drag lane steering and selector1 physics for all four actors are wired.
Original cold-engine drag driver144 ground/144 air vectors match the shared
steering/pedal kernel (drag-driver-native-r5.txt, drag-driver-air-r1.txt).
This excludes overheat, collision override and full drag strategy/trajectory.

Confirmed drag placement defect: raw1210 backward grid is above a roof whose
polygon flags are0x18. Original4407B0 excludes flags&0x0A from the ground
partition consumed by440A40. Our ray accepted it as a horizontal road.
Grid placement now applies this eligibility predicate. Native original440A40
returned the actual lower road for all four supplied grid coordinates:
native-drag-grid-r3.json/log. r1/r2 failed fixture setup and remain failures.
PC drag-grid-flags-r1.log now has four supported cars and movement; before,
all four were stuck above the road. No whole-query equivalence claim.

Still open: stock Civic/Golf launch stalls near walking speed; drag AI uses
ordinary speed/path strategy and caps near19m/s; manual shifts/engine heat,
drag HUD, KO stuck rival/eliminated control, mode start/finish presentation,
map atlas runtime, all-event completion and Switch package/hardware checks.
No NRO ready, no upload/push/merge. Next: inspect original drag action2 speed
and stock launch driver/engine transition before marking drag playable.

Continuation: drag launch and finish now exercise a complete bounded PC race.
Original44B1EC launch lead uses selected difficulty row77A014/18; recovered
4528B0 drag action requests explicit1000 speed without route-speed scaling.
Four cars now warm up on the grid, then use selector1 and lane controls.
Action17 speed0 after finish/KO is sourced from435323 and4537D0. The manual
fixture avoids issuing shift edges during countdown and allows shift settling.
Evidence drag-finish-r1.log: actual finish, no reset; this is a fixture result,
not Switch acceptance or full original drag strategy/engine/HUD equivalence.
AI action tests passed after launch configuration changes.

KO probe confirms stuck rival is against geometry, with valid road/path and
road-class0 (target override), so ordinary path planning cannot release it.
Persistent world contact records now feed the existing native-backed recovery
predicate/path/lifetime code. Integration is UNVERIFIED while build/run
ko-recovery-r3 is pending. No arbitrary teleport/speed/timeout tuning added.
Remaining: recovery integration verification, drag heat/shift/HUD, mode start
presentation, map runtime, full-event result/save checks and Switch NRO.
Local-only working tree, no upload, push or main merge.

Continuation (drag HUD and recovery): native45E82C reverses summed wheel slip
when signed speed is negative. ai_driver now does so;224 supplied-state native
updates added to reverse tests,4559 checks pass. Original stuck KO rival now
backs out, later flips and remains unsupported. Upside-down recovery remains
open; no full knockout trajectory acceptance. Recovery exit keeps path geometry
and resets only descriptor1, fixing an accidental empty published path.

Sprint TRACK1102_MAP is accepted by the renderer;1210 has no authored minimap
and no fabricated map is loaded. Targeted sprint/drag load checks passed.
Drag HUD now loads actual drag_HUD_test.fng objects and original textures;
drag-only atlas grows to1024 rows. Default HUD tests pass, drag bounded full
race reaches finish. Current pivot correction is building (drag-hud-build-r3).
4F1810 native matrices confirm position+pivot-R*pivot. Crucial correction to
previous research: drag uses4A4A50/4A57D0 and CarType+C0C/C10 RPM angles, NOT
ordinary4A0F50/4A0EA0. The old assumption was wrong. Full disassembly retained
in drag-hud-component.asm; native matrix evidence drag-pivot-native-r1.json.
Runtime loader now reads the selected car's angles. Screenshot verification
pending. Heat/shift state, sprint HUD and further mode presentation remain open.
No Switch build, upload, source push or main merge. Owner scope remains events
1..5 and unlock6 only. Drift HUD timing and missing impact sound remain deferred.


Continuation: drag-hud-r4 PC capture confirms corrected needle pivot and
selected-car angular range. Drag engine heat/quality are now integrated.
Native46CA90/46E940 supplied-state oracle passed960 heat updates and384
shift-quality cases;90 native46DEC3 torque-pulse states also match. See
career_drag_native.py, generated tests/career_drag_native_cases.inc and
drag-engine-native-r2.json. No native call hooks. Default player powertrain
tests pass. World clock/grid heat hold and reason6 engine failure added from
4357F2..43584B; engine failure has no valid finish time or career victory.
Drag-engine-r3 full PC fixture completed event4, no resets. This is a bounded
fixture, not original whole-trajectory equivalence or hardware acceptance.

Drag authored ShiftMessage ZoomIn and overheat Blink animations are loading
through the existing animation parser, without changing drift publication.
New drag HUD load currently FAILS atlas capacity after adding warning art;
fix packing before producing a candidate. drag-hud-r5 andr6 preserve failures.
One HUD test invocation used the wrong CLI; it is not a passed check.
No Switch build yet. Remaining: coherent HUD load, mode presentation, targeted
campaign result/runtime checks and Switch package. No upload/push/main merge.


Latest checkpoint: career-five-r1 candidate, 2026-10-18
-----------------------------------------------------
Scope remains events1..5 and unlock6, tournament play excluded. Solo owner
workflow; targeted validation only, no new review session or full matrix.
The prior atlas failure is fixed by packing drag resources into space occupied
by ordinary dial art, preserving shared font glyphs and the lap white tile.
The drag atlas remains1024x1024. drag-hud-r7 loads and its HUD-only capture was
inspected. ShiftMessage ZoomIn and overheat Blink are integrated. Earlier
failed logs remain preserved and are not counted as passes.

Validation on the local candidate:
- cmake --build build --target nfsu_career_races_tests -j6: PASS.
- nfsu_player_powertrain_tests: PASS including native heat/quality/boost cases.
- nfsu_race_hud_tests: PASS (no arguments); nfsu_race_timing_tests: PASS.
- nfsu_career_tests RETAIL: PASS, career-results-r2.log. Wins/losses, rewards,
  five-win prerequisite, reload and interrupted-write handling covered.
- NFSU_CAREER_SCENE_CHECK=1 nfsu_career_races_tests RETAIL EVENT 600,
  EVENT1,2,4,5: all PASS, scene-event*-r1.log. Production scene loading,
  authored three-rival resources, traffic where present, HUD, nonempty car
  and track draw commands. This is not a GPU screenshot or Switch run.
- nfsu_career_races_tests RETAIL4 6000: PASS full drag fixture finish;
  drag-engine-r3.log. Native-backed heat/shift pulse integrated, no resets.
- nfsu_career_races_tests RETAIL5 18000: bounded run completes by player KO;
  ko-final-r1.log. A flipped rival remains unsupported until elimination.
  This is NOT a full human knockout playthrough or recovered rollover policy.
- build/career-first-events-r1/build-switch-r1.sh: PASS build and7/7 selected
  Switch NRO/shader/GPU-contract package checks. Embedded tag career-five-r1.

Remaining fidelity limits, not hidden by the package result:
- Rival rollover recovery remains unported; reverse road/stuck backing
  correction does not solve a fully overturned car. Existing collision debt.
- Drag special manual clutch/missed-shift penalty and full AI shift strategy
  remain incomplete; Shift_light/progress presentation is not yet restored.
- Sprint still uses the common lap panel. New-event authored crowd/camera
  sequences remain unported (existing UG0/drift start sequences retained).
- Full first-five Switch start/play/finish/save/unlock acceptance remains open.
- Drift HUD animation mismatch and impact-audio loss stay deferred per owner.
No current upload authorization, no source push, no main merge. The candidate
is for integrated hardware testing, not a complete original-parity release.
Next: owner-approved Netloader test of campaign selection/new modes, especially
sprint/drag/knockout transitions and the final tournament unlock. Preserve
accepted drift-civic-r9. Local source/evidence seal accompanies career-five-r1.


Owner hardware feedback after career-five-r1:
- Circuit and sprint work. Drag fails to load/crashes (confirmed by owner).
- Owner used Netloader. The saved run1 log contains menu presentation only,
  no career/grid/drag attempt, so the failure phase is NOT established.
- Do not claim the drag loader fixed from desktop tests or infer OOM.

Current drag-start-r2 local candidate:
- Dedicated drag controls: R up, Switch A down, L nitrous; ordinary controls
  unchanged. NitrousControls previously reserved L when NOS installed,
  suppressing downshift. This did not explain the reported loading failure.
- Targeted nitrous controls tests pass with/without NOS, including A/R/L.
- Production scene/resource and first-step drag tests passed on all five
  starter cars: GOLF,MIATA,NEON,PEUGOT,CIVIC (drag-start-*-r2.log).
- Opt-in-by-build-tag drag loading journal now records/syncs stages and heap
  totals on SD plus stdout. Files sdmc:/nfsu-drag-startup-NNNN.log use O_EXCL;
  older attempts preserved. Journal closes before gameplay. Diagnostics may
  perturb loading time; no physics or speed changes.
- build-switch-drag-start-r2.sh: SDK compilation and selected7/7 package,
  shader and GPU-contract checks passed. Hardware crash remains unverified.

Sprint surface finding: actual TRACKB1102 centerline samples79.58..84.58pct
hit material8 DIRT before finish. Original table6EF2D0, stride94hex: DIRT
static factor0.97, sliding0.957875, rolling1.0. Actual587546/5878A2 consumers
apply sliding/static factors. This is static material-table/consumer evidence,
not full runtime material-modulation or whole-race comparison. Ordinary port
TrackVehicleGround currently publishes surface0; player tyre work uses unit
material factors. No artificial speed cap or surface tuning added. Preserved
sprint-surface-original-r1.json, sprint-surface-r1.log, surface-original-r1.asm.
Next: one owner-approved Netloader drag attempt with r2 to establish the last
completed stage, then correct its evidenced failure. Campaign hardware
acceptance and original-fidelity limits in the previous checkpoint stay open.


Surface/shadow owner task, 2026-10-18 (in progress):
- Owner requested actual sprint dirt response and all three stock-rival shadow
  differences: shape/size, offset and opacity. No new upload authorization.
- Ordinary TrackVehicleGround now publishes retail material IDs. Player tyre
  force/limit/rolling derivative uses the original29 material factors. The
  positional441D80 bump is applied at each strut hub. Wheel material persists
  with the prepared force epoch; no artificial speed cap or grip tuning.
- tools/vehicle_surface_native.py authenticates executable/GLOBALB and executes
  original441D80,587330,587830..587A91,587170 with supplied ordinary wheel setup.
  Accepted evidence: build/surface-shadow-r1/surface-native-r6.json, exact116
  bump cases and87 wheel cases. This is bounded operation evidence, not a full
  original road-query/trajectory or AI-selector3 material claim.
- Player tests and shadow projection tests pass (player-r2.log,shadow-r2.log).
  Actual sprint dirt drive passes (sprint-dirt-r1.log), with material8 reaching
  ordinary wheel work and a measurable response against the old unit-material
  path. Header changes require a fresh Switch ABI rebuild, still pending.
- Stock CIVIC/NEON/GOLF/PEUGOT bounds match original567890 reducer plus padding
  bitwise for independently read selected solid headers; see stock-shadow-
  bounds-native-r1.json. Correct per-car SHADOWIG texture already selected.
- Corrected original674898 alpha truncation/multiply order. This is a small
  opacity correction and DOES NOT explain the whole owner shadow complaint.
- Found authored shadow mips discarded by decode/upload (Civic/Neon4 levels,
  Golf/Peugot1). Projection currently uses a vertical segment; original440A40
  has a cached ground-polygon query and flags filter. Investigating these and
  preparing an actual PC stock-grid frame. No full shadow parity claim yet.
- Main checkout untouched. Owned tree merge remains unresolved as a Git commit;
  do not reset staged GLM integration. Drag crash remains unlocalized and its
  diagnostic r2 candidate preserved. Owner deferred Netloader sending.


Surface/shadow consolidated local candidate, 2026-10-18:
- Added per-car rendering-only CarShadowGroundCache. Original4400E0 edge
  test and440A40 selection now retain strict-interior plane, ordered ground
  list, two-pass height selection and move-to-front beyond index4.4407B0's
  flags&0x0A road partition and pre-partition160 cap are retained; radius8
  refresh has0.5 expansion. The collision index/decode remain port adapters.
  This changes shadows for player, rivals and traffic, not body contacts.
- tools/car_shadow_ground_native.py generates13 supplied-cache cases directly
  from authenticated original440A40/4400E0, no call hooks. Exact height bits,
  chosen polygon and retained cache match in car_shadow_projection_tests.
  shadow-ground-native-r2.json is accepted; r1 exhausted a declared range and
  is not a passing run. Tests also retain sampled-grid/fallback/strip cases.
- Preserve authored SHADOWIG mip chains through decode AND upload. Stock grid
  probe verifies metadata-to-asset-to-upload chain for all four models. Alpha
  uses original positive truncation. Correct texture/selected bounds are
  original-backed; this is not a claim that all hardware visual differences
  reported by owner have been accepted.
- Reproducible bounds witness saved as build/surface-shadow-r1/stock-shadow-
  bounds-native-r2.py/json: four stock selected solid bounds agree bitwise.
- Actual PC stock-grid render: stock-grid-r4.ppm/png and stock-grid-r4.log.
  All four shadows have all three strips; sampled terrain follows the road.
  CPU preview expands GPU-block track textures to RGBA only in optional test
  mode, updating texture byte accounting; no production texture policy change.
- Native material witness and player tests remain as above. After shadow/cache
  changes, sprint-dirt-r2.log passes1200 event2 steps, actual dirt integration,
  and production GPU scene/upload validation. Shadow tests pass shadow-r3.log.
- SDK build tag drag-start-surface-r3 preserves pending drag startup journal.
  Fresh dependent compilation and seven selected package/shader/GPU-contract
  tests pass in switch-build-r1.log. This is focused owner iteration, not a
  new Default/Release full-matrix claim or merge-ready independent verdict.
- ASCII tracked r1 scan is INVALID: missing grep on PATH was hidden inside
  shell if pipelines. scan-surface-shadow-r2.sh checks executable presence and
  sets MSYS PATH; ascii-tracked-r2.log is the valid passing scan. New candidate
  file names/bytes are separately scanned when sealing the source snapshot.
- No Netloader upload, source push or main merge. Owner deferred sending.
  Hardware dirt/shadow acceptance remains open. Drag crash still needs the
  deferred diagnostic attempt; do not claim this batch fixes that crash.
Next: owner-authorized Switch run of preserved NRO: sprint dirt, stock rivals
and pending drag loading journal. Deferred drift UI/impact-audio debts remain.
