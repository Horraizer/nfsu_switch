# Headlights CPU prototype checkpoint

Owner requested sequential headlights work without agents or reviewers.
Local-only dirty worktree: build/day20261008-root-combined, base b009de72.

Recovered original HEADLIGHTS texture (hash 28EEFA9C), view8 race pose,
projection angles, color and 64-texel range mask from authenticated retail
speed.exe. CPU prototype now illuminates actual world geometry in the saved
Skyline scene. The first image was unchanged because raster interpolation
ignored headlight lanes; this was corrected and covered by a fragment test.

Validation: nfsu_track_render_tests 1/1 passed, including beam mask,
body-heading rotation and original additive texture/color equation. Current
modified/new source ASCII scan passed. Full candidate gates were not run.

Evidence and exact source/capture hashes: runtime-relative
build-artifacts/combined-20261008-root/headlights-r1/checkpoint.json.
Capture: pov0-r2.png; baseline: ../road-reflection-r1/after-pov0.png.

This is a CPU prototype, not a delivered Switch fix. GPU binding, shader,
runtime integration and hardware validation remain. Native lookup precision,
body-origin mapping and original receiver culling remain comparison limits.
Existing shadow shader interface failure is unchanged. No new NRO, push or
merge. Next: GPU implementation of this same projected light.
