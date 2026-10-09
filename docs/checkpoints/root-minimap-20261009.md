# First-track minimap integration, 2026-10-09

Owner authorized sequential first-race work: map, then nitro/turbo and then
opponents. Current step is map only. Portable is the primary acceptance mode.
Owned branch codex/draw-distance-original, tree build/day20261008-root-combined.
Base5e92643c4a0b38e10a3ffdd875c7d13eca6c4a3e. Local only; no delegates,
source push, merge or console upload in this step.

## Implemented

- First-track resource TRACK1003_MAP loaded from authenticated retail texture
  and GLOBALB metadata through the previously recovered minimap kernel.
  Original CAR_MARKERB artwork represents the current single driven player.
  No map is selected for event8; no roster or rival markers are synthesized.
- Recovered perspective/rotating style1 values, native128x128 placeholder
  center, world-to-UV projection, clipping and heading-aware marker geometry.
  Final placement follows the current HUD's uniform640x480 canvas adapter,
  consistent with the supplied reference. Style1 is selected for this port;
  universal live original style-selection/default parity is not claimed.
- Original0x005919D0 component Hermite filter reads the published chase-camera
  horizontal direction and elapsed quantized world ticks. Repeated snapshots
  and pause do not advance it. Port reset restarts history explicitly.
  The existing chase_angle substitute handles bATan's angle conversion;
  its documented native rounding gap remains. No vehicle/camera mutation.
- Original numeric UVQ survives native clipping and port artwork-boundary
  clipping. Positive HUD-only vertex pz stores reciprocal depth, reconstructed
  into homogeneous GPU position by emit_overlay. Ordinary/frontend vertices
  keep unit depth. CPU reference follows the same perspective interpolation.
- Outside artwork is transparent, avoiding atlas-neighbor sampling. This is
  an explicit port atlas policy, not recovered original sampler addressing.
  Existing linear HUD sampling and full-byte modulation adapter are used;
  raw native route/marker bytes remain in the recovered kernel. Original
  world.efx Modulate2x, including alpha, is documented in FRONTEND_66.md.
  Exact original GPU material/address/filter parity remains visually unverified.
- HUD atlas widens from512x512 to1024x512. Existing pixel locations remain;
  gauge/font normalized U and CPU row stride change together. No artwork or
  existing gauge/lap expectation is changed. Atlas CPU payload adds1MiB;
  GPU image and staging grow accordingly. Runtime persistent64MiB gate stays.
- Normal Switch direct race/free driving enables this first-track map. FPS,
  R3, current effects and accepted steering correction remain in the build.
  Standalone native race CLI is unchanged; PC verification uses the shared
  integration capture executable, not a claimed full graphical game run.

## Evidence and verification

Evidence directory: build-artifacts/draw-distance-20261008-root.
minimap-r1-original contains fresh authenticated retail/native disassembly,
resource/style/geometry/filter measurements. minimap266_trace.py completed
PASS with full before/after retail manifests unchanged. No retail input wrote.
Additional0x0040BEF0 disassembly preserved as draw-quad-instructions.txt.

Native affected HUD/kernel/lap targets built. Targeted CTest3/4PASS:
nfsu_race_minimap_hud_tests, nfsu_minimap_tests and nfsu_race_lap_hud_tests PASS.
nfsu_race_hud_tests retains only the two previously recorded manual up/down
fixture failures. Pixel-space gauge UV assertions retain their original
expected values while accounting for the changed atlas width.
No complete green native matrix is claimed.

New focused cases cover original67-tick filter sample, same-time pause,
reset history, bounded integrated route/player output, finite projective
vertices, UV isolation, repeated snapshots, unchanged simulation recording,
atomic capacity refusal and no first-track map for unselected resources.

Actual original resources and first-track collision/route loaded read-only.
Three integrated HUD captures include lap/timer/map/gauge/FPS, startup and
moving/turning poses. Hot-plug neutral frame precedes throttle. Viewed
minimap-r1-moving-pc/hud-360.png; moving car and turning player/map are visible.
Captures use a flat diagnostic background, not an original or port scene shot.

Switch SDK Release NRO built with tag minimap-r1; NRO/embedded shader/frontend
profile contract CTest3/3PASS. Minimal relevant gates per owner instruction.
ASCII source/path/entry-mode checks required before delivery. Intermediate
test compilation typo is retained in the first build log, then corrected.

## Previous portable run, historical measurement

At5e92643 fps-r3-r1, nxlink-fps-r3-r1-run1.log records7541 driving presentation
intervals: mean16.677885ms, p99 histogram bin18.25ms, max26.810833ms,4over25ms,
0over33.333333ms. Actual GPU460800000Hz/CPU1020000000Hz/EMC1331200000Hz.
Owner observed theater and other stutters. This interval proxy is not GPU
execution time or measured physical scanout. Optimization remains deferred.

Remaining: owner portable map placement/turn/R3 appearance and load/lifecycle
acceptance at current NRO identity; second-track map, original default/style
and material parity, race start-line/rival markers are not claimed complete.
Next authorized step after map acceptance: nitro/turbo mechanics and HUD.

## Owner correction: minimap-r2

At267c4a2, owner rejected transparency and the changing window shape after
actual minimap-r1 upload. Do not count the original map as visually accepted.
The log nxlink-minimap-r1-run1.log records a completed driving run and exit;
current Netloader availability is not inferred from that run.

The integration's extra UV0..1 polygon crop was wrong: native0x0058FB10 clips
to the stationary HUD rectangle. Removed that crop. Original world.efx has
WRAP/LINEAR and Modulate2x for RGBA; authenticated SHA-256 is
d1cdbd788d564c4cd408a060d0eda4e99245f0eb54b18a4fcee848d2966e6c98.
The map sampler now wraps within its atlas rectangle, with one texel of
periodic padding copied from original artwork to isolate linear edge samples.
No texture alpha mask or arbitrary opacity multiplier was introduced.

Original0x0040BEF0 emits unit position W and separate UVQ. The previous GPU
adapter moved Q into position W and thereby perspective-weighted color/alpha
too. Renderer-only HUD flag1024 keeps unit W and screen-affine RGBA while
dividing route UVQ separately. CPU reference uses the same separation.
Ordinary scene uniforms keep the flag off; frontend uses its existing shader.
Native read-only disassembly is saved in minimap-r2-render-context.txt.

Targeted native minimap and shader manifest CTests PASS. The existing HUD
CTest still fails only its two retained manual up/down fixture assertions.
New negatives catch route removal outside artwork and perspective-weighted
alpha. The first test build failed on an array-assignment typo, corrected in
minimap-r2-native-build-r2.log; no failed run is counted as a pass.
Actual Switch SDK NRO/embedded shader/frontend contract CTest3/3PASS in
minimap-r2-sdk.log. No complete native matrix is claimed.

Fresh full PC renderer capture PASS: minimap-r2-pc-scene.png and comparison
minimap-r2-pc-reference.png. This renders the retail street, selected Skyline,
road reflections, shadow and HUD through current native libraries; it is not
a gray-background HUD composite or Switch readback. The helper uses its
previous diagnostic far plane250 and a stationary free-driving pose. Deck
pose/car dress/viewport differ. Visible route and marker are close to the
reference; final alpha equality is not claimed. Blue start-line marker is
still absent, event8 map remains unimplemented, and original live style
selection/technique state is not fully established. These remain explicit.

Source is local only. Preserve minimap-r1 NRO and receipt. Seal minimap-r2
against its actual commit after ASCII scan; do not claim a new hardware run
or publish/merge automatically. Next: owner checks corrected map on Switch.
