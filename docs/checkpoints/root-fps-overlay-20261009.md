# FPS overlay and portable priority, 2026-10-09

Owner requests visible FPS and a graph to distinguish subjective stutter.
Owner tested docking and reports good play; portable remains the primary
target. One pink sky texture behind buildings disappeared after restart and
did not recur. This is an unresolved intermittent visual defect, not fixed.
No source push, merge, delegation or upload in this step.
Base: 8562a3d761a20e5599d2b3cacc01d2b53c80c5ca, includes held R3 and steering.
Owned tree: build/day20261008-root-combined.

## Diagnostic implementation

Optional FPS overlay in the existing final GPU HUD pass. Default is OFF in
source CMake; fps-r3-r1 is configured ON. CLI --fps-overlay/--no-fps-overlay
overrides it. Requires HUD/direct presentation; AB measurement refuses the
overlay rather than silently contaminating its baseline.
No GPU readback, fence insertion, new per-frame log or allocation is added.
The HUD capacity is increased from128 to256 fixed quads for90 graph bars,
retail font text and the ordinary lap/gauge elements. ABI consumers rebuilt.

Samples are wall-clock intervals between renderer.present returns, including
simulation, rendering submission and normal acquire/fence waits. They are a
presentation cadence proxy, not GPU execution durations or measured physical
display scanout. The overlay itself has small unmeasured draw overhead.
FPS is1000 divided by mean interval across the last90 valid samples. Graph
bars show individual intervals, oldest to newest, with16.7/33.3ms reference
lines; red above25ms. Graph clips at66.7ms, numeric latest interval does not.
History resets on dock/portable changes. Overlay appears during race/free
driving, including paused/countdown frames, not on the frontend menu.

Automatic scene height is540 handheld and720 docked unless CLI overrides it;
the presentation canvas stays1280x720. This follows deko_scene_target.h;
there is no1080p dock rendering claim.

## Verification

- Actual Switch SDK Release NRO build completed. Targeted NRO/shader/frontend
  contract CTest3/3PASS. First compile failed on a local flag scope typo;
  corrected before successful build, logs retained separately.
- Native HUD target rebuilt. Targeted CTest retains exactly the two known
  manual up/down fixture failures, documented before this change; no new
  failure message. New interval validity/60Hz/stutter history checks pass.
  Full native green is not claimed. A temporary duplicate test variable was
  corrected before final build; intermediate failed logs remain separate.
- Retail atlas loaded read-only in --capture; three1280x720 synthetic HUD
  frames written. Viewed stock-driving.png: FPS text, bars, stutter and gauge
  are visible. This is synthetic diagnostic geometry, not a gameplay capture.
- ASCII artifacts/source and entry-mode scans required before delivery.
- Evidence/build logs: build-artifacts/draw-distance-20261008-root/fps-r3-*.

Remaining: actual portable FPS/R3 check, theater steering owner confirmation,
intermittent pink texture reproduction. Original whole-scene speed blur and
airborne camera behavior observed in second-track reference remain future
sequential work. No unrequested effect or physics adjustment made here.
