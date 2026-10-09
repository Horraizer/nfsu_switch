# Gradual high-speed steering correction, 2026-10-09

Owner reports steering stopped BEFORE impact, repeatedly at the theater on
event1. Video 2026100918352000-16851BE00BC6068871FE49D98876D6C5.mp4 shows the
approach near lap time43-45s. Timer is accepted; glow displacement is no
longer confirmed. Optimization and minimap remain paused for this correction.

Base: 5205e05b779ff827948a0f1a58fa08771dd1b9bd. Own tree:
build/day20261008-root-combined. Local artifacts:
build-artifacts/draw-distance-20261008-root.

## Confirmed software defect

RaceSession called recovered steering twice per60Hz pad tick with dt1/120.
Original human4615E0 calls460930 once, passing the supplied world dt at
461691/461698; its returned angle is published at4616A9 before physics.
The source phase contract is docs/PHYSICS_DISPATCH_256.md. This establishes
call ownership and dt propagation, not measured original live frame cadence.

Native460A2C..460A3F snaps a completed slew angle below1 degree to zero;
constant6B7390 is1.0. Duplicating a held sample with half dt causes extra
zeroing and dilutes the input-gradient history. The primitive is unchanged.
Only recovered steering now runs once per port world tick with that tick's
dt. Its history clock and shift elapsed remain in seconds. Processed controls
retain their existing two updates; physics retains four slices. No grip,
steering table, snap threshold, collision geometry or rendering change.

## Evidence

Authenticated speed.exe SHA256:
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
root-theater-steering-native.py executes the existing isolated retail helper
for slow left/right ramps at58m/s. All1080 native/port rows agree within the
existing comparison tolerances, including exact angle words. These are
supplied contexts, not the owner's input recording. Retail manifests match.
Single60 first angle at frame38; duplicate120 first angle at frame58.

Actual PC RaceSession with four physics slices and original pad binding:
start speed58m/s, coast30ticks, increase steering by.025/tick to.8.
Stock240SX and decoded SKYLINE both first turn at frame56 on the parent,
frame37 after correction, in both directions. At frame44 SKYLINE parent
road angle is0; corrected angles are+/-0.04209rad and yaw+/-0.257rad/s.
Both front wheels contact the ground, with no body collision. CSV before
and after captures are preserved separately. This reproduces lost turn-in;
it does not establish the exact cause of the owner's theater episode.

## Targeted validation and remaining limits

- cmake --build build --target nfsu_steering_tests nfsu_race_session_tests -j4
  completed in the existing Release configuration.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_steering_tests|nfsu_race_session_tests)$': session passes;
  steering suite retains116 assertions failing in four old categories.
  No complete green native gate is claimed.
- Parent RaceSession compiled with identical current tests/dependencies:
  same116 failures plus four new slow-turn assertions. Corrected code passes
  those four; no additional failing assertion. Comparison saved as
  steer-world-r1-parent-comparison.json. No old expectations were changed.
- Real Switch SDK Release NRO build completed; targeted NRO, embedded shader
  and frontend profile contracts pass3/3. No unrelated full matrices per
  owner instruction. Tag steer-world-r1, same FXAA/current graphics settings.
- Required next check: owner's same theater approach on Switch. Raw stick,
  road angle, yaw and wheel contact can be captured with existing optional
  --handling-telemetry (bounded2400rows); that run is not a performance A/B.
  Current old run has connection status only and cannot establish raw input.
- Actual theater collision attribution, corrected hardware handling and the
  subjective judder report remain unverified. No main merge/push/upload.
