# Car shadow: retail trace (issue 188)

The retail trace preceded the renderer change. PR 210 is merged. The CPU
reference and deko3d command stream now draw the first ground-darkening
layer; console photo and timing remain to be recorded by the coordinator.
The standing acceptance is [issue 181](https://git.accad.pro/murzintsevav/nfsu-switch/issues/181).

## Provenance and reproduction

Read-only inputs from the owner's installation:

- `speed.exe`, 3178496 bytes, SHA-256
  `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
- `Cars/240SX/TEXTURES.BIN`, 697340 bytes, SHA-256
  `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA`.
- `fx/world.efx`, 2404 bytes, SHA-256
  `D1CDBD788D564C4CD408A060D0EDA4E99245F0EB54B18A4FCEE848D2966E6C98`.

All executable addresses are PC virtual addresses. Image base is 0x00400000;
for these .text/.data addresses, file offset is VA minus the image base.
Capstone 5.0.7 disassembled the ranges below. Reproduce with Python and
Capstone installed:

```text
python tools/car_shadow_trace.py --root <retail-root> --out out/188-codex-s8-trace
cmp out/188-codex-s8-trace/188-codex-s8-before.json out/188-codex-s8-trace/188-codex-s8-after.json
```

The script refuses an output directory under the retail root. It brackets
reads with manifests of every retail file's relative path, size, mtime in
nanoseconds and SHA-256, and compares manifest bytes even on an exception.
Its output contains instructions and numeric metadata; no asset is added
to Git. Effect source is decoded in memory with XOR 0xAA, as established in
[MATERIALS.md](MATERIALS.md). The script prints only relevant source lines.

## Mesh and texture

**Observed:** the main car draw function starts at 0x0056DC30. Its shadow
path is 0x0056EB0F..0x0056F475. It constructs a procedural textured grid,
not a shadow map or stencil pass: 0x0056ECCB and 0x0056ECF3 set two loops
to four; 0x0056ED00..0x0056ED7A writes sixteen positions with UV coordinates
stepping by 1/3 (float at 0x006CCA58). Positions have 16-byte scratch stride;
UVs have 8-byte stride. Submission packs them into 24-byte XYZ/color/UV
vertices (0x0056F2F1..0x0056F3EC, helpers 0x00410940 and 0x00410970).

**Observed:** 0x0056F280..0x0056F45D submits three strips of eight vertices
per texture, using 0x004107E0 and 0x0040F0F0. The queue marks these as an
immediate draw (`+0x0C = 1`, 0x0040F193). The immediate queue walker uses
`DrawPrimitiveUP`, D3D primitive type 5 (triangle strip), vertex count minus
two, and stride 0x18 at 0x0040EFF4..0x0040F003. Therefore the ground shadow
has eighteen triangles before any strip is suppressed for invalid road
samples (0x0056F28B..0x0056F295). These are structural deductions from the
loop bounds, not copied implementation/test pins.

**Observed:** there are two iterations (0x0056F463..0x0056F46F). The first
reads CarTypeInfo `+0x4B4` and its shadow color (0x0056F208..0x0056F21C);
the second reads `+0x4BC` and a different, animated color
(0x0056F21E..0x0056F22F). They are distinct layers. This issue targets the
first, ground-darkening layer; reproducing the second would require tracing
its texture and modulation separately. Do not interpret both as shadows.

**Observed:** when global 0x0077A920 differs from 3, initialization formats
`%s_SHADOWIG` (string 0x006BBD14) at 0x0056AC32..0x0056AC60, hashes it,
resolves it at 0x0056AC67, and stores the texture-info pointer in
CarTypeInfo `+0x4B4` at 0x0056AC7E. The other branch formats `%s_SHADOWFE`
(string 0x006BBD20, 0x0056ABEF..0x0056AC30). The draw's matching mode test
is 0x0056DCAC..0x0056DCC0. Calling these branches in-game/front-end is an
inference from their names and their road-query branches, not a recovered
enum declaration.

The stock 240SX texture-info record starts at file offset 0x43F8 in
`Cars/240SX/TEXTURES.BIN`: name `240SX_SHADOWIG`, name hash 0xF36596F5
at info +36, dimensions 128x64 at +68/+70. `240SX_SHADOWFE` is at 0x437C,
hash 0xF3659690. The script reads these directly. Softness is supplied by
the sampled texture; no blur/render-target pass occurs in the traced path.
A pixel-accurate reconstruction should use this retail texture rather than
inventing a blurred ellipse. Its decoding/storage follows
[FORMATS.md](FORMATS.md) and `nfsu_texture_tests`.

## State and order

**Observed:** both stock records store +85 = 2 and +86 = 1. The common
texture state at 0x0040F436..0x0040F4CC disables alpha testing for +85 != 1
and selects SRCALPHA/INVSRCALPHA for +86 = 1. Thus this shadow uses
source-over alpha blending, not a multiplicative blend-factor pair.
The factor decoding is also documented in [MATERIALS.md](MATERIALS.md).

**Observed:** the shadow explicitly selects effect object 0x0071ABB8
(0x0056F410), effect column 0. Effect initialization sets the column at
0x0040E210; the file table 0x007014E0 names column 0 `fx/world.efx`
([MATERIALS.md](MATERIALS.md), "The walk"). Its decoded line 61 multiplies
texture sample by vertex color and two. Lines 71..75 take alpha/blend
state from the common runtime; line 80 sets culling to NONE. The fixed
technique uses Modulate2x for color and alpha (lines 116/120).

**Observed:** ZENABLE is TRUE at 0x00408B2A..0x00408B2F and ZFUNC is
LESSEQUAL at 0x00408B4E..0x00408B53. No shadow-local disable was found in
the traced submission or `world.efx`. The immediate walker explicitly sets
ZWRITEENABLE FALSE at 0x0040EFC0..0x0040EFC4 and restores TRUE at
0x0040F01B..0x0040F020. The world blended-texture branch also disables
writes (0x0040F543). Therefore the traced normal shadow path tests depth
and does not write it. Depth testing under a different caller's state is
not proven by this static trace.

**Observed:** the file's +81 = 5 and +84 = 1 would initially select row 13.
Initialization OVERRIDES these: 0x0056B099 clears +84 and 0x0056B0A2 writes
+81 = 2 on the `+0x4B4` texture. The adjacent layer at `+0x4BC` is assigned
row 4 (0x0056B0C7..0x0056B0D0). Rendering the shadow with its on-disk row
would therefore be wrong.

**Observed:** the immediate append computes the bucket from texture +81,
+84 and effect column (0x0040F12B..0x0040F152). The flush walks rows in
ascending order, then columns (0x0040ED66..0x0040ED80,
0x0040F0AC..0x0040F0CF). Consequently, within a shared fill, row-0 opaque
road/car draws precede the row-2/column-0 shadow; row-5, row-8 and row-13
car glass/light draws follow it. There is no universal "all shadow before
all car" order. Opaque car pixels already in the depth buffer reject the
shadow. Ordering across different flush cycles is not established here;
[MATERIALS.md](MATERIALS.md) lists the flush sites and this limitation.

## Placement and fading

**Observed:** `CULLABLE_CAR_PART_SHADOW` is placement index 13. Its centroid
and rolling-radius offset follow 0x0056B33C..0x0056B43A, independently
traced in [PART_PLACEMENT.md](PART_PLACEMENT.md), "Derived placements".
This is culling placement evidence, not proof that the sixteen rendered
vertices are a rigid mesh attached at that centroid.

**Observed:** the visible grid uses CarRenderInfo `+0x10`/`+0x20` as
footprint inputs (0x0056DED2..0x0056DEDF, 0x0056EB61..0x0056EBBD), then
transforms local XY positions by a matrix via 0x005ABDAF
(0x0056ED11..0x0056ED24). It replaces their Z with road heights through
0x00440A40 (0x0056EEB0..0x0056EEC7). That helper evaluates a collision
plane at 0x00440A79..0x00440A93 and returns its normal at
0x00440AA9..0x00440AC4. Four wheel locations are queried separately
(0x0056EE20..0x0056EEA0); when a grid query fails, a valid wheel plane can
supply its height (0x0056EEDD..0x0056EF10). Thus horizontal placement
follows the car transform, while vertical shape follows the road.

**Observed:** 0x0056EF50..0x0056EF8A adds 0.04 (float 0x006B5DCC) above
accepted/fallback road heights. The index test at 0x0056EF12..0x0056EF27
adds a further 0.08 (0x006B5DC8) to selected grid points. Front-end/no-road
fallback uses 0x006F1AD4 as height (0x0056EFA6..0x0056EFD7). Invalid
samples are counted and can suppress a strip at 0x0056F28B..0x0056F295.
The exact terrain-validity policy of helpers 0x0056CE90/0x0056CEB0 remains
unresolved; flattening every failed sample to one body plane is an
approximation.

**Observed:** height difference is transformed car origin Z minus the
queried road height (0x0056E3AA..0x0056E3BD). It becomes a clamped fade
parameter f from 0 to 1 between float thresholds 0 and 3
(0x006B5D80/0x006B5D84, 0x0056E3C4..0x0056E42C). The grid expands by
`1 + 0.75*f` (0x0056EB6B..0x0056EB8D) and the normal in-game footprint
has another 1.05 scale (0x006B5DB4/0x006B5DB8,
0x0056EB55..0x0056EBBD). Exact meanings of the original footprint fields
have not been traced back to all their producers.

**Observed:** normal in-game shadow vertex color is RGB 0x80/0x80/0x80
with alpha clamped to 0..254 (0x0056F03E..0x0056F05A). Its pre-integer
alpha is `114 * abs(matrix[+0x28]) * (1-f)`; 0x0056EFD9..0x0056F03C
computes this from a matrix entry, not vehicle speed. The matrix entry is
consistent with vertical alignment, but interpreting it as a normalized
up-vector cosine requires a separate matrix convention proof. Alpha is
also multiplied by the texture sample and two in `world.efx:61`. No
speed-dependent fade was found in this ground-shadow calculation. This
is a bounded negative finding, not proof about every caller or game mode.

## Implementation and approximations

`src/car_shadow.cpp` decodes only `240SX_SHADOWIG` from the stock car pack
and builds the traced 4x4 UV grid as eighteen triangle-list triangles. The
CPU reference draws it into the track depth buffer before car composition.
The race command stream gives deko3d the same grid and texture, with one
draw, source-over blend, depth test and disabled depth writes. Its GPU
vertices and texture are resident; only one transform/material uniform pair
changes per frame. The shadow is visual only. `nfsu_race_run_tests` checks
the replay state and cached/reference commands; `tools/race_replay_check.py`
pins the changed race frames. The standalone stock-car frame has no road
and remains unchanged.

Approximation: road placement uses the current contact wheels' positions,
suspension compression and authored radii. A plane through three wheel
centres estimates the road normal, and each centre moves one rolling radius
along it. This avoids per-frame allocations in the collision query. The
retail path instead samples road height at each grid vertex and uses wheel
fallbacks for invalid samples. The current grid is planar and skips the
shadow when no wheel contacts the road. Its half extents use the car's
authored length and width, since the producers of the retail footprint
fields are not fully traced. A 0.04 normal lift, 0..3 height fade, 1.05
footprint scale and 0.75 expansion follow the traced values. The CPU/GPU
path batches the three retail strips into one draw. The track renderer
currently draws the shadow after its track list; retail interleaves it in
row 2 of an immediate queue, so overlap with later blended track layers
can differ.

Expected GPU cost is one resident-texture draw, eighteen triangles and one
sampled alpha blend per covered fragment. No shadow map, extra framebuffer
or per-frame vertex upload is used. The cost estimate still needs a Switch
GPU SCENE timestamp before and after this change, in docked and handheld
modes. The coordinator's console photo should include resting and turning
views; issue 55 owns frame-budget acceptance.
