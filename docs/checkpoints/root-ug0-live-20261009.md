# Introductory race zero: first playable prototype

role: worker
agent: Codex root, direct owner collaboration
assignment: owner selected UG introductory race 0, then authorized moving rivals
phase: awaiting-hardware
base: a447eecff1b89513f6a7f74f2f69698a1af5d144
branch: codex/draw-distance-original
worktree: build/day20261008-root-combined
saved: local only; no source push or merge

## Implemented

- UNDERGROUND opens event0, track1003, two laps, with its authored Integra
  player and Brian/Eclipse, Lola/Celica, Brett/Supra configurations/grid.
- Each actor uses its own recovered vehicle table and selected retail CV
  points for track collision; the player retains authored level-one upgrades.
- Four physical sessions share one course and one four-racer timing status.
- Stable rival GPU payloads are appended before renderer upload. Subsequent
  frames update placement, wheels, lamps and projected original shadows.
- The map shows four actors. A small port position label and result rows
  expose standings. The original Position FEng package is not implemented.
- Pair admission/separation uses port oriented boxes over selected CV bounds.
  Closing response uses existing recovered kind0 impulse math and publishes
  both authoritative momenta, without resetting lap/progress state.

## Explicit limits

- Rivals use STOCK physics and the existing slow port route driver. Neither
  retail adaptive tuning/RNG nor original racing AI is reproduced.
- Pair contacts run after each 60Hz frame, not between the four physics slices.
  SAT witnesses/separation are approximations. The original hard-hit callback,
  collision-state transitions, pair-list scheduling and rollover are open.
- Rival headlight sprites/projectors and rival engine audio are not added.
- No traffic, career save/progression, flyby or original intro movie integration.
- The existing recording does not seal the complete rival world state.
- Hardware appearance, input, frame pacing and menu reentry remain unverified.
  This is a first test build, not an accepted original-compatible UG release.

## Verification and evidence

Root artifact directory: build-artifacts/draw-distance-20261008-root.

- Native CMake build of intro_grid_viewer and race_run_tests passed.
  Targeted nfsu_race_run_tests passed (ug0-live-native-r2 logs).
- Fresh 20s PC scene: all four cars moved; ug0-live-r2 captures/log.
- Full two-lap PC run: FINISHED with four timing records; fresh final captures,
  pair admission/hit/self-contact checks and stable GPU package validation
  passed in ug0-live-full-r3.log. Supra was still completing its second lap
  when the human finish ended the race.
- Earlier full-r1 stopped before finishing its bounded replay window; full-r2
  reached the finish but the capture harness wrongly required final distance
  from the grid. That check now runs at ten seconds. Both failed logs remain.
- Actual SDK NRO build passed; targeted package, embedded shader and frontend
  power/transition checks passed 3/3 (ug0-live-sdk-r2.log). Initial source
  check failed on its old literal free-drive handler; it now covers the shared
  UG/free-drive handler and both launches, retaining its causal negatives.
- FPS overlay off, FXAA on, MSAA off; accepted graphics/nitrous base retained.
- No full native/Release matrices or independent review, per owner workflow.

Next: owner hardware test of loading UG0, start, car contacts, map/position,
two-lap result and return to menu; then discuss original AI/adaptive tuning.
