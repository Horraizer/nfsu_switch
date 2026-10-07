# Exact generic race material specialization

`include/nfsu/race_effect_fast.h` selects only the generic textured car and
track programs whose angle-dependent alpha, environment, diffuse gate and
lamp terms are zero. The source material, resource table and sampler remain
unchanged. Shaded car surfaces, sky, ground shadows and reflection receivers
stay on their original paths. The accepted frontend specialization remains
separate and has priority when frontend lighting is enabled.

The renderer copies a private material payload. Marker 4 denotes a material
that was already unlit; marker 5 keeps the original geometric flat shade.
Both textured fragment shaders retain UV flipping, sample rounding, vertex
or constant-color modulation, source-alpha rounding and fixed-function state.
The cutout shader tests the original sampled alpha before specializing.

Static package payloads are selected from immutable material coefficients at
open. Neither marker encodes frame lighting, vertex-color or cutoff flags.
The shader reads those flags each frame, and the existing cutoff cache key
still selects the appropriate fragment shader. Static draw lists retain every
draw and their original chunk boundaries. Reflection uploads specialize
private payloads but retain their original draw count and uniform slots.

Dynamic main-view draws with exactly zero source alpha can be omitted only
with additive or source-over blending, RGB-only writes and no depth writes.
This retains every live effect. The original dynamic uniform index remains
the binding index, and fixed-state reuse follows the last emitted draw.
Resource allocation, validation and recorded diagnostics retain the original
command stream.

## Verification and acceptance

- `nfsu_race_effect_fast_tests` executes scalar functions extracted from the
  actual GLSL, with rounding boundaries and unsafe-state controls.
- `nfsu_race_effect_source_tests` binds those equations to both production
  shader branches, flat-light operations, private upload paths, cutoff cache
  and draw emission. Its negative controls must fail.
- `nfsu_race_effect_retail_*` inspect real cached and reference race draws
  under both filters and car-shading states, including live particles,
  glows and wet reflection sources. CPU material frames compare the original
  sampled equation with the actual specialized scalar equations, including
  modulation, lighting flags, alpha tests, cutoff and blend results.
- The existing retail replay and frame gates remain the full CPU camera-frame
  reference checks. The new material frames are equation panels, not GPU
  captures or a claim of console pixel parity.

Console acceptance compares baseline and candidate with identical scene
height, view, effects, filters, car shading, HUD, clocks and input. First
measure normal presentation; use `--profile-frame` separately for GPU timing,
because it waits for measurements and can change cadence. The coordinator
owns serial uploads and records exact NRO hashes, launch arguments, visual
evidence and measured racing benefit. Build and host parity alone do not
establish a performance improvement or stable frame rate.
