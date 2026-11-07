# Free driving / Underground physics and NOS audit

CHECKPOINT
role: worker
agent: Codex root, solo owner-directed work
updated_utc: 2026-10-14
assignment: owner reports physics and NOS sound differ; check acceleration and steering
phase: awaiting-evidence
branch: codex/draw-distance-original
base: c9866e91d1d7f14bff44366c9bc2d8655bb43276
worktree: build/day20261008-root-combined
saved: local commit and build/mode-parity-r7; not published
ownership: root; no delegated sessions

## Findings and correction

- Both menus use RaceRun/RaceSession and recovered predicate1 human steering.
  UG0 adds opponents/traffic and their contacts; no alternate human solver.
- Default Free Driving is Skyline, base performance except induction1/NOS3.
  UG0 original DDay human is Integra with all eleven performance levels1.
  See resolve_free_drive_skyline and load_event0_setup. Different acceleration
  and handling across these two setups are not proof of a mode discrepancy.
- Confirmed defect: RaceRun used stock_240sx_hull for every non-UG0 car.
  It now decodes the selected car's original named *_CV solid at load time.
  UG0 retains its grid hull; 240SX retains its authenticated baseline hull.
  Missing selected geometry refuses start, without a guessed fallback.
- Existing intro/traffic hull parser is shared in car_collision_hull.h;
  output is transactional. No per-frame file read or allocation was added.
  Selected geometry is temporary and freed before live session construction.
- NOS1 uses Nitrous_00_MB.abk; NOS3 uses Nitrous_01_MB.abk. Both modes use
  the same effects graph implementation. No sound production code changed.

## Validation at the local correction based on the SHA above

Evidence directory: build/mode-parity-r7.
Owner explicitly requested minimal necessary tests and solo implementation;
no broad Default/Release matrix or independent reviewer was run.

- cmake --build build --target nfsu_freeride_skyline_tests
  nfsu_intro_grid_viewer -j 4: PASS, build-r2.log.
  Initial test compile failures retained in build.log, corrected before run.
- build/nfsu_freeride_skyline_tests.exe RETAIL_ROOT: PASS, parity.log.
  Actual retail selected Skyline, both tracks and existing visual checks.
  Same car/tune/input free versus race compared every serialized physics
  slice over 1200 frames: throttle, NOS, steering, braking and handbrake.
  Actual hull session also matches; substituting old 240SX hull diverges.
  NOS activation and charge are identical between the paired modes.
- build/nfsu_intro_grid_viewer.exe RETAIL_ROOT
  build/mode-parity-r7/intro 120 profile: PASS, intro.log.
  Four-car UG0 load/run/render survives shared parser extraction.
- cmake --build build --target nfsu_race_audio_tests -j 4: PASS,
  audio-build-final.log.
- build/nfsu_race_audio_tests.exe RETAIL_ROOT: PASS, audio-final.log.
  Same NOS level and activation produces byte-identical PCM across Skyline
  and Integra setups; levels1/3 differ. Existing effects lifecycle, music and
  collision graph tests pass. Initial audio.log/audio-r2.log/audio-r3.log
  failures retained: old countdown test assumed exactly zero velocity before
  the existing grid-settling change. Actual held velocity stays unchanged;
  updated assertion requires unchanged epoch/velocity and no contact/NOS.
- SDK configure/build with tag mode-parity-r7: PASS,
  sdk-configure.log and sdk-build.log.
- ctest --test-dir build-switch -R
  '^nfsu_switch_race_(nro|embedded_shaders)$' --output-on-failure:
  2/2 PASS, sdk-package.log. FPS overlay defaults OFF; L3+R3 retained.
- git diff --check and ASCII contents/paths scan: checked before commit.

## Limits / next action

This is mode parity for identical setups, not complete retail physics parity
or a guarantee of equal lap times between different cars. Paired race has no
opponent/traffic impulses. Existing whole-physics fidelity backlog remains.
Original sound bank selection is retained; upstream engine load/RPM audio
smoothing remains a separate documented fidelity gap.

The prior r6 hardware log contains Free Driving only; do not compare it to
UG0 four-car measurements as optimization proof. New r7 is not uploaded or
hardware accepted. Exact NRO/source/check hashes are in package.json.
Next: owner-requested deployment with fresh Netloader readiness, then compare
free Skyline loading/handling and unchanged UG0. No push, merge or board write.