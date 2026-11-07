assignment: Owner interface/car/object/sound audit, lane permission continuation
phase: implementing
branch: codex/draw-distance-original; Git/index/ref/board publication HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: validated and archived r77, source freeze released
worktree: build/day20261008-root-combined
ownership: Codex root, active
saved: local only; no independent review or hardware send

r77 Default/Release/SDK/native/ASCII passed and UNSENT source/NRO are archived.
The full traffic replacement and overall owner request remain open. Retail is
read-only. No game launch, new capture, UI control, agents, reviews, Git/index,
refs/board or console operations.

Native5A7700 calls5A75C0 first, then for lane-to-lane changes on1206/1214
walks every adjacent pair from old lane toward candidate.5A7820 uses fixed
five-word source rows, keyed by from/to/direction and a CLOSED progress
interval. A strict lower bound disagrees with actual executed endpoint cases.
This is not a track divider asset parser. Original getter40 supplies direction,
actor814 supplies progress. Invalid destination category is a valid rejection;
ordinary categories or nonrestricted tracks skip the divider table.

Ignored native research: fidelity-r78/root-lane-native-r3.py/json. First run
failed on a truncated5A75C0 ret4 declaration, corrected to end5A7688. Second
run had an intended far2000 coordinate encoded as an integer word; corrected
r3 uses bits(2000.). Accepted whole-vs-adjacent comparisons are r3 only. The
harness uses explicit synthetic all-enabled raw route/cell/chain/actor inputs,
executes real functions and does not alter any retail file.

Next expose this permission in shared road code and pass actual track/direction/
raw progress through the live drag planner. Keep traffic lane/path owner and
full-world integration open; another helper is not an accepted natural race.

Updated UTC: 2026-10-29T19:23:30.996449+00:00.

## Implemented and target-checked

Production now exposes5A7700/5A7820 lane permission, the live drag planner
passes actual track/direction/raw progress, and traffic adjacent admission
uses it. Actual owner2 header admission bypasses future chain flags; the old
test that treated such a flag as a disabled raw lane has been corrected to
use a real disabled header lane. A separate native-backed case checks the
owner2 bypass and another checks the non-owner2 flag gate. This corrects the
producer model, rather than fitting a final race time.

Public native r1 failed because the unsigned stack serializer was given an
unencoded negative old category. The corrected public r2 masks its signed
ABI word, executes real predicates and writes the accepted fixture. Targeted
configure/build/tests, including read-only retail manifests, pass with receipts
in fidelity-r78/root-target-r1.json. Full mandatory matrices are next.

Source acceptance remains bounded. Full live traffic actions/spawn/scheduler,
natural interactions, listening/visual acceptance and independent review are
still open. No source correction here changes that HOLD or sends an NRO.

Updated UTC: 2026-10-29T19:33:46.795151+00:00.

## Validated local r78 boundary; owner full scope remains open

Updated UTC: 2026-10-29T19:59:22.249383+00:00.
Required Default and Release checks passed with no skipped retail tests;
Switch packaging/shaders and actual traffic intersection native verifier passed. The native lane-divider permission verifier and live drag/traffic lane-admission checks are included in both matrices, with before/after
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

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r78 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r78 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0.

Command: `/usr/bin/cmake --build build-switch-fidelity-r78 --target nfsu_switch_race_nro -j 2`

Result: exit 0.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r78 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/traffic_lane_permission_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

No independent review, hardware acceptance, console send or Git/index/ref/board
mutation occurred. Current NRO is UNSENT. Code freeze is released after the
archive/identity check in root-boundary-r78.json. Future code edits invalidate
these gates. The full fidelity scope remains open in FIDELITY_STATUS.md.
