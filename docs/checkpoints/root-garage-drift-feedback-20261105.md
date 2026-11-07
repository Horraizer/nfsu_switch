CHECKPOINT
role: worker
agent: Codex root solo
updated_utc: 2026-11-05T17:33:05.2121969Z
assignment: Garage rim/engine color, new parts arrows, drag turbo gauge and second drift scoring.
phase: awaiting-evidence
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local source and docs/GARAGE_DRIFT_FEEDBACK.md; existing Git/index/history/remote hold preserved. Native and PC run artifacts in build/garage-drift-root. No Switch build sent.
verified: Eight focused CTest results passed (final-focused-ctest.log). Original-feedback fixture regenerates from authenticated speed.exe and retail assets. The actual garage test confirms same-material color changes in the resident bank, buying the highlighted swatch, event6 package badge after save/reload, and event18 visual badge on the rewarded level. First Civic engine package uses the stock mesh without remappable accent. Extreme Civic displays distinct colors. Stock Civic completed three four-lap event13 runs and banked drift points using input only. Native award code on the same observed zone visits and rating gives rival totals within .003 points at all three difficulties (compare_event13_trace.py).
decisions: Latest native reward context stored by win handler5A0DC0, at saved profile+1388. Port save schema11 retains it and accepts previous schemas without guessing event order. Native arrow alpha loop is retained. No artificial difficulty scalar change.
remaining: Runtime Switch rendering and matched-input player score on the original PC game.
next: Continue with a console build when requested; no new NRO requested in this pass.
blockers: Console and independent review unverified.
ownership: root, active


Default PC gate passed on final source:
- cmake -S . -B build-crest-followup -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground": pass.
- cmake --build build-crest-followup: pass.
- CTEST_PARALLEL_LEVEL=3 ctest --test-dir build-crest-followup --output-on-failure: 353/353, no skipped retail tests; retail manifest unchanged.
- Bash ASCII HEAD path/blob gates: no matches. Byte scans cover changed files and new paths in the local scope.
- First Release wrapper passed a scalar as a PowerShell splat; configure refused its arguments. The explicit Release command superseded that attempt.

Rail follow-up: the authored track1201 type9 annotation applies to the forward
route. A forward event21 observer with temporary occupied animation clocks
verifies action19 entry and cleanup for two moving actors. Normal simulation
still uses unmodified world clocks; physical braking under a natural blocked
crossing has not been replayed against the original.

Release PC gate passed on the same source:
- cmake -S . -B build-release-crest -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release: pass.
- cmake --build build-release-crest: pass.
- CTEST_PARALLEL_LEVEL=6 ctest --test-dir build-release-crest --output-on-failure: 358/358, no skipped retail tests; retail manifest unchanged.
- The initial Release test invocation at parallelism 3 was stopped and superseded before any of the long draw tests completed. The final invocation includes all five retail-full tests.
- The separate engine Pro check passed. Retail wheel inventory checked every aftermarket texture family for a matching source and mask.
