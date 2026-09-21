# Rendering one bounded track region

[`TRACK.md`](TRACK.md) decodes the geometry a bounded region draws.
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry goes.
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) and [`TRACK_ZONES.md`](TRACK_ZONES.md)
decode which of it a camera sees. [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)
turns those three into one owned scene and one per-frame draw list.

This note decodes nothing either. It is the first backend of that draw list:
the deterministic CPU reference renderer of [`RENDERER.md`](RENDERER.md),
drawing a track region instead of a car.

Header: [`include/nfsu/track_render.h`](../include/nfsu/track_render.h).
Implementation: [`src/track_render.cpp`](../src/track_render.cpp).
Tests: [`tests/track_render_tests.cpp`](../tests/track_render_tests.cpp).
Command: `nfsu_probe track-render`.

## What this does not change

`src/render.cpp` is not modified by the commit that adds this layer. The
rasterizer, the camera, the clipper, the fallback colour and the frame
writer are used through the public declarations of
[`include/nfsu/render.h`](../include/nfsu/render.h), exactly as
`nfsu_viewer` uses them.

So the car path's frames are byte-identical by construction rather than by
measurement. They were measured anyway: all five reference hashes
[`RENDERER.md`](RENDERER.md) records reproduce on this branch, in three
build configurations, with each output deleted by exact name before its run
and each run required to have written a file and printed no error line.
See "Read-only retail acceptance" below.

## Where the boundary is

Issue #40's second criterion is that visibility selection sits above every
backend, and this layer is the first backend built to test that claim
rather than restate it. It holds:

- `render_track_draw_list` takes a `PreparedTrackScene` and a
  `TrackDrawList` and nothing else. It never builds a frustum, walks a
  bounding tree, reads a residency rule or calls the culler.
- `tests/track_render_tests.cpp` builds every draw list it renders from the
  plain-vector entry point, `build_track_draw_list(scene, visible, ...)`,
  and never constructs a `TrackFrustum`. A renderer test that had to reach
  for the culler would have been the counter-example.
- The one thing the renderer does read from the culler's vocabulary is
  nothing: `TrackDrawList::requested_policy`, `policy_held`,
  `sections_resident` and `sections_degraded` are provenance for a caller,
  and `render_track_draw_list` ignores all four. A backend that ignores
  them draws the same frame.

## The placement

Three coordinate conventions meet here, and two of them have already cost
this project a picture that was wrong and looked plausible. They are
composed in `track_placed_vertex` and nowhere else:

```text
slot  = (v.px, v.pz, v.py)          the file's raw slot order
moved = slot * linear + translation row-vector, row-major, row 3 is the
                                    translation
world = (moved[0], moved[2], -moved[1])
```

- The raw slot order is `src/track.cpp:1031`, which reads the three
  position floats as `px`, `pz`, `py` in that order.
- The transform is `TrackTransform` from
  [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md): nine int16 divided by
  `kSceneryMatrixOne` and three floats, not a 4x4.
- The world map is [`FORMATS.md`](FORMATS.md):286, `x = px`, `y = py`,
  `z = -pz`.

The second half is the same map `track_world_from_raw` applies for the
culler, and a test pins that the two agree on five points rather than
leaving one rule written twice. Another pins the row-vector, row-major
convention with a permutation matrix, for which a transposed read gives a
different answer on every input rather than agreeing by symmetry.

## The camera

A car sits at the origin and fits in a box, so `nfsu_viewer` can default to
distance 7 and target `0.2,0.35,0`. A region does not: the retail region
measured below is 429 by 160 by 139 world units across and its centre is
650 units from the origin. The car defaults render it entirely outside the
frustum, and the frame is background.

`frame_track_bounds` is the rule that replaces them. It needs no flags:

```text
target   = centre of the drawn box
radius   = half the box diagonal, that is the bounding SPHERE
ty       = tan(fov_y / 2)            tx = ty * width / height
s        = min(tx / sqrt(1 + tx*tx), ty / sqrt(1 + ty*ty))
distance = radius / s * margin       margin defaults to 1.05
near     = clamp((distance - radius) / 2, kMinNear, kMaxNear)
far      = min((distance + radius) * 1.25, kMaxFar)
```

`s` is the sine of the smaller of the two half-angles, so `distance * s` is
the half-extent the frustum admits at the target plane and a sphere of that
radius fits in both axes. No libm transcendental is used: `det_sin_deg`,
`det_cos_deg` and `sqrt` only, the same restriction the renderer keeps.

**The sphere and not the box, deliberately.** A box fit depends on the view
direction, so the framing would move whenever yaw or pitch did and two runs
at different angles could not be compared. The cost is standing farther
back than a box fit would, and it is bounded: the sphere radius is the box
half diagonal, so the ratio to the largest half-extent is at most
`sqrt(3)`. For the retail region it is `239.0686 / 214.32904 = 1.1154`,
eleven and a half percent.

The test does not restate that arithmetic. It projects all eight corners of
the box through `make_camera_frame` and `project_point` - the renderer's
own camera - and requires each to be inside the frame and between the near
and far planes, over eight yaws by five pitches, three frame aspects, five
box shapes and the `margin = 1` boundary. A wrong aspect direction, a
tangent where a sine belongs or a dropped margin fails there.

Every `--view-*` option of the command replaces one field of the framed
camera and leaves the rest, so one override does not discard the framing.

## `nfsu_probe track-render`

```text
build/nfsu_probe track-render <STREAM.BUN> <geometry-offset> <geometry-size>
      <object-offset> <object-size> --out <FILE>
      [--texture <file>]... [--lod-slot <0..5>]
      [--order <visible-instance|material-group>]
      [--eye <x> <y> <z>] [--target <x> <y> <z>] [--far <f>]
      [--residency <every|no-zone>] [--list <count>]
      [--width <N>] [--height <N>]
      [--view-yaw <deg>] [--view-pitch <deg>] [--view-fov <deg>]
      [--view-margin <m>] [--view-distance <d>]
      [--view-target <x> <y> <z>] [--view-near <d>] [--view-far <d>]
      [--cull <none|back|front>] [--shading <flat|none>] [--flip-v]
      [--alpha-cutoff <0..255>] [--vertex-color]
      [--background <r> <g> <b>]
```

Everything above `--width` is `nfsu_probe track-scene`'s and means the
same; the two commands print the same scene, cull and draw-list lines for
the same inputs, because those lines are printed by one shared function.
`--eye`, `--target` and `--far` select the CULL camera, in the world frame;
every `--view-*` option is the RENDER camera.

**The output is never replaced.** An existing `--out`, or one that names an
input, is refused before any file is opened, and `nfsu::write_frame_tga`
refuses again and publishes through an exclusively created
`<target>.partial`. `--out` is required; there is no default path.

It is the third `nfsu_probe` command that writes a file, after `obj` and
`export-textures`, and like them it opens the installation read-only.

### Residency, and what it can decide here

`--residency` carries `track-scene`'s two rules. The zone rule of
[`TRACK_ZONES.md`](TRACK_ZONES.md) is not offered by this command, and the
reason is that it would decide nothing for a bounded region: residency
selects whole SECTIONS, the region measured below holds exactly one, and
its number is 303, so `every_section`, `no_zone` (303 mod 100 = 3, below
10) and any zone list containing 303 all make it resident. Use
`nfsu_probe track-visibility --zones` to see the zone-driven set; it cannot
change this frame.

## The retail region

Container `Tracks/STREAML1RB.BUN`, geometry at 23015540 size 178268,
scenery at 23011344 size 4168, textures from
`Tracks/STREAML1RB_1210.BUN`. It is the region
[`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) measures, and its scene, cull
and draw-list lines are the same ones that note records.

```text
build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --out build/shots/region.tga
```

### Observed

- The camera is framed with no flags:

  ```text
  render: camera yaw 35 pitch 30 distance 834.7762050028643
          target 650.4794311523438,56.53457498550415,-139.00596725940704
          fov_y 35 near 10 far 1342.3060254589725
  render: eye 1242.6750802612487,473.92267748693627,275.65389034164036
  ```

- 1280x720, 3686418 bytes, SHA-256
  `D17D632B1648B46C26852E91B71BC27CC69D25216C8056AD15FD87E049EA9FD2`.
- 46 draw-list entries, 2827 triangles, 42 distinct groups, 17 meshes, 41
  texture binds. Every entry resolved a texture: `resolved 46 unresolved 0
  unusable 0`, and no pixel in the frame is a fallback colour.
- 2827 triangles submitted, all textured. 89 are degenerate at the source
  and 88 degenerate on screen; 1657 face the camera and 1082 face away;
  2739 rasterize. Nothing is clipped or outside.
- 164543 pixel tests, 58383 fragments, 11669 depth-failed and 46714
  written. 33449 pixels drawn, bounds x 436..851, y 175..622, so the region
  is inside the frame on every side.
- The drawn world box is

  ```text
  x 436.150390625 .. 864.8084716796875
  y -23.55398178100586 .. 136.62313175201416
  z -208.3073980808258 .. -69.70453643798828
  ```

  over 8481 visited vertices, against a stored-box union of
  `x 436..865 y -24..137 z -209..-69` over the same 18 instances. The two
  agree to within a unit on every face, which is what the int16 rounding of
  the stored boxes predicts, and the drawn box is inside the stored one on
  all six faces. The stored boxes are reported, never trusted:
  [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) measured them failing to bound
  their own mesh for 327 of 29674 retail instances.

### What is visibly in the frame

A night city block. A tall dark tower with a red-lit crown
(`XB_RJB_OMINOUS_TOWERA_1B_00`, 994 triangles, ten material groups), two
lit office slabs (`XB_GENBLDINGBE_A_00`, eight copies), a row of low
shopfronts with fire escapes, two elevated rail segments
(`TRSABC_TRACKS_CHOP2`), an overpass (`TRSABC_PROPS_OVERPASS_CHOP2`), two
tree clusters, and the street itself: `TRSABC_ROAD_CHOP14` with its lane
markings and `TRSABC_SIDEWALK_CHOP8` beside it.

The buildings stand upright, their bases sit on one plane, and the road
runs along the region's long axis. A top-down view (`--view-pitch 89
--view-yaw 0`) puts the drawn silhouette in 571 rows by 175 columns, a
ratio of 3.26; the region's own footprint is 428.66 by 138.60, a ratio of
3.09. The two are not required to be equal - one is a silhouette and the
other an axis-aligned box - but they agree closely enough to say the long
axis is where the geometry says it is and not across it.

### The ground plane, cross-checked against the collision

This is the check that the picture is at the right height, and it does not
come from the render path at all. The static collision of
[`TRACK_COLLISION.md`](TRACK_COLLISION.md) is a separate container, decoded
by separate code, that this renderer never reads.

Rendered, over the placed instances of this region:

| Mesh | drawn world y |
| --- | --- |
| `TRSABC_ROAD_CHOP14` | -22.7532 .. -21.2757 |
| `TRSABC_SIDEWALK_CHOP8` | -22.7532 .. -21.1319 |

Sampled from `Tracks/TRACKB1210.lzc` with `track-collision`, over the same
`(px, pz)` window, the indexed ground runs **-22.7539 .. -21.1719**.

The rendered road's floor and the collision floor differ by **0.0007**
world units and the sidewalk's ceiling and the collision ceiling by
**0.040**. At the scale measured below that is under a millimetre and four
centimetres. Two decoders that share no code agree on where the ground is.

The file-to-track correspondence was established twice and independently:
by the naming rule [`TRACK_COLLISION.md`](TRACK_COLLISION.md) records, and
by casting a ray down each of this section's 30 real instance translations
in all 31 `TRACKB*.lzc` files - only 1201 and 1210 put their surface a few
tenths of a unit above the instance origins, every other candidate that
hits anything is 13 to 79 units away.

Scale: the 240SX body mesh measures 4.374 by 1.929 by 1.243 world units
against a real 4.31 by 1.815 by 1.315 m car, and
[`TRACK_COLLISION.md`](TRACK_COLLISION.md) gives the median collision
polygon as 7.000 by 3.500 units, a 3.5 m lane. **One world unit is about
one metre**, so this region is a 430 m by 140 m patch with a 160 m tower in
it - three or four city blocks, not a continent.

### What the default camera is, and is not

The default frames the whole drawn region, tower included. That is the
right default for "render one bounded region", and it is also why the
picture is mostly empty: 33449 of 921600 pixels are drawn, 3.6 percent.
Two separate things account for that and neither is a framing fault.

- **The region is sparse.** 21 of its 39 instances place no mesh at all
  (see below), and the 18 that remain are towers and street furniture with
  air between them.
- **The target is the box centre, `y = 56.5`, and the road is at
  `y = -22`.** For a whole-region overview that is correct; for a view of
  the street it is 78 m too high, which is about the 25th floor. A
  street-level view is an explicit camera, and the one in the acceptance
  section below is on the road surface with the buildings either side.

## What is missing from the frame, in numbers

Nothing here is tuned until it looks acceptable. These are the holes.

- **21 of 39 instances place nothing.** They name 10 distinct mesh hashes
  that no mesh of this geometry region claims, because those meshes stream
  in with another region and this project cannot assemble more than one
  region yet. `unplaced: empty slot 0 skipped hash 0 unresolved 21 distinct
  10`. Where the game draws that scenery, this frame has background.
- **5 of the region's 22 meshes are never drawn.** They are
  `XB_GENBLDINGBE_B_00`, `XB_GENBLDINGBE_Z_00`,
  `XB_RJB_OMINOUS_TOWERA_1C_00`, `_1D_00` and `_1Z_00` - the B, C, D and Z
  members of two level-of-detail chains. `--lod-slot 0` selects the head of
  each chain, so the coarser members are correctly unused; which slot the
  game would draw at a given distance is undecoded
  ([`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)), and this is a documented
  choice rather than the retail rule.
- **No texture is unresolved.** `unresolved hashes 0`, `pixels fallback 0`.
  On this region the single `--texture` source covers every material group.
- **No retail material state.** `render_track_draw_list` refuses
  `MaterialStateMode::retail`. [`MATERIALS.md`](MATERIALS.md) proved that
  state for the car draw path, and whether the track path shares it is
  listed as unknown in [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md). The
  consequence is visible: a texture with an alpha channel - a fence, a
  chain-link, a foliage card - is drawn as an opaque quad here. The
  `--alpha-cutoff` diagnostic is available and is a diagnostic, not the
  retail rule.
- **Everything is drawn two-sided.** The default `--cull none` draws both
  faces, and the retail per-draw cull mode is recorded but not applied
  ([`MATERIALS.md`](MATERIALS.md)). Of the region's 2739 rasterized
  triangles 1657 face the camera and 1082 face away; `--cull back` keeps
  29365 pixels and `--cull front` keeps 18272, against 33449 for the
  default. The two do NOT partition the default - they sum to 47637 -
  because 14188 pixels are covered by a front face and a back face both,
  and the default resolves those by depth. The building shells are
  single-sided facades, so from some angles you see through one wall to the
  inside of the opposite one.

## Draw order

`TrackDrawOrder` changes the texture bind count and the order coplanar
surfaces resolve a depth tie, because triangle ids are the running count of
submitted triangles and the larger id wins.

On this region, measured rather than assumed:

| Order | Binds | Fragments written | Depth failed | TGA |
| --- | ---: | ---: | ---: | --- |
| `material-group` | 41 | 46714 | 11669 | `D17D632B...` |
| `visible-instance` | 45 | 46890 | 11493 | `D17D632B...` |

Grouping by material saves four binds, exactly the number of repeated
copies. The two orders write 176 fragments differently - the same total
fragments, resolved to a different owner 176 times - and the **finished
images are identical**. So this region has overlapping coplanar surfaces
but none where the two orders disagree on the winner. That is a
measurement, not a guarantee; the suite pins the rule directly, with two
coplanar quads whose winner does follow the order.

## Determinism

**Repeated runs of one build are byte-identical**, as
[`RENDERER.md`](RENDERER.md) requires. Every view below was rendered twice
in separate processes from each of two build directories, with the output
deleted by exact name first and both sides required to have written a file
and printed nothing on standard error.

**Across optimisation levels there is one divergence, and it is in
`src/render.cpp`, not in this layer.**

| View | Release (-O3) | Debug (-O0) |
| --- | --- | --- |
| default, `material-group` | `D17D632B...` | same |
| default, `visible-instance` | `D17D632B...` | same |
| street-level (below) | `20CCC85C...` | `F691A28D...` |

The street view's two frames differ in **61 pixels of 921600**, all of them
in the two-column strip `x 656..657`, `y 299..401`, which is one
near-vertical silhouette edge of a brick building seen almost edge-on.
Every statistic of the two runs is identical - the same pixel tests, the
same fragments, the same 471545 written, the same 365723 drawn and the same
drawn bounds - so coverage and depth agree and only the sampled texel
differs. The deltas run from 9 to 48, which is a neighbouring texel of a
detailed texture rather than a rounding step.

Four things were measured, in this order:

1. The fp flags are applied in both builds: `-O3 -ffp-contract=off
   -fno-fast-math` and `-g -ffp-contract=off -fno-fast-math`, read out of
   `ninja -t commands`.
2. Adding `-ffp-contract=off -fno-fast-math` to `CMAKE_CXX_FLAGS` globally,
   so that every translation unit carries them, changes nothing: the same
   two hashes.
3. Compiling **only `src/render.cpp` at `-O0`** inside an otherwise
   `-O3` Release build reproduces the Debug hash exactly,
   `F691A28D96876C7F864FA04FF54E5FB265B6F2C3A33A2E2B00EE9E3704F771AC`. The
   divergence is inside that one translation unit.
4. The car path does not reproduce it. All five
   [`RENDERER.md`](RENDERER.md) reference hashes are identical in Release
   and in Debug, and so is a deliberately hostile car view - grazing angle,
   `--near 0.0001 --far 90000`, `--fov 120`.

What separates the two scenes is magnitude. A car sits within a few units
of the origin; this region's vertices are 436 to 865 units out, so
`world - eye` in `project_point` cancels about nine bits before the
perspective-correct interpolation in `Rasterizer::fill` divides by an
interpolated `w` that spans three orders of magnitude across a grazing
wall. That is the regime where two instruction schedules of the same
expression can land on different texels.

**This is a pre-existing property of the reference rasterizer that a track
scene is the first to reach, and it is not fixed here.** Fixing it means
editing `src/render.cpp`, which is the one file this branch keeps untouched
so that the car reference hashes hold by construction. It wants its own
issue. [`RENDERER.md`](RENDERER.md)'s determinism section claims byte
identity for repeated runs of one build and says identity across compilers
has not been measured; that claim is still true, and this note is the first
measurement that shows where it stops.

The default view - the one this note pins a hash for - is identical across
both optimisation levels.

## Statistics

`render:` lines, in order: the frame size and whether the camera was framed
or explicit; the camera and the eye at full precision; the drawn and stored
world boxes; the instances the list named; entries walked, drawn, resolved,
unresolved, unusable and the texture binds; the distinct drawn instances,
meshes and groups; the region's meshes, rendered, skipped and unresolved
hashes; then the triangle, coverage and pixel blocks of
[`RENDERER.md`](RENDERER.md), unchanged.

Four groups of `RenderStats` fields are deliberately left at zero and the
header says so: `material_groups` and the `groups_*` counters, because a
draw-list entry is one COPY of one group and the same names would count
copies; and `groups_alpha_tested`, `groups_blended`, `draw_rows` and the
blend fragment counters, because the retail material state is refused.
`entries_drawn` is equal to `entries` for every scene a parser can produce,
because a material record with no indices is refused by `src/track.cpp` and
a group with an empty range is not drawable.

## Tests

`tests/track_render_tests.cpp`. Every fixture is a byte image written at
absolute offsets and parsed by the real parsers, so the vertex field order,
the int16 instance matrix, the material tiling rule and the alignment rules
all apply as they would in a retail file. Four fixtures: a flat one for the
counts and the geometry; one whose single instance carries a 45-degree
rotation about the world X axis, so the shade factor depends on a normal
that is not `(0, 0, 1)`; one rotated about the world Y axis instead, so the
normal's x component is not zero either; and a probe mesh whose first
triangle has a different unique extreme on each of its three vertices and
whose second triangle has no area.

Five pinned pixels do most of the work, all derived rather than recorded,
over the flat texel `(255, 120, 180)`:

- the lit face of the X-tilted quad is `(235, 111, 166)`, from shade 236;
- the same face from behind is `(90, 42, 63)`, from shade 90, which is what
  the clamp at zero produces once the normal is flipped towards the eye;
- the Y-rotated quad, whose normal has a non-zero x component, is
  `(177, 83, 125)` from `+x` and `(90, 42, 63)` from `-x`;
- with `--vertex-color` the flat texel becomes `(255, 60, 45)`, the vertex
  colour `(255, 128, 64)` folded in before the shade factor.

The four corners of the corner-textured quad are the four texels, which
pins the UV orientation absolutely rather than relatively.

### Mutations

Two sets were written against `src/track_render.cpp`, applied one at a
time, rebuilt and run. Both harnesses verify the revert against the
pristine text before applying the next mutant, refuse to start unless the
clean suite passes, and give no credit to a mutation that fails to compile.

| Set | Written | Compiled | Caught | Survived |
| --- | ---: | ---: | ---: | ---: |
| The author's, first pass | 60 | 59 | 43 | 16 |
| An independent pass, first run | 185 | 185 | 90 | 95 |
| The author's, after the fixes | 60 | 59 | 56 | 3 |
| The independent survivors, re-run after the fixes | 43 | 43 | 42 | 1 |

**The first row is the number a reader should distrust, and the second is
why.** The independent pass was run by a subagent that had not seen this
suite, against the same head, and it found 95 survivors out of 185 where
the author's own set found 16 out of 59. Of its 95, seven are provably
equivalent and three are guards no file can reach, leaving **85 real
survivors: 63 in `src/track_render.cpp` and all 22 it wrote against the
`track-render` command**. It also re-ran each of its 73 library survivors
against the whole CTest suite one at a time: none is caught by any other
test in the repository.

What the two passes found between them, and what closing it took:

- **No test read a pixel's colour at all.** The suite compared frames to
  each other and never to a value, so every fault that changes what a
  fragment IS - the UV pair, the vertex-colour channels, the light
  normalization, the clamp on the shade factor, the normal flip, the bound
  texture, the rounding in the shade factor - left every count, bind and
  total identical. Twelve separate mutations survived on that one gap. The
  suite now pins five exact pixels, derived and not recorded, and the four
  UV corners of a four-texel texture.
- **Every fixture normal was `(0, 0, 1)` and every camera was in front of
  it.** That made three more faults unfalsifiable: at a normal of
  `(0, 0, 1)` the shade is 156 whether the fixed light is normalized or
  not; with the eye always in front, the clamp at zero never ran; and a
  wrong sign in the cross product is exactly a negated normal there, which
  the flip towards the eye puts straight back. There are now three
  fixtures: flat, tilted 45 degrees about the world X axis, and rotated 45
  degrees about the world Y axis so the normal's x component is non-zero.
- **The flat texel was 60.** `(texel * shade + 128) >> 8` gives the same
  byte for shades 89 and 90 at a texel of 60, so dropping the `+ 0.5` from
  the shade factor was invisible. It is 255 now, where they differ.
- **`stats.min_x`, `max_x`, `min_y`, `max_y`, `pixels_blended` and
  `triangles_degenerate_source` were never read.** Six mutations to the
  pixel scan survived, including one that dropped the last row and column
  of every frame. The suite now scans the colour buffer itself and compares
  the whole rectangle, once with the geometry inside the frame and once
  with it overflowing every edge.
- **Five boundary comparisons turned refusals into out-of-range reads and
  the tests accepted it**, because they asserted only that the call
  returned false and a later check caught the garbage the earlier one let
  through. Every such refusal now asserts which diagnostic fired.
- **`validate_track_draw_list` was redundant with the bounds walk** for
  every case the suite had. The case only it catches is a list whose
  entries are valid and whose cost statistics are not, and that cost is
  what the submitted-triangle limit is measured against.
- **Three counts coincided with something else in every fixture.** The
  copies that reached the frame equalled the copies the scene held; the
  region's mesh total equalled its prepared total; and `entries_unusable`
  had no fixture at all, because nothing ever produced a hash that is
  claimed by a record the decoder could not use.
- **The near-plane rule was only ever tested where the clamp answered.** On
  the retail-shaped box `(distance - radius) / 2` is about 300 and
  `kMaxNear` is 10, so the rule itself was invisible.
- **The stored box was asserted on one axis of three**, and the bad-index
  skip was tested with every triangle broken, so a walk that stopped
  instead of skipping was indistinguishable from one that skipped.

Four mutants survive both passes after the fixes, and all four are stated
rather than hidden:

1. `if (distance > kMaxDistance)` to `>=`. Refusing a framed distance of
   exactly `kMaxDistance` needs a box whose `radius / sine * margin` is
   exactly 10000.0 in IEEE double, and no fixture can be built to land on
   it: `sine` comes from the polynomial `det_sin_deg`, so neither factor is
   under the test's control. Both neighbourhoods are tested - boxes far
   below are accepted, and a 9000-unit cube is refused with the distance
   diagnostic.
2. Removing the `validate_camera` self-check at the end of
   `frame_track_bounds`. Every clamp in that function is one of
   `validate_camera`'s own bounds, and the explicit checks catch the inputs
   that would otherwise reach it, so the two are mutually redundant by
   design. Removing **both** is caught, which is the evidence that neither
   is dead code.
3. `if ((first + count) / 3 > first / 3)` to `>=`, the guard that decides
   whether an entry drew anything. No file can produce an entry that draws
   nothing: `src/track.cpp` refuses a material record with no indices and a
   group with an empty range is not drawable. Same shape as
   [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)'s "no undrawable group", and
   the header says `entries_drawn` equals `entries` for that reason.
4. Scaling ONE lane of the fixed light by its own length instead of
   dividing by it. The light's length is `sqrt(1.005)`, so this moves one
   lane by one part in four hundred, and the shade factor is an integer
   0..256: over every normal the three fixtures carry, the two answers
   floor to the same shade. The whole-vector version of the same fault,
   which changes all three lanes, IS caught by the tilted fixture. Closing
   this one needs a normal tuned so that `k * 256 + 0.5` straddles an
   integer, which would pin a coincidence rather than a rule.

### What the command has no test for

The independent pass wrote 22 mutations against `run_track_render` and the
`track-render` block of `src/main.cpp`, and **all 22 survived**: nothing in
the repository ran that command's body. Two CTest entries now do - they
check that `--out` is required and that an existing `--out` is refused,
both decidable before any input is opened, in the shape the three
`nfsu_viewer_*` guard tests already use - and both were confirmed to fail
when the guard they pin is removed.

The rest of the command is covered by the read-only retail acceptance below
and by nothing else, which is the same position every other `nfsu_probe`
command is in: `nfsu_probe_usage_commands` and `nfsu_path_arguments` check
the inventory and the path arguments, and no test in this repository runs a
probe command against real data. The mutations that remain uncovered are
worth naming, because they are what that gap costs: framing the camera on
the stored box instead of the drawn one, ignoring a `--view-*` override,
accepting `--lod-slot 6`, accepting `--alpha-cutoff 256`, skipping the
first option after the positionals, and returning 0 where the reports
failed.

## Read-only retail acceptance

Every command below only reads the installation. The rendered images are
derived game data and are **not committed**; this note records their sizes
and hashes instead.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --out build/shots/region.tga

build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --out build/shots/street.tga \
    --view-target 600 -20 -85 --view-distance 55 --view-yaw 0 \
    --view-pitch 6 --view-fov 55 --view-near 0.5 --view-far 2000
```

Expected:

1. Both exit 0 and write nothing to standard error.
2. The first prints the scene, cull and draw-list lines
   [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) pins for this region,
   unchanged, then the `render:` block recorded under "Observed" above, and
   writes 3686418 bytes with SHA-256
   `D17D632B1648B46C26852E91B71BC27CC69D25216C8056AD15FD87E049EA9FD2`.
3. The second is the street-level view: the eye on the road at
   `600,-20,-85` looking along `-x`, with the road surface, its kerb and
   sidewalk, a brick warehouse facade and the tower above it. 365723 pixels
   drawn, drawn bounds `x 248..1156 y 0..719`, 3686418 bytes, SHA-256
   `20CCC85C62993298989CEDE3B3BFBBC0099F246573F2C3ED42BA9D0678B875A4` in a
   Release build. See "Determinism" for why a Debug build gives
   `F691A28D...` for this one view.
4. Running either twice in separate processes gives byte-identical output.
5. Re-running with `--out` naming a file that exists is refused with
   `already exists; this command never replaces a file`, exit 1, and
   nothing written.
6. The 476 files of the installation are unchanged in path, size and
   modification time before and after.
