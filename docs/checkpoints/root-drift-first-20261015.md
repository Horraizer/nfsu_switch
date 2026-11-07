# First campaign drift restoration - 2026-10-15

- Owner assignment: restore the first campaign drift, then the initial circuit,
  knockout and sprint events; original handling, scoring, HUD and detailed
  Markdown scoring article. Solo implementation; no console upload yet.
- Owned tree: build/day20261008-root-combined, codex/draw-distance-original.
- Base: df08a6f537e10738f5b27fa31e2abc37774282ed (menu-camera-r2, not sent).
- Current phase: recovering original drift producer and integrating track 1301.
- GLM source: 6c987ce6c2f4acce7c1a38eee0681c32ffea864e. Its parent
  includes circuit changes absent from this tree. The no-commit cherry-pick
  conflicted; all and only its paths were restored. No conflicts remain.
  Adapt the drift delta without importing unrelated circuit changes.
- Verified from retail event parsing: event 3, drift type 3, track 1301,
  L1RB, four forward laps; prerequisite event 0.
- Original image: PC USA 1.4 speed.exe, SHA256
  c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
- Original scoring object: race actor +0x9e0; constructor 43DC40,
  setup 43DD40, tick 43E310, base contribution 43E540, surface bonus
  43E6E0, combo 43E790, collision 43E9D0, wrong-way 43EDE0.
- Correction to initial attribution: drift flag 78A345 / override 7361A4
  dispatches 479350. Neighbor 479510 belongs to another mode. The drift
  event setup and HUD selector confirm the corrected attribution. Separate
  slip/yaw torque and wheel +180 curve; not simply disabling a stabilizer.
- Local evidence: build/drift-first-r1/{strings.json,drift-hud.txt,
  drift-force.txt,field-refs.txt,score-refs.txt,scoring.txt,helpers.txt}.
- References already preserved in build/career-audit-20261013;
  video_2026-10-13_08-22-59.mp4 contains drift gameplay;
  video_2026-10-13_08-28-47.mp4 contains finish/reward/garage return.
- Implemented locally: adapted track 1301 dependencies; original scalar
  scoring/force functions, native oracle and detailed docs/DRIFT_SCORING.md.
  Scalar check: 133 cases, zero failures. Track-dependent targets built.
  Correction: style phase zero means qualified, so the lower speed threshold
  applies there; all other phases use the stricter threshold.
- Remaining: live mode/setup/scoring, drift HUD, launch/return and campaign
  result. No drift NRO exists yet.
- Next: isolate exact scoring and physics functions with native calls, port
  them, connect the authored track and event flow, test relevant paths only.

## Live integration boundary

- Original drift setup postprocessing 57F337..57F5D7 implemented separately
  from ordinary tuning. Native whole-setup comparison: Civic/240SX/Skyline,
  levels 0..3, 12/12 identical. Evidence: build/drift-first-r1/setup-r1.
- Drift-only steering predicate, wheel grip and yaw callback connected.
  Track 1301 first-frame loader check passes; no actual driving acceptance yet.
- Drift scoring state producer, material category 3 bonus-wheel adapter and
  race session connection implemented but stateful behavior not validated yet.
- Polygon byte 2 flows through 43FF39, 440A97 and 586D8E to wheel+150.
- Collision strength 465830/46588B accumulates magnitudes of solver impulses;
  43FBB0 admits strength >5 with recent record and wall/car/unsupported test.
  43E9D0 applies a half-second wall latch. Live adapter remains unfinished.
- HUD_Drift.fng authenticated at INGAMEB.BUN offset 17140C; object extraction
  in build/drift-first-r1/hud-objects.json. Gauge/flow still unfinished.
- Scalar check remains 133/133; ordinary race style check passes with retail.
- No drift NRO, no Switch upload, no full-mode acceptance or commit yet.
- Next: collision lifecycle and original HUD, then playable campaign entry
  and finish/reward plus targeted PC checks and Switch packaging.

## Integration checkpoint: drift HUD and first complete PC route

- Source remains local and uncommitted at base df08a6f537e10738f5b27fa31e2abc37774282ed.
- Native combo oracle now executes actual timer helpers: 160 cases. The old stubbed-timer fixture is preserved as unaccepted evidence. nfsu_drift_state_check: 166 checks, 0 failures, including six chain/impact/finish lifecycle checks.
- First manual-input PC capture created fresh frames240/480/780 and trajectory in build/drift-first-r1/pc-drive-r2. It ended before finishing, intentionally a partial recording; not a successful race replay.
- First automated full event3 PC drive: build/drift-first-r1/route-drive-r1.log and its recording/trajectory. All four laps finished, no reset. This verifies route traversal/finish, not human drift feel or console performance.
- HUD uses original angle resources and original scoring panel coordinates. First frame inspected; missing uppercaseX identified and fixed. Map atlas slots relocated to permit original1301 map alongside angle HUD.
- Original43EB00 simulated opponents read per-class first/later lap points from type6 TrackPathZone+94/A4, context min/max77A03C/40, event scalar78A380 and performance scale700A18. Loader462070 copies34B00 payload to779C78; corrected context offsets are3C4/3C8, not3B8/3BC. Initial misread was inspected before implementation and was never accepted.
- Competition/HUD/minimap edits after this checkpoint's last successful build are unbuilt. Campaign entry/difficulty/reward/results, wrong-way reset and native collision-record lifetime still require work. No NRO or upload exists for drift.

## Candidate checkpoint: drift-first-r1

- Owner scope: first campaign drift, angle HUD, scoring/captions, detailed
  scoring article, and original drift-specific handling. Other opening events
  remain later work. Solo implementation; no delegated review. No upload.
- Implemented campaign event3 briefing and three difficulties, purchased-car
  start, score competition, finish statistics, reward/defeat and map return.
- Latest PC build: pc-final-build-r1.log; subsequent regression-build-r1.log
  updates only the input-only slide tool and affected ordinary tests.
- Original scalar comparison: nfsu_drift_check tests/recordings/drift-native.txt
  -> 133 cases, 0 failures. Original full setup comparison remains setup-r1,
  twelve whole records equal. Original combo fixture plus integration checks:
  nfsu_drift_state_check -> 171 checks, 0 failures (state-final-r1.log).
- First live slide attempts did not bank: conservative route driver capped
  below admission, shorter slides remained below minimum raw50. Logs preserved.
  Input-only full-throttle/short-handbrake slide on Civic now banks422.225,
  live-final-r2.log. No pose, speed or score injection, no physics retuning.
- route-final-r1.log: four-lap PC drive completes, no reset; frames and real
  trajectory saved in route-final-r1. It is a clean route test, not drift feel.
- career-final-r1.log: difficulty/reward/defeat/duplicate refusal/resume passes.
  Briefing images inspected; corrected multiline ordering and duplicate footer.
- Regression command: ctest --test-dir build -R
  '^(nfsu_player_powertrain_tests|nfsu_vehicle_tuning_tests|nfsu_race_style_retail)$'
  --output-on-failure -> 5/5 including both retail readonly fixtures.
- Real SDK build via build/drift-first-r1/build-switch.sh completed exit0.
  switch-check-r1.log: race NRO, embedded shaders, GPU sample contract ->3/3.
- Candidate: build/drift-first-r1/nfsu-drift-first-r1.nro,3948005bytes,
  SHA25615E9B59E4E52537E9EEDF66AE669265A1C8DBE3103F4128A4F63FB7E967F8D4B.
  Embedded tag drift-first-r1. Not uploaded; no console acceptance.
- Article: docs/DRIFT_SCORING.md. First-render captures: drift-pc.png and
  briefing.png under build/drift-first-r1. Runtime UI text stays original English.
- Remaining fidelity: drift start NIS/crowd selection; all FEng HUD animation
  states; per-lap native result-record order; native contact-record lifetime
  adapter; exact reference-speed/surface-force producer parity; original RNG
  history. Ordinary4773F0 assist is not yet implemented in the port, so there
  is no claim that a fully restored ordinary stabilizer was merely disabled.
- Next: owner console run when explicitly requested, then remaining drift
  presentation/handling differences. Circuit/knockout/sprint are not enabled
  by this candidate. No claim of complete original parity or final acceptance.
