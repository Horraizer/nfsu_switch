assignment: Owner highest priority, event9 drag tournament cannot steer; original traffic-impact outcome
role: worker
agent: Codex root
phase: awaiting-evidence
ownership: Codex root, active
branch: codex/draw-distance-original; Git/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: validated r83, owner console run, source freeze released
saved: local-only; build/fidelity-r84 and this checkpoint

Owner completed event8 first on Hard, but rejects rival routes/acceleration.
The completed log retains the AI setup and exit summary. Full per-rival time
series was absent; no behavioral acceptance is claimed. Magazine opening
TIMING is also wrong according to owner; defer that investigation behind drag.

Event9 is a three-round drag tournament:1210 reverse,1210 forward,1201 reverse.
A confirmed source omission exists: IntroAiPlanner writes batch.human only
in the ordinary branch that returns early. Its drag branch sets drag_steering
but leaves the human path empty. RaceSession consumes that packet for human
road steering, so input cannot steer the intended lane. Existing isolated
lane tests and load-only drag-track checks did not cover this publication.
Reproduce on the real event9 grid before fixing; add a production regression
for all tournament rounds and the ordinary drag4 path. Separately recover the
original traffic-impact failure conditions instead of treating drag as a
normal collision/camera or inventing an impulse threshold.

Original launch/recording, independent review and Git publication HOLD remain.
Owner authorized the r83 upload only; active nxlink PID79800 is retained and
is not replaced, killed or disconnected. Retail stays strictly read-only.

Confirmed reproduction and corrections:
- root-drag-repro-r1 uses the real event9 round0 retail grid and the old
  r83 compiled libraries. It observed human_points=0 and the explicit FAIL.
- Publishing the human packet alone did not pass a moving pad-input test.
  The steering consumer still received a negative local road coordinate.
  Ghidra45E970/45FAC8 reads actor3B8 against2.0. Actual426930,
  426A5C..426A95, accumulates absolute published speed*dt into that field,
  for speeds in0..1000. This is not AiRoadLocation.distance.
- The drag human/rival packets now use a separate world-published travel
  accumulator. Ordinary packets retain their old adapter pending a separate
  source audit; do not silently broaden this drag regression into AI parity.
- Moving event9 round0 pad inputs now produce opposite processed steering
  words in the production pad/planner/driver/physical path. Failed scratch
  tests remain recorded: empty packet, local-distance gate, and HUD test
  forgetting the separate drag-atlas loader. None counted as passes.
- Actual465D00 native cases confirm the current-hit magnitude threshold35,
  inclusive, for physical selector1 in either drag mode. Special kind4
  hashes24CEBC53/427A6453 use15. The latter helper boundary is covered, but
  real special-object admission is not newly claimed.
- Actual46BAE0/465830/465D00 numeric callbacks confirm drag's additional
  vertical impulse and clamp multiplier1.2. The declared effects-tail hook
  is outside that numeric callback; no original world construction claimed.
- Drag contacts latch actor874 and heat1.5. Original435570 consumes reason7
  before reason6(engine failure), assigns the last free place, and ends
  the human race. Original4A7D60 selects English E1279C38(Totaled!) versus
  A4C878F6(Blown Engine); result text B7B75185 is Totaled.

Remaining: complete all tournament-round/ordinary-drag steering regressions,
real pair/traffic latch binding, terminal HUD glyph and post-result checks,
all mandatory Default/Release gates, Switch build/shader/packaging gates,
ASCII/source snapshot and NRO. Hardware acceptance and independent review
are still unverified; publication and new upload remain held.

Follow-up source correction before final validation:
- Full4605E0 input event processing also compares actor3B8 against2.0, not
  absolute speed. The lane adapter's parameter/caller now use travel distance.
  The native release-event witness exercises this gate at both zero and high
  supplied speed. Equality admits the input event, whereas full45E970 still
  zeroes the resulting wheel word at exact2.0. Keep these different boundaries.
- The first mandatory gate runs were stopped at their owned process trees
  after this discovery, before editing frozen source. Process identities and
  superseded partial logs remain under fidelity-r84. They are not acceptance.
- The scratch retail regression passes event4 and all three event9 rounds,
  including opposite wheel words, last-place reason7 precedence, real loaded
  human/traffic hull contact at light versus hard closing speed, and localized
  failure labels with every required glyph. These supplied-state seams are
  not complete original trajectories or hardware acceptance.

Final terminal-flow audit:
- Added Totaled admission to both Switch post-results and career completion
  seams through one shared predicate, checked with the actual failed session
  and post-race report. Without these consumers a new failure status would
  bypass the result pages.
- The r2 SDK build failed on a wrong local variable name in the new failure
  HUD call (run instead of race); corrected before packaging. The attempted
  two-runner stop found only the host runner because SDK had already failed.
  Host tests briefly overlapped the subsequent header edit before its own
  identity-checked stop. All r2 results are superseded, not accepted.
- Final validation must use freeze-r3 and fresh r3 receipt/log names. Do not
  reuse any partial result, object identity or failed SDK NRO as acceptance.

## Validated local r84 source and NRO boundary

Updated UTC: 2026-10-30T07:45:35.372966+00:00.
All final r3 Default/Release commands and Switch commands passed. Retail
fixtures ran without skips. The identity is the uncommitted source freeze
root-code-freeze-r3.json, not unchanged HEAD. Earlier r1/r2 receipts are
superseded. These gates and supplied-state tests are not console acceptance.

Command: `cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-configure.log.

Command: `cmake --build build -j 5`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-build.log.

Command: `ctest --test-dir build --output-on-failure -j 5`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-ctest.log.

Command: `cmake -S . -B build-release -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground -DCMAKE_BUILD_TYPE=Release`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-release-configure.log.

Command: `cmake --build build-release -j 5`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-release-build.log.

Command: `ctest --test-dir build-release --output-on-failure -j 5`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-build-release-ctest.log.

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r84-r3 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r84 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-switch-configure.log.

Command: `/usr/bin/cmake --build build-switch-fidelity-r84-r3 --target nfsu_switch_race_nro -j 2`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-switch-build.log.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r84-r3 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0. Log: build/fidelity-r84/root-r84-r3-switch-tests.log.

NRO is UNSENT. Git/index/ref/board publication and independent review remain
held. No original launch/recording or computer use. Next: owner-authorized
console test of event9 lane changes, hard/light traffic impacts, Totaled HUD
and the failed tournament result flow. Event8 rivals, magazine opening timing
and other FIDELITY_STATUS criteria remain open. A source archive and NRO hash
are saved in the final local boundary receipt. Code freeze releases only after
that receipt is written; later source changes invalidate this boundary.

## Owner-authorized r84 upload

Owner explicitly requested send after the validated r84 handoff. This lifts
the console-transfer HOLD for this artifact only. Source/NRO bytes are
unchanged. root-upload-r84-run1.json supersedes the historical UNSENT state
in the sealed archive/package; it confirms transfer and the console build
tag fidelity-r84. The nxlink server remains active and captures the log.
Its PID and process creation identity are in that receipt; do not replace or
interrupt the capture during the owner's test. The sender is ASCII, scanned,
and dry-run smoked from the real workspace before the authorized network call.
Hardware behavior acceptance and independent review are still unverified.
