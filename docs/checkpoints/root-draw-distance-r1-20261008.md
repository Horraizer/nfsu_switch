# Draw distance development candidate

Assignment: owner requests original draw distance, directly with root.
Base: 2183f0e11e425403e862c5a8d8f5069df0c6bbee (visuals-r1).
Branch: codex/draw-distance-original. Local only; owner hardware acceptance pending.

## Change

Switch opts into recovered camera-constructor far plane 10000 (47CF8F),
original camera-zone section membership and matching road-reflection membership.
The legacy 250/125/62.5/31.25 diagnostic fallback stays in diagnostic callers.
Prepared static draws use their persistent uniforms; transient uniform budget
counts only the dynamic tail, matching the existing deko renderer allocation.
The bounded command capacity is 4096; overflow refuses without silently reducing
visibility. No physics, material, texture or effect policy changes intended.

## Evidence and validation

Runtime-relative artifact directory: build-artifacts/draw-distance-20261008-root.
original-distance.json authenticates the original executable and constructor run.
native-tests-r2.log: focused visibility, rendering and execution CTest 3/3 PASS.
switch-build-r1.log: real SDK build and shader/package CTest 49/49 PASS.
root-distance-smoke-r2-{0,1}.log: twelve visual frames and two dense frames PASS,
including original-distance retention, current command planner and update checks.
Dense held-camera witnesses exceed the former 2047-draw limit on both tracks.
These are render probes, not gameplay trajectory acceptance or hardware timings.
manifest.json binds delivered nfsu-distance-r1.nro to source file hashes.

## Limits and next action

Full native Default/Release matrices were not rerun for this development build;
inherited baseline handling and visual-reference failures remain uncertified.
The exact original live LOD/screen-size gate is not integrated or claimed here.
Camera constructor distance does not establish every later camera override.
Hardware memory use, frame timing, scene appearance and a full lap remain open.
Deliver the NRO for owner testing; upload only with current Netloader readiness.
Keep visuals-r1 available for rollback. No push, merge or branch cleanup performed.
