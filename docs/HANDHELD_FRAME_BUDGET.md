# Handheld GPU budget, issue 55

This round starts at `2f2b305a6e54e5481c51516a9fdf57bf04e02c57`.
The acceptance target remains 60 fps, with about 13 ms for the GPU scene
and room for the HUD and pending effects. No console result from this
round exists yet. Lowering the simulation rate or skipping frames is not
part of this change. A short scene mean alone cannot establish that every
complete driving frame meets the budget.

## Profile before changing the renderer

Read the [issue 55 thread](https://git.accad.pro/murzintsevav/nfsu-switch/issues/55)
and the GPU SCENE/REPLAY page implementation in `switch/race_main.cpp`.
The scene report is elapsed GPU service, including timestamp/report costs,
not utilization. CPU submission time, vertex cost, fragment cost and
texture bandwidth cannot be separated by that one timestamp pair.

Historical console evidence at `3f53122`, handheld replay, roughly 60 s per
run: [PR 227 comment 5627](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/227#issuecomment-5627)
records scene means of 20.59 ms with sky off, 22.01 ms with sky after the
opaque world, and 25.64 ms with sky early. The sky stays on. At `d4fb769`
and `47b7231`, [issue 55 comment 5283](https://git.accad.pro/murzintsevav/nfsu-switch/issues/55#issuecomment-5283)
also shows the benefit of opaque-first submission. These experiments are
evidence that fragment/overdraw work matters; they do not establish that
the entire scene is fragment-bound.

Inspection of the base renderer, rather than the older first-executor
description in `DEKO3D_RENDERER.md`, establishes:

- Native scene, color and depth targets are 1280x720 in handheld as well
  as docked. The sky already follows opaque depth writers.
- `load_track_slice` retains DXT1/DXT3 top-level blocks. The backend uses
  BC1/BC2 images for them and RGBA8 for decoded/generated textures.
  Reimplementing compression is not a new saving. BC3/ASTC are SDK formats,
  but converting existing blocks to them would need new quality and load
  measurements; this round performs no recompression.
- Images have one mip level, sampled nearest/repeat. Mips are a possible
  future bandwidth lever, with filtering and cutout-coverage consequences.
- Static constants are already uploaded once; visible track command lists
  are rebuilt only when selection changes. Draw calls still execute on the
  GPU. Grouping commands into lists does not merge geometry.

The old `nfsu_frame_budget_probe` crashed on this base: its census indexed
the generated sky instance as a scenery instance. This round fixes that
classification and adds disjoint sky/opaque/cutout-or-nonwriting/blended
counts and a census of actual uploaded texture formats. Its existing
ordered selection comparison and complete replay verification remain.

Historical desktop measurement at `2f2b305`, with only this round's probe
census fix, Release GCC 13.2.0, Windows, five fixed replay poses, one warmup
and 120 frame samples (`out/c055-profile/stdout.txt` in the worker tree):

| Quantity | Observed |
| --- | --- |
| Track draws, including sky | 968 to 1561 |
| Track triangles, including sky | 51605 to 71728 |
| Opaque draws | 567 to 900 |
| Cutout or other nonwriting, nonblended draws | 308 to 428 |
| Blended draws | 60 to 231 |
| Unique texture bindings per sampled view | 151 to 231 |
| Resident texture payloads | RGBA8 2383872 B; BC1 11914432 B; BC2 2134016 B |
| Same texture dimensions expanded to RGBA8 | 106235392 B |
| Per-frame C++ allocations / static index visits | zero / zero |
| Frame preparation, sum of disjoint stage means | about 0.040 to 0.092 ms |

These CPU wall times came from a shared host without frequency/affinity
control. They are not Switch timings or a GPU throughput prediction.
Texture payload bytes are residency, not bytes fetched per frame. Existing
frame and replay pins are owned by `nfsu_race_replay_check`,
`nfsu_draw_commands_retail`, and `nfsu_race_run_retail_full_draws`.

Historical geometric coverage measurement at `2f2b305`, unchanged
`nfsu_sky_fragment_probe`, every 6000 replay steps, eight views at 1280x720:
mean rasterized sky fragments 1298286, depth-passing sky after opaque
165289, uncovered floor 151320. Uncovered sky minification share was about
5.8%. The probe models cutout coverage as solid, ignores some hardware
raster rules, and is not a GPU timer. It supports retaining the existing
sky order and gives little reason to prioritize sky mipmaps. It does not
measure track texture minification or establish track bandwidth cost.

## Ranked levers

| Order / lever | Expected gain and desktop evidence | Image change | Console must confirm |
| --- | --- | --- | --- |
| 1. Scene resolution, implemented | 540p removes 43.75% of pixels, 450p removes 60.94%; same geometry and draw selection. Existing console shader/order results justify testing pixel cost first. Savings apply only to the pixel-dependent component; upscale adds cost. | Behind `--scene-height`; default 720 unchanged | GPU mean/p95/max including upscale, full frame pacing, clarity and stability; target about 13 ms scene |
| 2. Opaque front-to-back | Potentially reduces opaque overdraw; the census shows many opaque draws. No additional ms saving is measured. It may reduce texture locality and change equal-depth winners. | Would require a switch and image review | Whether the remaining pixel cost is overdraw, and the tradeoff against state/bandwidth cost |
| 3. Load-time static geometry merging | Could remove GPU draw/setup work; many more draws than texture identities remain. CPU preparation is already small. A section/material bucket is only an optimistic count: instance culling, transforms, material constants and blend order still constrain batches. | Identity only with exact selected geometry/order; coarse section batches change visibility | Resolution-insensitive floor, actual merged draw reduction, GPU benefit, peak memory and total load within about 15 s |
| 4. Mipmaps / texture formats | Native BC1/BC2 already saves payload space. World minification is unmeasured. New mip retention/generation could improve locality but no saving is credited. | Would require a switch; cutout alpha coverage must be preserved deliberately | Track sampling/bandwidth cost and foliage quality; sky probe alone cannot justify it |
| 5. Tighter culling / LOD | Could lower geometry, fragment and draw costs. Existing reference selects a 250-unit far plane at sampled poses; arbitrary shortening would remove scenery. | Would require a switch and separate fidelity work | Pop-in, recovered LOD/group visibility rules, gains over the same replay |
| 6. Shader simplification | Current flat shading uses screen derivatives; changing lighting to vertex/baked shading risks the image and pending car/effect work. No gain is credited without profiling. | Would require a switch if lighting changes | ALU versus bandwidth cost; visual review with all effects |
| Sky off / early | Existing diagnostic, not a production optimization. Existing late sky is already substantially cheaper than early. | Existing `--sky` switch | Use only to isolate remaining sky cost; acceptance keeps sky on |

Only the first optimization lever is implemented in this round. The next
choice depends on the resolution sweep: implementing section merges first
would assume that draw/vertex cost dominates without measuring that claim.
The other rows are evaluated candidates, not completed optimizations.

## Resolution implementation and measurement model

One NRO accepts `--scene-height 720|540|450|360`. The default is 720.
The corresponding scene targets are 1280x720, 960x540, 800x450 and 640x360.
360p is a diagnostic for the remaining fixed cost, not a recommended
quality setting. All preserve the camera's 16:9 aspect and source command
viewport. No simulation, visibility, resource table, shader, geometry or
draw-order code changes. Reduced targets are refused with `--panel`.

Each acquired presentation slot owns its own scene color and depth image.
The existing completion fence protects their reuse along with uniforms
and command memory. A linear 2D blit fills the native 1280x720 presentation
image. Both source and destination layouts declare 2D-engine usage.
Barriers complete rendering before the blit and complete the blit before
the end timestamp, so page 7 includes upscale and synchronization cost.
The native path allocates no extra image and issues no blit or extra
barrier. The renderer's existing memory accounting/budget covers the new
images. Future native-resolution HUD work belongs after this scene blit.

Page 7 identifies both dimensions and says whether upscale is included.
Page 2 retains the original replay result and hash reporting. Existing
desktop pins prove the shared simulation/reference path remains intact;
they do not prove the scaled GPU image or hardware synchronization.

For interpreting matched console runs, use the hypothesis
`T(r) = F + P*r + U(r)`, where r is scene pixel count / native pixel count,
F is resolution-insensitive GPU work, P is native pixel-dependent work,
and U is upscale/synchronization cost (zero on the native path). This is
a model to test, not a timing claim. At the historical 22.01 ms native
mean, the zero-fixed-cost, zero-upscale illustrations are 12.38 ms at
540p and 8.60 ms at 450p. Real results will be higher if F/U are positive;
the historical build also differs from this base. Do not report these
illustrations as achieved savings. A flat resolution sweep points toward
draw/vertex/setup work; a strong slope supports pixel work but does not
alone distinguish ALU, overdraw and texture bandwidth.

## Reproduction

Use a real Python interpreter and put `/c/Strawberry/c/bin` before
`/mingw64/bin` for desktop builds/tests. Configure both `build` and
`build-release` with `-DNFSU_RETAIL_ROOT=<retail-root>`; use Release for
these probes. Run from the actual workspace root, with a fresh output
directory for each invocation:

```text
python tools/frame_budget_evidence.py <retail-root> out/c055-profile run -- ./build-release/nfsu_frame_budget_probe.exe <retail-root> tests/recordings/event1.txt
python tools/frame_budget_evidence.py <retail-root> out/c055-sky run -- ./build-release/nfsu_sky_fragment_probe.exe <retail-root> tests/recordings/event1.txt 6000
```

The wrapper hashes every retail file before and in a finally block after
each run. Compare each `before.json` and `after.json` with `cmp`, including
failed runs. The CTest retail fixture additionally checks directory
metadata. All outputs stay outside `E:/Games`. The track loader reads
`Global/GLOBALB.BUN`. No retail or derived asset belongs in Git.

The coordinator's console request lists the exact arguments and required
page photos. Start with the native/540p/450p runs in handheld, sky on,
roughly 60 s each, with no pause/reset. Record window ticks and repeat
unequal prefixes before attributing a difference. Then run a complete
replay at the promising setting, verify EQUAL on page 2, and assess frame
distribution with all effects before accepting 60 fps. An interrupted
60 s replay correctly says RUNNING, not EQUAL.
