# Wet road reflections (#183)

## Evidence and retail producer

The read-only Capstone 5.0.7 trace used `speed.exe`, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
Addresses are virtual addresses in that image. Research runs used
`tools/frame_budget_evidence.py` to compare the full retail manifest before
and after execution. The port reads `Global/GLOBALB.BUN`.

Recovered by static disassembly:

- `0x40C8BD..0x40C906` walks the selector table at `0x701440`:
  bit 1 takes priority over 0x40, which takes priority over 0x80. The 0x40
  row selects effect 2, associated with `fx/worldreflect.efx` by the effect
  name table at `0x7014E0`. Track solid chunk `0x00134900 +12` supplies
  these flags. `track_vertex_format` retains the selector precedence.
- The XOR-0xAA effect source projects vertices to screen UV with flipped Y.
  Its reflection term is `vertex alpha * diffuse alpha * ReflectedTex(uv)`.
  Thus the track vertex color's fourth byte is the wetness mask; neither
  an upward normal nor an invented road-height threshold selects wet areas.
- `0x40A890..0x40A8F6`, gated by `0x700E90`, creates a one-level 320x240
  render-target texture at `0x71AA28`, level-zero surface at `0x71AB68`,
  and matching depth surface at `0x71AB64`. The D3D9 CreateTexture call
  at `0x40A8C0` receives usage 1 and format from `0x702A94`.
- The target descriptor at `0x71AA68` is used by view 4 (`0x7405E0`,
  descriptor pointer `0x740638`). `0x40ABD0..0x40AC7A` binds its color and
  depth targets, clears black/depth, and sets its viewport.
- `0x40A671` calls the scene renderer `0x409580` each rendered frame.
  `0x4095AE..0x4095D8` checks quality, active view, and target, then binds
  and clears the reflection target. There is no reduced-rate or modulo
  gate on this path. This is a static scheduling trace, not a GPU capture.
- When quality `0x700E90 > 1`, `0x4095F5` submits scenery via `0x446730`,
  followed by calls to `0x570DB0` and `0x570950`. The latter is the car
  render-node path described in `PART_PLACEMENT.md` (wheel/brake matrix
  copies at `0x570A77..0x570B02`); retail therefore includes dynamic car
  content as well as scenery. Quality 1 skips these three calls.
- The common tail calls `0x4AE650` for object `0x746DD0` at `0x40961E`,
  then drains the queues at `0x409623`. That auxiliary producer is itself
  gated by `0x4AE530`; it selects view-dependent data, iterates ten solid
  records at `0x4AE73D..0x4AE785`, and submits via `0x40C910`.
  Its asset identity is not recovered here. `0x700EA0` additionally gates
  effect calls `0x544F30`, `0x570D00`, `0x570C70`, and `0x4AE7D0` at
  `0x409634..0x40966A`. These auxiliary effects are not implemented here.
- Reflection culling passes context bit `0x800` at `0x40903B..0x409045`.
  `0x53E2E7..0x53E2F3` adds low-byte 0x40 to the common 0x10 mask.
  `0x445EDA..0x445EEB` rejects an instance if
  `((instance.flags ^ 0x60) & 0x50) != 0`. This is a selected scene,
  not every object and not a brightness classification.
- For that context, `0x445F5B..0x445F91` selects reflection LOD slot 4
  (runtime pointer object +0x34), subject to the object +0x20 threshold.
- `0x4460B9..0x4460DA` builds the instance matrix and translation.
  `0x4460F8..0x446108` negates matrix +0x28, its local vertical diagonal,
  leaving translation +0x38 unchanged. For upright objects the mirror is
  about the object's local-origin height. This does not establish a single
  global road plane. The special instance bit 0x200 follows the identity/
  null-placement branch at `0x446063..0x4460A6` instead.
- `0x40E7B0..0x40E7D4` binds texture `0x71AA28` through the effect's +0x84
  parameter handle (`ReflectedTex`, name at `0x701EE0`). The projected
  reflection parameter is set in the same effect setup. Material queues
  drain through `0x40ED50`; `0x40F41D` binds the material's diffuse texture.
  The override at `0x40CA26..0x40CA38` chooses effect 0, not effect 3.

## Implemented approximation

`--wet-road-reflections on|off` is the single effect control, default `off`,
for the track-path probe, desktop race and Switch race. The old fixed cyan
UV ribbons and base brightening have been removed.

The producer draws the selected real track instances to a black 160x90
color/depth target at 16:9, applying the local vertical-matrix sign change.
It borrows the existing geometry and texture uploads. The race producer
uses the already selected visible main-view draws. Main-view geometry/LOD
and culling are reused, instead of loading reflection LOD 4 or doing a
second visibility query. Car reflections, auxiliary effect producers and
the special 0x200 placement branch are omitted. These are explicit
approximations; this is not a complete reconstruction of the retail pass.

The source is refreshed every frame, avoiding stale reflected signs during
camera movement. Halving both dimensions from the comparison source of
320x180 quarters its pixel area. Reducing frequency would save producer
work but introduce temporal lag; it is not an additional user mode.

Only solids selecting the 0x40 row sample the target. Reflection color is
added at the screen-projected position, multiplied by perspective-correct
vertex alpha and diffuse texel alpha. Bilinear sampling clamps at edges.
CPU and GPU use per-fragment masks and round additions with floor(x+0.5).
The old per-triangle strength and GPU truncation difference are gone.

CPU/deko3d pixel equality is still approximate: software fixed-point edges,
double versus GPU float arithmetic, hardware filter precision, and the
existing opaque-first GPU ordering can differ. No hardware image comparison
has been measured for this revision. Main scene diffuse lighting retains
the port's existing rule; retail's 2x diffuse term is not introduced here.

The source target is a render pass, not a procedural light pattern. Removing
or recoloring a selected source changes its reflection. On the reviewed
hand-path view at sample 6000, red/white barriers now reflect directly below
the barriers and the false blue road band is gone. The supplied reference
images were inspected in place, without copying them. Compared with retail
06/12, the result remains softer and fainter, especially the long bright
lamp streaks: it lacks retail reflection LOD/effect content and glow/bloom.
It does not yet match the original game's complete lighting appearance.

## Cost and reproduction

The former 0.1-0.4 ms estimate is withdrawn. Its assumed 300,000 road pixels
was already below the reviewer's changed-pixel count, and the implementation
now has an actual extra scene pass. A changed-pixel count also excludes
occluded work and is not a shaded-fragment count.

Run the existing hand-path command with `--benchmark-wet-road` in Release.
This diagnostic compares off, 160-wide and 320-wide reflection sources;
it is not a second game setting. For each captured pose it warms once,
times three complete CPU renders, prints their median, changed pixels,
and producer draw/triangle/fragment counts. Texture/scene loading and file
writes are outside that timed interval. The normal `--out` frame uses the
single on/off setting. `tools/race_replay_check.py` contains the full path
arguments. Example, with RETAIL set to the read-only installation:

```sh
python tools/frame_budget_evidence.py "$RETAIL" out/wet-cost run -- build-release/nfsu_probe track-stream-path "$RETAIL/Tracks/STREAML1RA.BUN" "$RETAIL/Tracks/TRACKB1003.lzc" 1003 --hand-path 1003 --texture "$RETAIL/Tracks/TEX1003TRACK.BIN" --texture "$RETAIL/Global/INGAMEB.BUN" --texture "$RETAIL/Global/GLOBALB.BUN" --list none --frame-every 3000 --out out/wet-cost-frame --width 1280 --height 720 --wet-road-reflections on --benchmark-wet-road
cmp out/wet-cost/before.json out/wet-cost/after.json
python tools/frame_budget_evidence.py "$RETAIL" out/wet-race-cost run -- build-release/nfsu_frame_budget_probe "$RETAIL" tests/recordings/event1.txt
cmp out/wet-race-cost/before.json out/wet-race-cost/after.json
```

Windows executables use `.exe`. `REFLECTION_SOURCE` reports the additional
race draws and triangles actually selected by the Switch command path.
The CPU hand-path benchmark uses its larger diagnostic list, so its counts
must not be presented as Switch race counts. The review handoff records the
measured outputs and the exact revision.

Expected Switch cost: extra geometry/submission work for those selected
race draws, a low-resolution color/depth pass, one bilinear source lookup
per wet receiver fragment, and two render-target transitions. Geometry
processing does not shrink with target resolution. GPU timestamps bracket
both passes. The target/depth/uniform/descriptor allocations are included
in the renderer's memory accounting, including the off-state allocation.
Main-view scene textures are not duplicated or uploaded each frame.

No millisecond estimate can be validated by the CPU rasterizer. Expect a
positive frame cost; the already over-budget handheld baseline has no
headroom for this effect. The existing baseline measurements are recorded
in PR #227 comment 5627. The mandatory 60 fps criterion remains unmet and
must not be waived or inferred from desktop timings. Default stays off.

## Automated checks and integration

- `nfsu_track_render_tests` checks source removal/recoloring, solid selection,
  zero vertex/diffuse alpha, and unchanged surface coverage.
- `nfsu_race_replay_check` checks paired wet/dry hand-path frames and wet race
  frames, complete per-step states, trajectory and results. Its current pins
  live in `tools/race_replay_check.py`, not this document.
- `nfsu_shader_source_guards` prevents the F2 fallback whole-word options
  check. Bit 0 is flat shading; bit 1 is wet; bit 2 is the internal producer.
  All shader manifests/blobs carry RGBA vertex color and the two textured
  samplers. Fallback writes RGB only, so its carried output alpha is inert.
- The Release gate retains `nfsu_race_run_retail_full_draws`; the main draw
  comparison remains complete. The extra pass reuses those definitions.
- CTest retail fixtures and explicit research wrappers check the retail
  installation before and after execution. Run default, Release, Switch,
  and ASCII gates as required by `CLAUDE.md`.

No merge of origin/main is part of this fix round. The coordinator resolves
the known #227 conflicts and must carry the options bit masks into its
shaders, retain its sky ordering, and rerun merged-result gates.

## Console request

The structured `CONSOLE_REQUEST` on PR #242 identifies the exact head.
The coordinator runs the same replay with wet off/on, each with
`-- sdmc:/nfsu --replay --profile-frame --wet-road-reflections off|on`,
and captures GPU SCENE page 7 and REPLAY page 2 after 60 seconds with the
same mode/clocks. Check mean/p95/max GPU time, frame time, stable GPFIFO and
memory, replay pins and visual reflection placement during camera motion.
Include a wet-on, flat-off view containing a fallback draw to verify F2.
Console performance and visual acceptance remain pending that paired run.
