# Owner difficulty restoration, 2026-10-18

Owner scope: restore adaptive rival setup, original catch-up, and investigate
unused event scalar; consolidate with dirt/shadow candidate before hardware.
Solo work per owner. No upload, source push or main merge requested yet.
Owned tree build/day20261008-root-combined, codex/draw-distance-original,
HEAD ea4d472c9c399265370fa10be94736b1121771ad; pending GLM merge7d18437d.
Prior frozen candidate build/surface-shadow-r1/nfsu-drag-start-surface-r3.nro,
SHA bf538180292cd2a029b0e5a3fb1ec5cfe30b4a27ae4bcf7bd683ae4d885bcfc6.

Confirmed correction to the previous assistant explanation:
- Event raw+13C+4*difficulty is stored in78A380 at4B5F66. Its sole read xref
  is43ED1A in drift opponent score calculation. Existing DriftCompetition
  already consumes it. Do NOT add an ordinary speed multiplier. Owner informed.
  Original ranges/xref evidence are in build/difficulty-original-r1.

Implementation in progress (NOT accepted or ready NRO):
- race_adaptive.inc included in race_setup.cpp ports original57F700. Actual
  player rating via ai_setup_performance; initial5 tolerance, original masks
  6F0F78, driving indices2/5/0/3, secondary1/6/7/8, copied player levels/items,
  nitrous preservation, tolerance doubling. make_race_vehicle_spec evaluates
  chosen car. Original57F2B0 clears mass_item before evaluation. Original
  rating/category thresholds are0/280/390/500, strict comparisons.
- intro_grid now resolves rival performance instead of silently using stock.
  Human reference is setup human performance. Actual owned upgrade override
  still needs checking at callers (first five currently stock; UG0 explicit1).
- ai_catchup has GLOBALB34B00 tuning decoder and human10-sample mean producer
  (430758/584270 cadence/ratio). IntroAiPlanner reads actual rank, lap progress,
  speed and history; selected context controls original tables. Catchup updates
  desired speed and publishes physical_scale in RaceRivalPlan.
- RaceRun reads actual event raw+12C reference weight and supplies actual
  laps/lap_skip to planner. No invented speed caps/tolerances.
- Selector3 ai_track latches actor404 into engine.boost from the plan. Original
  474D58 does this independent of6FFE54. Body/grip keep ordinary initial policy.
  Selector1 drag479160's scaling is disabled by original6FFE54==0; do not
  blindly apply selector3 boost to drag. Native property/runtime coverage open.
- Existing race stand/progress adapter is reused; no whole trajectory parity
  claim. Human road context still uses the existing port road-selection adapter.

Validation state:
- PC build targets career_races_tests and ai_catchup_tests passed build-r1.log
  before native history vectors/test extension. ABI headers changed; Switch
  must rebuild all affected dependencies after the final code batch.
- tools/race_difficulty_native.py is RUNNING in exec session65194, output
  build/difficulty-original-r1/native-comparison-r2.log. It executes original
  57F2B0/57F700 with authenticated real car records,30 player/rival/level cases,
  compares port stdout levels/rating bits, then captures16 original history
  samples to tests/ai_catchup_history_native.inc. No semantic call hooks.
  First run exhausted3M declared instruction budget; rerun30M. Log currently
  at GOLF/MIATA level0. Do not start a second copy while this runs.
- Original supplied-cache catchup13 cases already exist ai_catchup_tests;
  need run against new config decoder, add history vector checks, and perform
  actual campaign runtime checks on Easy/Medium/Hard with resolved setup.
- Needed: inspect native mismatches; fix causally, no tolerances/blind pins.
  Check native history harness stack/entry ranges if capture fails. Then fresh
  PC targets, representative actual races incl dirt/shadow regressions, SDK7
  package gates, ASCII scan, immutable source/NRO seal. Hardware acceptance open.

Next: collect native comparison; implement original-backed fixes and complete
history/config/integration validation. Preserve all failed attempts and prior
source/NRO seals. Owner asked one consolidated result, not partial handoffs.


Continuation and corrections (latest supersedes notes above):
- Native30 adaptive cases now PASS exactly: all11 levels, reference/target
  rating bits, and final three aero coefficient bits. Evidence is
  build/difficulty-original-r2/adaptive-native-r1.json; no semantic hooks.
  The first comparison caught five metadata differences, not rating errors.
- Important offset correction:57F2B0 clears RideInfo+2C/+30, which are the two
  AERO LEVELS, not mass_item. It then adds continuous ordinary aero from the
  normalized weight/suspension/tyre sum and publishes truncated aero levels.
  race_adaptive.inc now reproduces this; mass_item is preserved.
- Original history timer samples only below zero, not at zero. Strict cadence
  and all32 directed steps/16 samples through ring wrap now match original.
  History evidence history-native-r1.json and history-capture-r2.py in r2 folder.
  The earlier capture failures had undeclared observation-boundary instruction
  4308D6; no result from those attempts counts as a history pass.
- PC build-r3 passed, and ai_catchup_tests RETAIL passed all13 original scale
  cases plus32 history steps and original initialized tuning/decoded table
  comparison. The following small fixture changes still require final build:
  UG0 human performance fixture now uses actual setup levels, summary prints
  history step count, parser/traffic comments corrected.
- load_race_grid optionally accepts actual owned performance levels. Switch
  supplies its performance selection; UG0 retains its authored player levels.
- Added deferred AI_DIFFICULTY summary (actual scale min/max/current, updates,
  human history/count, reference weight) and startup rating/context rows.
  No per-frame logging or new passes. SDK must rebuild after these ABI changes.
Next: representative live PC scenarios on all three difficulties including
UG0 and first campaign modes, surface/shadow regression, then fresh SDK NRO.


Final integration follow-through (still awaiting current build/hardware):
- All three profile difficulties passed actual1200-step event1 runtime. UG0
  INTEGRA passed1800 steps; event2 passed1200 steps plus actual dirt response,
  per-car shadow mip metadata/upload checks and production scene validation.
  Drag scene and1200 steps passed on PC; Switch crash remains unlocalized.
- Found and corrected two integration details before publication. Human history
  is now observed independently during countdown as430758 requires, using actual
  owner speed and direction-oriented spatial road/category. Sampling reads no
  RNG and occurs only on its original timer; duplicate same-tick reads are inert.
  Catch-up clock is actual RaceTiming world tick. Road selection remains the
  existing port adapter, not a new original whole-controller parity claim.
- Original57F962 forces rival category to the player's category.45FB30 and
  steering gains consume RideInfo+424. Live driver category must use that actual
  performance category; removed the previously hardcoded2 for rivals/drag human.
  Road/lane categories remain distinct and unchanged.
- SDK switch-build-r1.log passed7 checks BEFORE these two final details. Do not
  seal/deliver that mutable build output as current; fresh final SDK rebuild is
  required after current PC scenarios. No hardware upload has happened.


Current final CPU source validated (build-phase-r2):
- career_races_tests RETAIL event0/1800 steps and events1/2/4/5 at1200 steps
  all PASS, event1 repeated for difficulties0/1/2. Actual current setup, history
  observation during held countdown, original forced driver category and
  actual catch-up clock are included. Logs *final-r2.log in r2 evidence folder.
- Event2 additionally passes production GPU scene, per-car shadow mip upload
  metadata and actual DIRT force/height integration. No original full-race
  balance or all-car collision parity is asserted from these bounded drivers.
- ai_catchup_tests RETAIL:13 original cases,32 original history steps and retail
  table selection match; player_powertrain_tests and shadow_projection_tests
  pass. No skipped test in these direct target runs.
- switch-build-final-r2.log is the fresh in-progress SDK build for this exact
  implementation. Earlier SDK pass is superseded; await its terminal result.
- Event scalar read/write evidence is saved in event-scalar-read-writes-r2.txt.
  Original43ED1A only reads78A380 in drift scoring; existing race_drift.cpp
  already applies event raw13C+4*difficulty. No ordinary speed multiplier added.
- Focused checks per owner iteration; no Default/Release full-matrix rerun,
  independent review, hardware acceptance, source push, merge or release claim.
Next: final SDK result, ASCII/source/NRO seal, then obtain actual Netloader
readiness for a single consolidated owner test. Earlier sending pause remains
until owner says the console is ready. Drag startup journal stays enabled.


Final SDK build terminated exit0: switch-build-final-r2.log,7/7 selected package,
embedded shader and GPU-contract checks PASS. Exact NRO/source identity is in
build/difficulty-original-r2/difficulty-r4-receipt.json and its source manifest.
No upload yet; hardware visual/balance/drag loading acceptance remains pending.

Owner confirmed Netloader readiness and authorized upload. Sent the sealed
nfsu-drag-start-difficulty-r4.nro using nxlink -s -a 192.168.1.56.
Upload record: build/difficulty-original-r2/upload-difficulty-r4-run1.json.
Owned nxlink PID2836, start2026-10-18T15:35:03.5887076Z. Output reached
starting server / server active; stderr empty. No game stdout received at
checkpoint, so successful game startup and gameplay are not asserted.
Keep the log server running for this owner test. Next: collect its actual
runtime log after the owner drive; assess difficulty/dirt/shadows and any drag
startup failure. Frozen source ZIP predates this upload-only checkpoint.