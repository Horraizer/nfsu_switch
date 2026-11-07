assignment: Owner interface/car/object/sound audit; traffic road context
phase: implementing
branch: codex/draw-distance-original; Git/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: r73 sealed source archive; own mandatory checks passed
worktree: build/day20261008-root-combined
ownership: Codex root, active
saved: local source, no independent review or console send

r73 Default/Release/SDK/native/ASCII passed and its source/NRO are archived
UNSENT; its source freeze is released. No Git/index/ref/board/agent/UI/live
original game operation is allowed. Retail remains read-only.

Actual traffic582F90/583700 and44CF30 now execute over initialized original
roads and declared exogenous stock240SX poses, including junction passage,
nearest-lane change and no-road/re-entry. The fixture supplies physical
selector3 and excludes natural spawn, contact and integrated physics.
root-context-native-r6.py/json preserves the native outputs and raw road
inputs. Earlier fixture failures were missing declared instruction ranges,
not source or game failures; verified actual ranges were added, never result
hooks. Original helper bodies are confirmed by Ghidra. r1 baseline missed
link dependencies. r2/r3 serialized traffic_type into source_point; r4 fixes
that fixture field. Those bad cache comparisons are retracted. The current
compiled baseline is root-context-baseline-r4.log, not the earlier attempts.

Actual5A5C30/5A6000 use ride+424 for ordinary compact-slot bounds/curvature
and enabled[0], separately from controller selected lane11..16. Existing
compact helpers accept only one category, so traffic needs explicit ride
category input. Actual583B20, called on44C250 reselection, picks nearest
enabled lane and clamps missing lane to0. Passed-event549130 is a no-op,
not racer462A00 marker clearing.44CF30 also admits adjacent lanes in order
+1,-1 through+5,-5 and resets the maneuver only on that admission; nearest
fallback has a distinct no-reset path. No-road stores category-1 and must
allow a subsequent spatial re-entry without passing-1 to low-level helpers.

Next: connect distinct ride bounds and traffic notification/category policy
in the common context API, reproduce the full native context snapshots,
then publish an authenticated repeatable verifier and run required gates.
Live traffic actions/shared-WORLD scheduling/spawn/contact acceptance remain
open. See FIDELITY_STATUS.md for the rest of the owner scope.

Updated UTC: 2026-10-29T15:37:19.591114+00:00.

## Connected context and repeatable differential

Separate ride_category now controls compact slot0 growth/curvature/bounds
and enabled[0], independently of selected traffic lane. Traffic reselection,
no-op passed markers, adjacent-lane admission/reset, no-road category-1 and
re-entry are connected in ai_road_update_context. Default racer behavior is
retained by the optional input. The initial comparison fixed only bounds;
three junction contexts still differed because growth offsets also use ride
category. Actual5A58D0 confirms that source; growth now uses it as well.
root-context-baseline-r8/r9.log match all captured metadata, RNG/cache,
notifications, geometry, offsets and bounds. R9 loads geometry from external
retail data instead of embedding it. These are isolated inputs, not live
traffic acceptance. The prior lane-reset observation at44D2C0 was wrong:
that is nearest fallback. Verified disassembly places adjacent reset44D337;
public witness uses the corrected address and an explicit inherited invalid
lane/maneuver case, where original resets to descriptor1/zeros.

Public traffic_context_native.py uses an explicit traffic option in
ActorController instead of dynamically rewriting its class source. Actual
582F90 constructs the traffic owner, the actor has no race row, and the
ordinary step helper refuses this domain. Original default constructor/pose
inputs retain their semantics; the existing ordinary native context verifier
was regenerated to fresh files and exactly matches its tracked fixture.
Native public fixtures contain compact context snapshots and connection ID
aliases only; geometry remains an external authenticated retail dependency.
No whole-track asset is added. New compiled native differential and original
verifier are registered with retail read-only fixtures.

Updated UTC: 2026-10-29T15:55:28.744327+00:00.

Target configure/build and all selected registered checks passed, including
retail before/after manifests. Exact commands/results are in
fidelity-r74/root-target-validation-r1.json and root-target-r1-*.log.
Source is frozen at root-code-freeze-r1.json for mandatory Default/Release/SDK
and ASCII matrices. Do not edit code or reuse r73 results for these changes.
Updated UTC: 2026-10-29T15:56:44.963802+00:00.

## Validated local r74 boundary; owner full scope remains open

Updated UTC: 2026-10-29T16:20:52.341775+00:00.
Required Default and Release checks passed with no skipped retail tests;
Switch packaging/shaders and actual traffic context native verifier passed. The new native traffic context verifier and extended audio assertions are included in both matrices, with before/after
retail manifests. Source identity is root-code-freeze-r1.json, not HEAD.

Command: `cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `cmake --build build -j 5`

Result: exit 0.

Command: `ctest --test-dir build --output-on-failure -j 5`

Result: exit 0.

Command: `cmake -S . -B build-release -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground -DCMAKE_BUILD_TYPE=Release`

Result: exit 0.

Command: `cmake --build build-release -j 5`

Result: exit 0.

Command: `ctest --test-dir build-release --output-on-failure -j 5`

Result: exit 0.

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r74 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r74 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0.

Command: `/usr/bin/cmake --build build-switch-fidelity-r74 --target nfsu_switch_race_nro -j 2`

Result: exit 0.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r74 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/traffic_context_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

No independent review, hardware acceptance, console send or Git/index/ref/board
mutation occurred. Current NRO is UNSENT. Code freeze is released after the
archive/identity check in root-boundary-r74.json. Future code edits invalidate
these gates. The full fidelity scope remains open in FIDELITY_STATUS.md.
