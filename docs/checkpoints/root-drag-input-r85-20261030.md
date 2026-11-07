CHECKPOINT
role: worker
agent: Codex root
assignment: Owner drag9 input/gear delay, excessive initial traffic, round1 ramp/train
phase: investigating
branch: codex/draw-distance-original; publication/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: Sealed r84 source freeze-r3 and owner console run, behavior rejected
worktree: build/day20261008-root-combined
saved: local only; this checkpoint and build/fidelity-r85
ownership: Codex root, active

Owner reports r84 steering/lane input has excessive delay. Gear shifts also
respond late and incorrectly. Then owner reports unrealistic traffic quantity
near the start. The ramp/train question was clarified: second race of
tournament9, race_index1, not standalone event10. Keep all these criteria open.
Validated r84 builds/tests are not behavioral acceptance.

No new original recording or game launch, no delegation, no Git/index/ref or
board mutation, no new console send authorized. Preserve active r84 nxlink
capture; do not kill or replace it. Retail files remain read-only.

Investigating the actual pad/planner/driver/gearbox sequence, not retuning
latency constants by feel. Need sustained input/physical-gear traces rather
than the r84 one-frame opposite-steering regression. Traffic still uses a
port spawn/recycle policy in RaceTraffic, independent of decoded pool size;
original drag-specific admission/density/locations must be recovered.
Second race is track1210 forward per current authored event table; verify
ramp model/collision and train lifecycle from original data/code.

Ghidra bridge currently reports no live instances. The existing analyzed-copy
input and prior ASCII wrappers are available; restart a local analysis backend
if needed. This does not authorize launching the original game process.
Next: save r84 owner log/source identities, recover drag controller and request
handlers, reproduce sustained input against native code, implement and validate
the complete correction before another NRO handoff.

## Local investigation continuation

No production source change, new NRO, Git operation, board write, delegation
or original game launch has happened in this investigation. Owner r84 is
rejected; its successful build is not drag tournament acceptance.

- Restarted the local copied-PE Ghidra backend on port8092 through ASCII
  root-start-ghidra-r1.ps1. Launcher PID107604. The existing input speed.exe
  is a regular copied PE, not a project directory. A run_analysis RPC timed
  out at the client, but the backend log subsequently recorded analysis
  completion; do not claim the RPC itself passed.
- Scratch root-input-probe-r1.cpp runs actual race grid, track and RaceRun
  for each round, with supplied moving human state. It exercises two shift
  presses and sustained right input. All runs completed; this is not a
  natural original race or a human performance benchmark.
- The two presses can move requested gear twice before physical engagement.
  Original461E70 manual mode1 also increments the requested gear, so the
  intermediate user-facing suggestion that this alone proves a bug is not
  established. Original mode3 instead increments published actual gear.
  Missed shifts genuinely slow the clutch via46DFA0. Do not retune rates
  or discard that penalty to hide a caller defect.
- Missing human ai_path_publish was tested in a scratch-only source override.
  root-path-publication-ab-r1.json records it. It slightly changes later
  outputs and does not resolve the large sustained lane response. No such
  patch has been applied to production.
- root-round-lifecycle-r1.cpp loads each real round9 on Hard with actual
  three-rival setups and optional traffic. Human remains idle at start.
  Props are installed through set_intro_traffic in both variants. It is a
  bounded port reproduction, not an original trajectory match. Round0 and
  round1 without traffic finish all rivals. Round0 with traffic resolves
  all rivals as failures. Round2 without traffic resolves two rivals as
  totalled, one near x410.56/y5.51 and one near x32.01/y-4.64. Thus ordinary
  traffic alone cannot explain the third-round report. Log files live in
  build/fidelity-r85/root-round-*-traffic-*-r1.log.
- Original44AA10 selects policy2 via44A190/vtable6C98B0; its virtual14 is
  44DD30. The live drag branch bypasses the retained rival action runtime,
  whose current documented scope is strategy1 only. Drag recovery10,
  contact12, traffic2, wall16, obstacle19 and default8 must be implemented
  with their original selection/publication, not ordinary strategy1 gates.
  Fresh native decompiles and vtable evidence are in build/fidelity-r85.
- Both TRACKB1201 and1210 contain DZCV_Train_EngineA,
  ZCV_Train_FlatbedA, ZCV_XO_Train_Box_Engine and ZCV_TRSABC_Track_Jump.
  Their names alone do not establish activation in a tournament round.
  Original special hash24CEBC53 references428548/454A7E/465DEA/57BE69.
  The owner explicitly means the second round of tournament9 for the ramp
  and train. Current adapter table is1210 reverse,1210 forward,1201 reverse;
  verify actual event assembly before asserting the owner meant another round.

Next: finish obstacle/prop census at third-round failure sites, recover the
policy2 action owner and moving train producers, restore them and sustained
human input/gear timing, then run all required gates on the coherent source.
Hardware, original natural runtime order and independent review remain open.

## Production correction started (supersedes the no-source-change state)

Production and regression changes now exist locally; no final freeze, full
Default/Release validation, SDK build, NRO or upload exists for r85 yet.

Confirmed original caller defects and current edits:

-44CF30 unconditionally calls5A75C0 and44D337 for unavailable categories11..16.
  The port incorrectly restricted future lane masks and adjacent escape to
  traffic policy4. Restored the common owner3/4 gate while owner2 still bypasses
  future masks. The current route header still applies to owner2. The extra
  manual5A7700 divider walk was also incorrectly inserted into44CF30; removed
  it there. Manual drag_lane_step retains its own original divider checks.
- IntroAiPlanner ran human held-lane processing on all drag rivals after the
  context update, overwriting the newly chosen safe lane. It now initializes
  their starting lane once, then retains the common context's category.
  Human held-lane processing stays on actor3 only.
-462390 returns max7006EC/7006F0/2; current defaults give3 seconds.458B90 and
  57A910 use max50/speed*seconds for retained road and neighbor horizons.
  The fixed60-metre rival horizon was incorrect at racing speeds. Added
  ai_road_horizons and connected both road and neighbor inputs. The ordinary
  human camera context's pre-existing horizon is unchanged pending its own
  controller proof.
- The actual controller kind and RideInfo424 are now supplied separately.
  Selected drag lane11..16 must not replace ride category in compact slot0.
  This final ride-slot edit has not yet passed the current targeted run.

Evidence:

- root-native-lane-owner-r1.py executes unchanged44CF30 in the existing
  initialized traffic scene with supplied actor24=3/2. This certifies the
  common owner gate, not a naturally constructed rival race. Both owner
  variants leave a disabled CURRENT header in case1 (16->12). An initial
  proposed assertion that owner2 would retain16 was incorrect and corrected.
- tools/ai_context_horizon_native.py executes unchanged462390/458B90 with
  supplied speed/globals and a declared ST0 result-store stub, no semantic
  result hooks. Its generated fixture is checked by nfsu_ai_road_tests and
  its own registered retail gate.
- root-horizon-ab-r1/r3.json compare full port rounds on the original track
  data. With common lane escape plus speed-dependent horizon all three rivals
  finish all three rounds WITHOUT ambient traffic. Earlier two third-round
  static barrier failures are eliminated. This does not certify traffic:
  the idle-human reproduction still totals all rivals in reverse1210 and one
  in reverse1201. Forward1210 completes with traffic in the latest A/B.
- The old drag branch DOES call ai_plan_three_paths and can choose avoidance
  paths. It lacks the actual retained policy2 action owner; describing it as
  having no avoidance at all would be incorrect.

Tests in progress:

- An initial target CTest regex matched no tests; that is not validation.
- The corrected context/old drag-steering subset passed with retail before
  and after fixtures (root target console evidence). These results predate
  the horizon/ride-slot edits.
- New drag_tournament_paths_retail_tests installs authored props with an
  empty ambient pool, leaves the human idle and requires all three real
  rivals to reach the finish in each actual tournament9 round.
- Its first build used nonexistent RacerStatus::finished; corrected to
  normal_completion. The resulting Not Run CTest entries are failures,
  never passes. Current target build/test is exec session52470, logs
  root-target-build-r2.log/root-target-ctest-r2.log. Do not edit source until
  that run has completed, and inspect actual log summaries.

Next: inspect that run, finish the traffic/policy2 and human input/gear audit,
and establish ramp/train activation/round ordering. Only then freeze source,
run the complete required gates and build the console artifact. The owner
has authorized no new r85 upload. HEAD/index remain at the r84 identities.

## Policy2 and traffic WIP (later resume)

- Target root-target-ctest-r2.log completed8/8, including all three real
  rounds with props and without ambient cars. It predates policy2 WIP.
- Implemented retained drag policy2 selector44DD30 and default8/wall16
  action updates, sharing the ordinary action owner. Added actual wall
  contact metadata. Native selector/action fixtures still need finishing.
- root-policy2-build-r1.log compiled common libraries successfully; the
  subsequent fresh IntroAiPlanner direct diagnostic is not a CMake gate.
  root-policy2-rounds-r1.json: all rivals finish all rounds without traffic;
  traffic leaves two round0 and one round2 totalled. Human idle, Hard,
  conditional test, not natural original gameplay.
- Native59B3E0/59B670/59B6A0 revealed existing type4 path zones hold
  min/max traffic spacing at7C..90.1210 reverse selector1 is95..115;
  current port had only a20m Cartesian traffic separation. Added parsed
  attributes, common zone lookup, native spacing/RNG scalar helpers and
  wiring from event density and borrowed world RNG. Native fixture pending.
- Added race-direction conversion for traffic neighbor progress. This
  needs regression testing, as previously it mixed raw point distances
  with rivals' reversed spline coordinates.
- Do not call traffic fully native: route emitter/scheduler, preferred
  spawn producer and complete activation proof are still incomplete.
- Latest traffic edits are not compiled. No full r85 gate, NRO or upload.
- Still open: native selector/spacing fixtures, human input and shift
  caller comparison, second-round ramp/train activation and full round
  evidence. Recent strong wall metadata is computed but not yet wired to
  native45E970 countersteering. Reviewer/board/Git HOLD remains in force.

## Density/strategy witnesses and limits

- root-density-target-0/1/2-r2.log: configuration/build and selected CTest
  completed12/12, including native density240 and policy2 selector1320
  boundary cases. Neither is a natural original race proof.
- Native selector fixture observes the actual inline obstacle memory read;
  predicates/constructors and allocation are declared boundaries. Its
  initial diagnostic lacked a mapped SEH page; fixed, not a passed run.
- root-density-rounds-r1.json: idle human, all three rivals finish every
  round with and without the real traffic pool. Added pool regressions.
- root-moving3-density-rounds-r1.json is stronger and incomplete: a moving
  human admits more cars. Round2 all rivals finish with traffic; round1
  one rival totals, and the human crashes before all finish in rounds0/1.
  A native traffic collision can legitimately total a rival; no claim of
  full original population/trajectory parity follows. Earlier moving
  probes1/2 held throttle on controller connection, so the intentional
  first-connection latch suppressed it; those were idle cases.
- Final Default/Release/SDK/ASCII gates have not run on this source.
- Second-round metadata is1210 forward, third1201 reverse, checked by
  load_supported_race against retail event slots. Type9 train annotation
  exists only on1201. Actual moving train activation remains unsupported;
  do not assert its absence on1210 merely from that annotation census.

## Frozen validation run and train research

- root-code-freeze-r1.json pins non-document source. Final host gates are
  exec session45404; independent Switch build is session43892. Do not edit
  source until they finish. Check actual logs, not process presence.
- root-ascii-validation-r85-r1.json records Bash HEAD scans, working bytes,
  BOM positive control and unchanged HEAD/index. Re-run after final docs.
- Fresh static train entries:4281E0 event10006 computes the train arrival
  lead from track progress/time; a lane11..16 adds1.2 plus.15 per lane.
  It writes countdown/progress to hash24CEBC53 and57C110 clears object
  flag80 on its TrainCar instances.57BF90 constructs that instance list.
  Saved decompiles are root-native-004281E0/0057BF90/0057C110.txt.
- Both tracks also contain train template hashes in3B300. Only1201 has
  the train hash in34122. Templates/mesh names alone are not proof that a
  train is activated in a given round. Live animated train producer remains
  outside this implementation; the owner's second-round report stays open.
- Human lane/gear latency remains unaccepted. Do not change shift/clutch
  coefficients merely because rapid manual requests queue: native mode1
  permits queuing requested gears. No native complete human timeline has
  yet been compared on the owner's actual pad trace.

## Superseded first freeze

- Stopped only the verified owned gate tree PID85400, created2026-10-30
  22:48:45.906881+03:00, after identifying a missing human driver publication.
  Default CTest was incomplete; it is not a pass. SDK14/14 applies only to
  that old freeze/NRO. No artifacts from that freeze may be sent.
- Native45FAF0 copies the controller's1C0-byte path then459980 resamples
  the driver's copy. Added a separate retained human action path and the
  missing driver publication. Reusing the normalized driver copy as the
  next action's retained source would introduce repeated normalization.
- All required gates must restart from a new freeze after this correction.

## Second freeze and bounded gearbox timeline

- Active source freeze is root-code-freeze-r2.json. Host gate runner is
  exec session19913. SDK session99923 completed build and targeted14/14;
  root-switch-validation-r85-r2.json records commands/results. Host gates
  have not completed yet. No source edits during this run.
- Human driver publication uses a separate raw retention buffer, then the
  authentic driver-copy normalization boundary. The new native caller
  check drag_human_publication_native.py passed seven supplied paths with
  no hooks and asserts that source action bytes are unchanged.
- root-native-shift-timeline-r1.py extracts the existing stock240SX native
  constructor fixture before its case loop. Fixed wheel speed/throttle,
  requested first-to-second and supplied quality values are explicit.
  Both actual46DB60 and46DFA0 execute; external Euler rounding is declared.
- root-compare-shift-timeline-r1.py compares the port's phase/gear event
  steps against that PE result for all four supplied quality values. PASS.
  Evidence is root-native-shift-timeline-r1.json and
  root-port-shift-timeline-r1.log. This is not the owner's live pad trace
  or a full vehicle/shift-quality producer replay. Do not retune clutch rate
  merely to remove the native penalty for a poor shift.

## Superseded second freeze: drag recovery refusal

- Stopped verified owned host gate PID112504, created2026-10-30
  22:59:35.346198+03:00. Its Default CTest was incomplete. SDK14/14 is
  superseded and its NRO must not be sent. No r85 upload has occurred.
- A stuck drag rival could select10, then ai_recovery_plan rejected its
  category11..16. The completion predicate also incorrectly supplied ride
  grade instead of the context road category. Native451D50 indexes the
  raw point's3C+2*category, shared by452070 and451DC0/451CB0.
- Extended recovery geometry/lifecycle to the six drag lanes and corrected
  the caller category.452070's -15/-6 choice reads actor24, controller
  kind, not physical selector28; the existing rival argument3 is correct.
- Extended actual452070 fixture to16 cases with supplied lane categories;
  raw input now preserves six lane offsets as well as five grade offsets.
  Full unchanged PE path execution succeeded. Newly executed instructions
  required exact additional function ranges; initial undeclared-execution
  failures are not passes. Evidence: root-recovery-lanes-native-r1.json.
- Added read-only --check-fixture mode and a retail CTest for the path
  fixture. No complete new target/full gates have run after this change.
- Before freezing again, run focused recovery/action/path checks. Do not
  repeatedly start the expensive full gate while this runtime audit is open.

## Recovery control producer restored before third freeze

- root-recovery-target-2-r1.log was17/18: native path fixture byte comparison
  failed because write_text produced CRLF while check compared LF bytes.
  Changed fixture generation to write_bytes and regenerated it. C++ original
  path comparisons themselves passed. The runner's attempted UTF8-to-cp1251
  error rendering also failed; the saved log remains the actual gate result.
- New controller regression forces old front contact on each lane11..16,
  constructs action10, then tests completion against a lane goal distinct
  from the ride-grade goal. Its elapsed time clears the minimum hold.
- Corrected action10 to preserve the native plan's disabled catchup flag.
- Native RacerController462AE0 returns1.3/1.6 minimum hold, not-1. Its
  condition uses outside-road distance and a full five-sample history below
  .2. Recovered constructor4620C0, updater462430, timer586640 and FIFO584270.
  Timer decrements truncate dt*4000; unlike another history, no+.5 is added.
- Added the authentic433DB4 fractional-progress speed filter, sampled by the
  controller history every1.5s, and retained the constructor-selected hold
  on action10. IntroAi observe_human updates all three rival histories once
  per world frame using completed competitive progress and preceding dt.
- recovery_control_native.py executes the actual timer/ring and rate slice;
  its hold input uses a declared outside-distance fld/ret stub.315 observed
  states compare bitwise to C++ recovery helpers. Full world not claimed.
- Direct diagnostics root-recovery-hold-r1.log and root-strategy-hold-r1.log
  passed. They are not CMake gates. New focused gate runner is exec43334's
  successor: root-recovery-target-0/1/2-r2.log (inspect current tool session).
- Full Default/Release and SDK must run again after the target checks. No
  active source freeze is accepted; first and second NROs are superseded.

## Third freeze: full validation pending

- root-recovery-target-0/1/2-r2.log completed configuration, target build
  and20/20 tests, including all six tournament path/traffic cases, human
  steering, actual native recovery paths and control/history witnesses.
- Active non-document source is root-code-freeze-r3.json. Do not edit code
  during the full run. Host gate tool session16180; SDK session18911.
  Scripts/logs/JSON all end r85-r3. Earlier r1/r2 results are superseded.
- Native history/hold diagnostics and complete controller regression passed
  before this freeze. Do not confuse the earlier direct executables with
  the CMake target run. Original files remain read-only.
- After host six phases and SDK three phases pass, update this checkpoint
  with the actual verdict, then re-read and run root-finalize-r85-r3.py.
  It checks the frozen source, registry/test counts, no skipped retail
  fixture, ASCII/index/HEAD, archives the source and creates out/nfsu-r85.zip.
  It intentionally refuses premature packaging and prior existing outputs.
- Final notes must retain: live console control/gear acceptance pending,
  original full-race trajectory parity unproved, dynamic train producer
  unsupported and reported second-round activation unresolved, independent
  review/publication on HOLD. No r85 upload has been authorized or performed.

## Final r3 validation: 2026-10-31

- Host session16180 completed all six phases successfully. SDK session18911
  completed all three phases successfully at the same non-document freeze.
  Retail checks ran, including retail-full in Release; none were skipped.
- Commands and exit codes are preserved verbatim in
  build/fidelity-r85/root-default-release-validation-r85-r3.json and
  root-switch-validation-r85-r3.json. Their named logs contain full results.
  All recorded commands returned0. No earlier freeze is a final gate result.
- Host commands, from this actual worktree with Strawberry tools on PATH:

```text
cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"
cmake --build build -j 5
ctest --test-dir build --output-on-failure -j 5
cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 5
ctest --test-dir build-release --output-on-failure -j 5
```

- Finalization script root-finalize-r85-r3.py verifies the unchanged source,
  complete test registries, retail fixtures, ASCII and preserved Git state.
  Its success record is build/fidelity-r85/root-final-r85-r3.json; require
  that record before treating out/nfsu-r85.zip as the final local artifact.
  The record also identifies the full local source archive and NRO hash.
- TrainEngine/TrainCar are actual shared3B300 records in both track files,
  not merely named mesh templates. Their presence does not establish the
  reported second-round activation. No live train producer is connected.
- Outstanding acceptance remains live console lane/gear handling, natural
  full-race trajectory parity, train activation and independent review.
  Git/index/board/review remain on HOLD. No r85 upload is authorized.
