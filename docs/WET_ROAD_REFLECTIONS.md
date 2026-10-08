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
  `0x409634..0x40966A`. The street-light part of `0x544F30` is implemented below; the other
  auxiliary categories are not identified as street lights.
- `0x409644` calls `0x544F30(view, 2)`. The mode-2 path walks streamed
  effect lists, calls `0x544E90`, then `0x5447C0` per effect record. The
  loader at `0x540F6A..0x54101D` accepts root `0x80135100`, aligned version-3
  header `0x135101` (count at +0x30), and 80-byte records in `0x135102`.
  These records are in decompressed `Tracks/TRACKB1003.lzc`, not the solid
  STREAM data. Enabled type 9 records are one authored street-light class;
  they are not the globe lamps visible at race tick 11344 (see below).
- `0x544857..0x5448BC` queries ground with `0x440A40` and caches
  `2 * ground height - lamp height`. Mode 2 substitutes that reflected
  height at `0x544923..0x54495F`, before its separate sprite visibility test.
  Ground hits strictly less than 0.05 from the light height become sentinel 9999 at
  `0x544899..0x5448AB` and mode 2 rejects that sentinel at `0x544952`.
  The port drops those records. Mode 2 also skips the other modes' 1.5
  size multiplier at `0x5444A9`; the port does not apply that multiplier.
  `0x5443F0` builds the flare quad; `0x544566..0x5445B3` scales the vertical
  components of the billboard basis by 10 (constant `0x6F3DF4`) in mode 2.
  It submits through `0x40C300` at `0x544776`.
- Type 9 selects descriptor `0x6F40D8` through `0x6F4198`. Its size endpoints
  are 30/100, color is RGB 110/100/75, alpha endpoints are 110/150, and
  texture index is 2. The loader `0x544790` uses hash table `0x6F3EE8`;
  index 2 is `0x3394FE62`, decoded from `Global/GLOBALB.BUN` in the port.
  `0x570D00` and `0x570C70` traverse vehicle effect lists through `0x56D8D0`;
  they are not the missing placed street lamps. `0x4AE7D0` calls the same
  record renderer on its own auxiliary list; this list's identity remains
  unrecovered, as does the solid object passed to `0x4AE650`.
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
- `0x4460DD..0x4460F5` adds 2 to height only when instance flags AND
  context include `0x100`. Context construction sets that bit for view
  types 10 through 15, not view 4. Applying that step to every reflected
  `0x100` piece would be incorrect; the port deliberately leaves it out.
- `0x40E7B0..0x40E7D4` binds texture `0x71AA28` through the effect's +0x84
  parameter handle (`ReflectedTex`, name at `0x701EE0`). The projected
  reflection parameter is set in the same effect setup. Material queues
  drain through `0x40ED50`; `0x40F41D` binds the material's diffuse texture.
  The override at `0x40CA26..0x40CA38` chooses effect 0, not effect 3.

## Implemented approximation

`--wet-road-reflections on|off` is the single effect control, default `off`,
for the track-path probe, desktop race and Switch race. The old fixed cyan
UV ribbons and base brightening have been removed.

The producer draws selected resident track instances to a black 160x90
color/depth target at 16:9. It selects the object's reflection model from
slot 4. When slot 4 is empty or skipped, the loader at
`0x445A66..0x445A9A` copies the last non-empty, non-skipped node of slots
0 to 3 into it. A non-empty slot 4 that does not resolve has its own node
and does not fall back; nor does an unresolved last earlier slot yield to
an earlier resolved one. The producer skips the second mirror for baked
`0x200` placement pieces and borrows existing uploads.
A separate conservative frustum query uses cached slot-4 bounds transformed
by the reflected placement. A main-view visibility miss no longer removes
a valid reflected source. This is not retail's exact spatial-tree/context
query or its distance threshold; it scans the resident prepared instances.

Textured source solids selecting ordinary world effect 0 now use the
recovered world.efx equation in the reflection target: linear texture
sampling times decoded BGRA vertex color times two, including source alpha
before its alpha test/blend. Generic directional flat shading is bypassed.
Other selectors and diagnostic material-off behavior retain their existing
paths. The source material has its own renderer marker, separate from the
main-world base and wet receiver, and cannot recursively sample reflection.

The race pass also renders type-9 street-light sprites into that target,
after solids, with additive blend, depth testing and no depth writes. Their
reflected height comes from the collision ground query and their vertical
billboard stretch follows the traced mode-2 path. Shared quad/texture data
is uploaded once, and camera-facing placements are refreshed each frame.
The loader explicitly targets TRACKB1003.lzc, matching the event-1-only
race; selecting lamp data for other tracks is not implemented.
The sprite size uses the recovered 30/100 endpoints with a port scale of
0.02 and fixed alpha 150/255. The complete retail size/FOV/alpha curve is
not recovered. Other flare categories, dynamic cars, bloom, and auxiliary
solid producers remain omitted. The diagnostic hand-path probe has no lamp
provider; use the race executable to inspect the street-light contribution.

The source is refreshed every frame, avoiding stale reflected signs during
camera movement. Halving both dimensions from the comparison source of
320x180 quarters its pixel area. Reducing frequency would save producer
work but introduce temporal lag; it is not an additional user mode.

Only solids selecting the 0x40 row sample the target. Reflection color is
added at the screen-projected position, multiplied by perspective-correct
vertex alpha and diffuse texel alpha. Bilinear sampling clamps at edges.
CPU and GPU use per-fragment masks. The receiver uses the original
worldreflect base term: texture RGB times decoded D3DCOLOR RGB times two,
plus the masked reflection. Output alpha is one; diffuse alpha only masks
the reflection. The CPU rounds the final sum, retaining continuous linear
texture samples until then. Stored track color bytes are BGRA.
The receiver uses linear base sampling even with the legacy global filter
option, and does not substitute generic directional flat lighting. Its
base term remains active when reflections are disabled. This does not add
mipmap selection or change source selection, placement or target resolution.

The original worldheadlightreflect effect additionally projects HeadlightTex
and HeadlightClipTex using HeadlightWVP and adds that light before multiplying
by the road texture. The race road path does not implement this contribution;
car lamp glow is not its substitute. Its live setup/selection remains to be
recovered before a matching road-light implementation can be claimed.

CPU/deko3d pixel equality is still approximate: software fixed-point edges,
double versus GPU float arithmetic, hardware filter precision, and the
existing opaque-first GPU ordering can differ. No hardware image comparison
has been measured for this receiver revision. Original-scene appearance
and performance remain unverified on Switch.

The source target is a render pass, not a procedural light pattern. Removing
or recoloring a selected source changes its reflection. The earlier claim
that the warm elongated pools at ticks 11344 and 11360 came from lamps was
wrong: review 5796 isolated them as reflected facades. The lamp-streak
acceptance criterion remains unmet. No brightness gain or arbitrary mesh
stretch has been added to disguise that missing result.

## Globe-lamp investigation (review 5796)

At `543122c`, an instrumented throwaway Release build recorded the draws
changing 5x5 regions around the three lamp heads at tick 11344, 640x360.
The glow is ordinary solid geometry: `XO_STREETLIGHTG_1B_00`, mesh hash
`8C325EAC`, material texture `3394FE62`. The prepared draw is 2375, with
90 indices starting at 564. Instances 1307, 566 and 564 produce the left,
middle and right heads respectively. Their flags are 0x60, so all pass
the reflection selector. The pole material is draw 2374, texture `4D49FB44`.
These are not the type-9 TRACKB records or an unidentified auxiliary list.

Their reflection slot selects `XO_STREETLIGHTG_1Z_00`, hash `8C3F87C4`.
Its flare material is draw 2377, 60 indices starting at 276, with the
same texture. Instrumentation confirms all three instances submit this
draw to the existing reflection pass. The source solid containers are
at STREAML1RA.BUN offsets `0x1A4BF44` and `0x1A4EB0C`; their children
contain metadata, texture hashes and geometry, with no embedded effect
record chunk. The reflection flare vertices form crossed, authored quads.

The relevant executable path is scenery selection at `0x445EC0`, slot 4
at `0x445F76..0x445F91`, and the mirrored placement at
`0x4460B9..0x446130`. `0x446730` consumes that queue and submits its
selected solid through `0x40C910` at `0x44685E`. The material loop at
`0x40C9D0..0x40CA79` obtains the texture via material +0x1C and the
solid's texture table +0x2C, then enqueues it through `0x40F1E0` at
`0x40CA54`. Reflection scenery is drained at `0x409623`, before the
separate mode-2 effects call at `0x409644`. The recovered mode-2 stretch
at `0x5445AB/0x5445B3` therefore does not establish a stretch rule for
these authored solid quads.

At the same head, subtracting a render with reflection materials using
`3394FE62` removed from the normal wet render isolates small spots, not
long streaks. Rendering only those materials into the source target also
produces spots. The two forms distinguish a missing producer from masking
and occlusion: the solid glow producer already exists. This investigation
does not establish another retail producer or a faithful inexpensive way
to turn these spots into the requested streaks. Retail appearance and
console performance remain blockers; the type-9 contribution must not be
presented as having resolved either one.

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
python tools/frame_budget_evidence.py "$RETAIL" out/wet-race-cost run -- build-release/nfsu_frame_budget_probe "$RETAIL" tests/recordings/event1.txt --wet-road-reflections on
cmp out/wet-race-cost/before.json out/wet-race-cost/after.json
```

Windows executables use `.exe`. `REFLECTION_SOURCE` reports the additional
race draws and triangles actually selected by the Switch command path when the probe is invoked
with `--wet-road-reflections on`. With `off` the source count is zero.
The probe also prints lamp counts, cache bytes and CPU preparation time.
The CPU hand-path benchmark uses its larger diagnostic list, so its counts
must not be presented as Switch race counts. The review handoff records the
measured outputs and the exact revision.

Expected Switch cost: extra geometry/submission work for those selected
race draws, a low-resolution color/depth pass, one bilinear source lookup
per wet receiver fragment, and two render-target transitions. Geometry
processing does not shrink with target resolution. GPU timestamps bracket
both passes. The target/depth/uniform/descriptor allocations are included
in the renderer's memory accounting, only when the effect is enabled. Off allocates no reflection target, depth,
uniform block, extra sampler/descriptor or additional command capacity.
On reserves a bounded 4096-draw source and two frame slots; this includes
about 4 MiB of reflection uniforms and up to 32 MiB of additional command memory.
Main-view scene textures are not duplicated or uploaded each frame.

No millisecond estimate can be validated by the CPU rasterizer. Expect a
positive frame cost; the already over-budget handheld baseline has no
headroom for this effect. The existing baseline measurements are recorded
in PR #227 comment 5627. The mandatory 60 fps criterion remains unmet and
must not be waived or inferred from desktop timings. Default stays off.

## Automated checks and integration

- `nfsu_track_render_tests` checks source removal/recoloring, solid selection,
  zero vertex/diffuse alpha, lamp removal/masking/camera movement, separate
  reflection culling including a source beyond only the far plane, the
  0x200 exception, the lamp ground-height tolerance and unchanged coverage.
  `nfsu_track_draw_list_tests` checks independent slot-4 selection/fallback.
- `nfsu_draw_execution_tests` requires the Switch lamp recorder to emit the
  visible fixture lamp with its own quad/texture resources, reflected height,
  unlit material, additive blend, depth test and disabled depth writes.
- `nfsu_race_replay_check` checks paired wet/dry hand-path frames and wet race
  frames, complete per-step states, trajectory and results. Its current pins
  live in `tools/race_replay_check.py`, not this document.
- `nfsu_shader_source_guards` prevents the F2 fallback whole-word options
  check. Bit 0 is flat shading; bit 1 is wet; bit 2 is the internal producer.
  All shader manifests/blobs carry RGBA vertex color and the two textured
  samplers. Fallback writes RGB only, so its carried output alpha is inert.
- The Release gate retains `nfsu_race_run_retail_full_draws`; the main draw
  comparison remains complete. The initial wet frame also compares cached/reference reflection definitions
  and validates their uploads, indices and pipelines.
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
