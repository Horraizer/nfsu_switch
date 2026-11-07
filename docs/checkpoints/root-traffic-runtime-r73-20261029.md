assignment: Owner interface/car/object/sound audit; traffic runtime continuation
phase: investigating
branch: codex/draw-distance-original; Git/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: r72 source frozen; required Release matrix pending
worktree: build/day20261008-root-combined
ownership: Codex root, active
saved: local read-only research; r71 sealed source/NRO retained

No agents/chats/review/board/Git mutation, original launch/recording,
computer use or console send. Retail remains read-only. Source freeze r72
stays in force until Default/Release/SDK/native/ASCII and archive complete.
Default and SDK matrices passed; Release is running. Do not reuse r72 gates
for later edits or call own verification independent review.

Confirmed live integration: IntroAiPlanner already snapshots all traffic
actors for rival queries (intro_ai.cpp). A preliminary hypothesis that its
world contains racers only was wrong; no correction is required there.
Traffic still plans late inside RaceTraffic::advance, with its private RNG,
permanent junction decisions, even-slot scheduling and finite corridor
speed adapter. Existing numeric helpers do not close that live owner.

Actual58350D..5835DB executes the traffic action ownership order. It runs
all44F700 predicates, uses44A0E0 to resolve completion, but selects and
constructs a candidate BEFORE cleanup/release even for a completed current
action. A completed current passes kind20/priority-1; a retained current
passes its actual kind/priority. Candidate priority must strictly exceed
that threshold. Rejected candidates are destroyed without updating current.
Accepted kinds1/3/9 promote physical selector2 to3 via426110 BEFORE583850.
A missing current uses strategy virtual18 to create its initial default.
There is no racer-style requested-kind3F4/fallback4 update in this owner.

Actual strategy44E020 (address0044E020) queries obstacle18 when current
kind differs and priority<=600, then avoidance3 when kind differs and
priority<=300, then its default8 constructor. Avoidance invokes44C2E0
reaction before allocation, including a candidate later rejected by owner.
Query side effects and construction effects must remain lazy and ordered.
Do not equate current physical selector2/3 with role3 or owner selector24.

root-owner-native-r5.py/json executes the actual owner slice, predicate/
completion dispatch, role3 full-frame gate and strategy selection. Explicit
boundary witnesses supply virtual predicates, geometry query outputs,
action allocation/construction/destruction, reaction, physics construction
and final path/speed update. This certifies caller order on declared inputs,
NOT those hooked implementations or a natural whole race. Prior r1 failed
on an unsupported fixture register accessor; r3 failed on unsigned argument
packing; r4 used a too-short declared ret-instruction range. These are
fixture failures and do not count as source defects or successful runs.

Next: seal r72, then implement distinct allocation-free traffic owner,
strategy and full-frame policy APIs and differential checks. Continue real
strategy4 actions8/3/18, traffic road-context/lane notifications, shared WORLD
RNG/caller order, physics promotion and spawn/retire policy as one live
traffic restoration; pure helper equality does not accept live traffic.
Full natural contacts, remaining audio/device/particles/UI/camera and
portable acceptance remain open in FIDELITY_STATUS.md.

Updated UTC: 2026-10-29T14:45:05.706054+00:00.

## Connected owner APIs; live traffic remains pending

r72 is sealed: Default/Release/SDK/native/ASCII passed, source and NRO
archived UNSENT. Its code freeze is released. New ai_traffic_action APIs
implement the distinct role3 frame gate, lazy strategy query selection and
candidate-before-cleanup owner. No live caller is replaced yet. The native
verifier now lives at tools/traffic_owner_native.py and its complete fixture
is tests/traffic_owner_native.inc. The production differential check matches
native predicate/status/action/call order and physical promotion, with
terminal failure assertions. Strategy comparison checks query order and
result separately from declared query/factory/reaction IO; it is not a
geometry or reaction implementation witness.

Target build initially failed because a test witness held an unassignable
reference member. The witness now owns its fixture row; successful rerun is
root-target-validation-r2.json. Registered native/compiled checks and retail
before/after manifests passed. Full matrices are not yet run for r73.
Remaining real traffic context/action/lane/shared-world/spawn integration
must not be described as restored from these helper results.

Source frozen for mandatory matrices at 2026-10-29T15:01:00.713709+00:00.
Only the expected owner API/native verifier/test/build files differ from
sealed r72; no earlier source was removed. Root-code-freeze-r1.json pins
this local group. Live traffic caller remains unchanged.

## Validated local r73 boundary; owner full scope remains open

Updated UTC: 2026-10-29T15:35:12.077358+00:00.
Required Default and Release checks passed with no skipped retail tests;
Switch packaging/shaders and actual traffic owner native verifier passed. The new native traffic owner verifier and extended audio assertions are included in both matrices, with before/after
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

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r73 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r73 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0.

Command: `/usr/bin/cmake --build build-switch-fidelity-r73 --target nfsu_switch_race_nro -j 2`

Result: exit 0.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r73 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/traffic_owner_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

No independent review, hardware acceptance, console send or Git/index/ref/board
mutation occurred. Current NRO is UNSENT. Code freeze is released after the
archive/identity check in root-boundary-r73.json. Future code edits invalidate
these gates. The full fidelity scope remains open in FIDELITY_STATUS.md.
