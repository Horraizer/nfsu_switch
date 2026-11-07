# World motion controller continuation

CHECKPOINT
role: worker
agent: Codex root solo
updated_utc: 2026-11-05T14:13:04.822974+00:00
assignment: Owner direct request to restore train/Trolley controllers through Ghidra and verify on PC first.
phase: awaiting-evidence
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
base: same integration head, with preserved owner-authored dirty changes
worktree: build/day20261008-root-combined
saved: Local source and tools, not committed or pushed under the existing owner Git/index/history/remote hold.
implementation: See ../WORLD_MOTION_CONTROLLERS.md. Shared EAGL channels, authored clocks, phase spacing, trigger release/prediction, node velocity, kind4 contacts and actual dynamic model commands are connected. Models are retained only when this track requests them.
evidence: Task scratch build/world-controllers-root preserves Ghidra exports, authenticated original native JSON, corrected fixture generations, baseline/final source manifests and all raw gate logs. Native tool rejects retail output and existing evidence; its positive smoke ran from the real workspace path.
verified: Targeted PC runtime/render/track checks passed. Full Default ran350 distinct cases, with seven stale static-payload/cache expectations failing. Corrected affected cases then passed9/9, fixtures included. A final dynamic-model triangle accounting correction was included in the clean full Release355/355, and final Default world-motion4/4 passed afterward. No retail test was skipped. This is not a claim that the initial Default run was green.
remaining: Independent review and Switch runtime are unverified. The specialized AI crossing-clear caller454A20, train audio/effects and arbitrary parented/skeleton factories are not certified by the movement/controller witnesses.
next: Review source-final.json and this evidence; hardware validation is a separate step. No whole-game parity claim.
ownership: root, active; no delegation

This is a local-only checkpoint while publication is held. No new NRO has
been built or sent for this continuation. R111/R112 artifacts are preserved.

## Final validation record

Recorded at 2026-11-05T14:34:47.201822+00:00, integration base above plus the local task delta whose
hashes are recorded in build/world-controllers-root/source-final.json.
All commands ran from the actual non-ASCII workspace path with
C:/Strawberry/c/bin on PATH. Python3_EXECUTABLE was the real Python312
interpreter selected with `$taskPython=Join-Path $env:LOCALAPPDATA 'Programs/Python/Python312/python.exe'`.

- `cmake -S . -B build-crest-followup -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" "-DPython3_EXECUTABLE=$taskPython"`: exit0.
- `cmake --build build-crest-followup --parallel 6`: exit0.
- `ctest --test-dir build-crest-followup --output-on-failure --parallel 4`: `98% tests passed, 7 tests failed out of 350`. Raw default-final-ctest.log retained.
- After test inventory corrections, `cmake --build build-crest-followup --target nfsu_race_run_tests nfsu_circuit_tracks_tests --parallel 6`: exit0.
- `ctest --test-dir build-crest-followup --output-on-failure --parallel 4 -R '^nfsu_(circuit_tracks_(retail|drive_retail)|race_run_retail(_linear|_linear_shading|_glows)?|tuned_race_draws_retail)$'`: `100% tests passed, 0 tests failed out of 9`.
- `cmake -S . -B build-release-crest -G Ninja -DCMAKE_BUILD_TYPE=Release "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" "-DPython3_EXECUTABLE=$taskPython"`: exit0.
- `cmake --build build-release-crest --parallel 6`: exit0.
- `ctest --test-dir build-release-crest --output-on-failure --parallel 4`: `100% tests passed, 0 tests failed out of 355`, including retail-full. No skips.
- After the last triangle-accounting correction, `cmake --build build-crest-followup --target nfsu_world_motion_tests --parallel 6`: exit0.
- `ctest --test-dir build-crest-followup --output-on-failure -R '^nfsu_world_motion'`: `100% tests passed, 0 tests failed out of 4`.
- HEAD Bash path/content/mode scans returned1/1/1, the no-match success convention. The complete working tracked/nonignored path/byte scan and local automation scan reported0 violations.
- Native probe positive smoke produced fresh native-guarded.json. Existing-output and retail-output refusal smokes returned nonzero and created no retail output. Its numeric results equal native-final.json.

The first incomplete default-ctest.log was deliberately stopped after the
model-retention/payload discrepancy. Its partial results are not substituted
for a passing gate. Native failed fixture attempts and correction history
remain in the task scratch checkpoint. No code was approved by its author;
independent review remains outstanding, with publication held by the owner.
