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

**Rendered**, over the placed instances of this region, measured by
`track_draw_list_bounds` over a sub-list filtered to one prepared mesh:

| Mesh | entries | points | drawn world y |
| --- | ---: | ---: | --- |
| `TRSABC_ROAD_CHOP14` | 3 | 534 | -22.753185 .. -21.275707 |
| `TRSABC_SIDEWALK_CHOP8` | 1 | 165 | -22.753185 .. -21.131891 |

Their union occupies, in the **raw slot order** that `track-collision`
takes its arguments in,

```text
px 436.515808 .. 650.009583      pz 70.252800 .. 94.762497
```

**Sampled**, from `Tracks/TRACKB1210.lzc`, over exactly that window. THE
LATTICE IS PART OF THE RESULT and is stated so the figures can be
reproduced: `slot0 = px_min + k` for k in 0..213 plus the exact `px_max`,
which is 215 values, by `slot1 = pz_min + j` for j in 0..24 plus the exact
`pz_max`, which is 26 - **5590 sample columns, of which 2744 hit a
surface**. Each column is the probe's own ray: origin `(slot0, slot1,
1000)`, direction `(0, 0, -1)`.

```text
ground over the window: -22.753906 .. -21.143860
```

Both extrema are single `track-collision` invocations:

```text
build/nfsu_probe track-collision <Tracks/TRACKB1210.lzc> 601.515808105469 76.252799987793
  -> probe ray: polygon 3317 triangle 1 distance 1022.75 point (601.516, 76.2528, -22.7539)

build/nfsu_probe track-collision <Tracks/TRACKB1210.lzc> 464.515808105469 94.252799987793
  -> probe ray: polygon 5082 triangle 0 distance 1021.14 point (464.516, 94.2528, -21.1439)
```

**The agreement.**

| | rendered | collision | difference |
| --- | --- | --- | ---: |
| floor | -22.753185 | -22.753906 | 0.000721 |
| ceiling | -21.131891 | -21.143860 | 0.011969 |

At the scale measured below that is **under a millimetre on the floor and
about a centimetre on the ceiling**, between two decoders that share no
code.

An earlier draft of this section printed the collision ceiling as -21.1719
and the difference as 0.040, and a reviewer could not reproduce either.
The cause is worth recording because it is a trap this kind of measurement
sets: -21.171875 is exactly -5420/256, a stored quantum, and it is the
maximum of an INTEGER lattice over the ROAD mesh's window alone,
`px 441..650` by `pz 71..91`. The sidewalk reaches out to `pz 94.76`, and
the collision surface ramps upward across exactly that last strip - pz
90.25 gives -21.2929, then -21.2108, -21.1885, -21.1662, and 94.25 gives
-21.1439. The old lattice sampled the middle of that ramp and stopped
before its top. **It made the agreement look three times worse than it is**,
which is an unusual direction for an error to run in and still an error.

The word **integer** in that reconstruction is load-bearing rather than
incidental, and it is worth saying so because it is the only lattice choice
that lands on the stored quantum. A step-1.0 lattice anchored at the ROAD
window's own minimum corner - 212 by 23, 4876 columns, 2388 hitting - maxes
at -21.192993 instead, and the same integer lattice over the road-and-
sidewalk UNION window reaches -21.149536. Three plausible samplings give
three different ceilings, each correct over the window it covers, which is
the whole reason this section now states its lattice rather than only its
result.

### What the file pairing does and does not establish

The ray cast **rules out 29 of the 31** `TRACKB*.lzc` files and **cannot
separate the last two**. Casting the same ray down each of the section's 30
non-zero instance translations:

| file | instances hit, of 30 | nearest | median | furthest |
| --- | ---: | ---: | ---: | ---: |
| `TRACKB1201`, `TRACKB1210` | 9 | 0.062160 | 0.190926 | 0.977442 |
| `TRACKB1207` | 2 | 33.685514 | 37.802794 | 41.920073 |
| seven more files | 6 | 67.493010 | 75.075417 | 75.893677 |
| three more files | 4 | 67.493010 | 71.394956 | 75.248512 |
| the other 18 | 0 | - | - | - |

The seven are `TRACKB1003`, `1006`, `1102`, `1104`, `1107`, `1108` and
`1109`. 2 + 1 + 7 + 3 + 18 is 31, which is every candidate, and 31 less the
surviving pair is the 29 the sentence above says are ruled out. An earlier
draft labelled the seven-file row "six", so the table accounted for 30 files
while its own headline said 29 were ruled out; every measured value in it
reproduced, and only the label was wrong.

So the nearest rejected file is 33.7 units away and the furthest is 75.9 -
**a margin of about 33 units** between the surviving pair and everything
else. An earlier draft said "13 to 79"; neither end reproduces.

Three things about that table are worth stating rather than leaving
implied, because each of them could be read as more than it is:

- **Only 9 of the 30 translations hit anything at all**, in any file. The
  other 21 columns have no indexed surface under them. The pairing rests on
  nine agreements and twenty-one silences, not on thirty.
- **The nine zero translations are excluded on purpose, and excluding them
  is load-bearing.** Fold them back in and `TRACKB1099` hits all nine at a
  distance of exactly 0.000000, which beats 1201 and 1210 outright. The
  zero column is not evidence about this region; it is the origin, and
  several containers happen to have a surface there.
- **`TRACKB1201` and `TRACKB1210` cannot be told apart here.** They are
  different files - 246347 and 244396 bytes, SHA-256 `DCF7F514...` and
  `3EF7DDAC...`, 7913 against 7841 polygons and 10502 against 10424
  indices - with identical stored bounds, identical 277 cells and 195
  nodes, and **identical answers at all 5590 lattice columns of the window
  above**: the same 2744 hits, the same no-hit pattern, the same heights to
  the last bit, and the same polygon and triangle reported at both extrema.

  So the correct statement is one method plus a tie-break: the ray cast
  narrows 31 candidates to 2 with a 38-unit margin, and the naming rule
  [`TRACK_COLLISION.md`](TRACK_COLLISION.md) records picks 1210 out of the
  pair. An earlier draft called that two independent methods. It is not.

  **The cross-check above is unaffected by which of the two is right**,
  because both return the same ground everywhere in this window. That is
  the reason this ambiguity is recorded here rather than treated as a
  qualification on the agreement.

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

**The measured views are now byte-identical across optimisation levels.**

| View | Release (-O3) | Debug and the default (-O0) |
| --- | --- | --- |
| region, `material-group` | `D17D632B...` | same |
| region, `visible-instance` | `D17D632B...` | same |
| street-level (below) | `20CCC85C...` | same |

The third column covers two configurations, not one: an explicit
`-DCMAKE_BUILD_TYPE=Debug` and the default with no build type, which
carries no `-O` flag and so compiles at `-O0` as well. Both were measured.

Before the square-root fix, the same street command exited 0 with empty
standard error and wrote 3686418 bytes in each build, but gave these
different SHA-256 values:

```text
Debug   F691A28D96876C7F864FA04FF54E5FB265B6F2C3A33A2E2B00EE9E3704F771AC
Release 20CCC85C62993298989CEDE3B3BFBBC0099F246573F2C3ED42BA9D0678B875A4
```

The files differed at 61 pixels, all in `x 656..657`, `y 299..401`.
Coverage, depth and every reported statistic agreed. At pixel `(656,299)`,
triangle 2602 has constant source U `0.0546875` at all three vertices and
uses a 128-wide texture. Debug interpolated U as `0.0546875` (texel 7);
Release interpolated `0.054687499999999993`, the adjacent lower double
(texel 6). This is a texel selection change, not a colour rounding change.

The first differing instruction is earlier, in `normalize` while building
the pitch-6 camera. Both builds enter with the same forward vector and
`dot(v,v) = 0.99999999999999989`, the binary64 value immediately below 1.
At `-O0` and `-Os`, MinGW GCC 13.2 calls its `sqrt`, whose `fldl; fsqrt; fstpl`
sequence rounds through x87 extended precision and returns `1` at this
midpoint. At `-O2` and `-O3`, GCC emits `sqrtsd` and returns
`0.99999999999999989` directly. Neither `-ffp-contract=off` nor
`-fno-fast-math` prevents the compiler from choosing an out-of-line call
at `-O0` and a hardware square root at `-O3`. The one-ulp camera basis
change shifts projected `w`, and independent rounding in perspective
interpolation puts that constant U on opposite sides of the texel boundary.

The shared `det_sqrt` now uses `sqrtsd` on SSE2 x86 and `fsqrt` on Switch
AArch64, and the renderer and track camera call it at every double-precision
square-root site. The Debug and Release street frames both have the Release
hash above. The five car hashes in [`RENDERER.md`](RENDERER.md), including the
stock wheel frame `FD672733...`, and the default region hash `D17D632B...`
were rechecked unchanged in both builds, with fresh output files and
unchanged retail-file manifests. The camera basis at pitch 6 is pinned by
`nfsu_render_tests`.

The street command was also rerun in the default, Debug, Release,
RelWithDebInfo and MinSizeRel builds after the shared helper was applied to
track framing, lighting and normals. All five fresh 3686418-byte TGA files
are byte-identical with SHA-256 `20CCC85C62993298989CEDE3B3BFBBC0099F246573F2C3ED42BA9D0678B875A4`.
The retail-file manifest matched before and after every run. Both region
orders still have SHA-256 `D17D632B1648B46C26852E91B71BC27CC69D25216C8056AD15FD87E049EA9FD2`
in the default and Release builds.

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

**A mutation score describes a SET, not a suite, and no single number here
is the coverage of this code.** Five sets were run, three of them by
sessions that had not seen the tests. Each row says what it measured and
who measured it; they are not summable and they are not comparable to each
other.

| Set | Written | Applied | Caught | Survived | Unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| Independent pass over the library, at the first head | 185 | 185 | 90 | 95 | not separated |
| **Independent pass over the library, 200 mutants, after the first round of fixes** | **200** | **200** | **169** | **31** | not separated |
| Independent pass over the `track-render` command | 34 | 34 | 7 | 27 | not separated |
| The author's own library set | 60 | 59 | 56 | 3 | 0 |
| The author's own `track-render` command set | 34 | 34 | 33 | 1 | 0 |
| The independent library survivors, re-expressed and re-run | 43 | 43 | 41 | 1 | 1 |
| The reviewer's named library survivors, re-run | 11 | 11 | 10 | 1 | 0 |

**The last four rows were re-scored under the sweep rule
[`CLAUDE.md`](../CLAUDE.md) gained while this branch was in review**: a
non-zero exit that printed neither a failure line nor the suite's own
summary is an UNKNOWN result, not a kill. Applying it cost this branch one
of its own catches, which is the point of the rule and is recorded here
rather than quietly absorbed.

The mutant is T30, `draw.mesh >= scene.region.meshes.size()` weakened to
`>` in `track_draw_list_bounds`. An earlier round reported it caught. It is
not: with the guard weakened, a handle exactly equal to the mesh count
passes and the next line reads one past the end of the vector, so the suite
**segfaults with zero bytes of output** - exit 139, no `FAIL:` line, three
runs out of three, reproduced in isolation. The old classifier read that
non-zero exit as a kill.

It cannot be turned into a real kill either, and that is worth stating
rather than leaving as a to-do: the mutant's behaviour is undefined by
construction, so no assertion can distinguish "the guard refused" from "the
process died". What the test CAN do, and does, is pin the guard on the
pristine code by requiring the refusal's own diagnostic - and that is what
kills the two neighbouring handle mutants. T30 is counted as unknown, not
as caught and not as survived.

**The bold row is the number to quote for the library.** It is an
independent 200-mutant pass by a reviewer's subagent that had not seen this
suite, and of its 31 survivors it classed 13 as real coverage holes, 12 as
provably equivalent and 6 as unreachable behind
`validate_prepared_track_scene`. By function it reports
`track_placed_vertex` 13 of 13, `extend_bounds` 9 of 9,
`extend_bounds_from_raw_box` 8 of 8 and `sine_of_half_angle` 5 of 5, with
the survivors concentrated in `frame_track_bounds` (9 of 44) and
`render_track_draw_list` (20 of 95).

That set is not one this branch can re-run - it was written elsewhere and
is not in the repository - so the honest statement is that an independent
pass measured 169 of 200 at the head it was run against, and the rows below
it are what this branch can reproduce on demand.

An earlier draft of this table reported "43 written, 42 caught, 1 survived"
as though it described the library's coverage. It does not: 43 is the count
of survivors the FIRST independent pass named, re-expressed against this
source. Re-running a set selected for having survived says something useful
about whether those particular holes closed, and nothing at all about the
remaining 90 per cent of the code. The bold row is there because it is the
only measurement in this table taken over a set nobody here chose.

**An oracle caveat that affects every number above, and every mutation
number in this repository.** Test binaries that write to FIXED paths under
the system temp directory collide when two `ctest` runs overlap, which this
protocol's shared-checkout model makes routine. `nfsu_track_object_tests`
was fixed under issue #122 and the remaining files are issue #124. It
reproduces on a pristine tree, it is not introduced here, and it can only
manufacture a false CAUGHT - never a false survivor - so survivor counts
are sound and catch counts can be optimistic.

That is the same shape as the T30 result above and the same rule catches
both: a crash is not a kill. `tests/track_render_tests.cpp` does no file
I/O at all, and the command set runs only the five tests a `src/main.cpp`
change can reach, so neither of those two sets can be affected by the
collision; the T30 segfault was the mutant's own and reproduces with
nothing else running.

#### What the passes found, and what closing it took

- **No test read a pixel's colour at all.** The suite compared frames to
  each other and never to a value, so every fault that changes what a
  fragment IS left every count, bind and total identical. Twelve mutations
  survived on that one gap. There are now five exact pixels, derived and
  not recorded, and the four UV corners of a four-texel texture.
- **Every fixture normal was `(0, 0, 1)` and every camera was in front of
  it.** At that normal the shade is 156 whether the fixed light is
  normalized or not; with the eye always in front, the clamp at zero never
  ran; and a wrong sign in the cross product is exactly a negated normal
  there, which the flip towards the eye puts straight back. Four fixtures
  now: flat, tilted about world X, rotated about world Y so the normal's x
  component is non-zero, and a probe mesh with a unique extreme on each
  vertex of one triangle and no area on the other.
- **The flat texel was 60.** `(texel * shade + 128) >> 8` gives the same
  byte for shades 89 and 90 there, so a dropped `+ 0.5` in the shade factor
  was invisible. It is 255 now.
- **The two out-parameters were never checked for being reset.** A caller
  that re-uses one `RenderStats` - which is what a viewer redrawing a frame
  does - doubled every counter, and a caller that read the statistics after
  a REFUSED call got the previous call's numbers, because the material
  state refusal and both validators return early. Pinned in both
  directions now.
- **Triangle-id identity was unpinned.** Ids that start at one shift
  `stats.pixels_fallback`'s own table lookup by one and shift what
  `Inspector::draw_of_triangle` reads out of `Frame::ids()` by one group
  boundary, and no count or pixel would show it.
- **A non-finite framing option reached nothing that refuses it.** `NaN <
  low` and `NaN > high` are both false, so the range gates pass it and only
  the finiteness gate stands in front - and that gate is one `||` chain, so
  a single `||` turned into `&&` opens three lanes of four. All four lanes
  are now driven with a NaN and with both infinities.
- **`stats.min_x`, `max_x`, `min_y`, `max_y`, `pixels_blended` and
  `triangles_degenerate_source` were never read.** One mutation dropped the
  last row and column of every frame and nothing noticed.
- **Five boundary comparisons turned refusals into out-of-range reads** and
  the tests accepted it, because they asserted only that the call returned
  false and a later check caught the garbage the earlier one let through.
  Every such refusal now asserts which diagnostic fired.
- **`validate_track_draw_list` was redundant with the bounds walk** for
  every case the suite had. A list whose entries are valid and whose cost
  statistics are not is the case only it catches, and that cost is what the
  submitted-triangle limit is measured against.
- **The near-plane rule was only tested where the clamp answered.**

#### What still survives, and why

Three mutants survive the author's library set and one survives the
reviewer's named list. All four are stated rather than hidden:

1. `if (distance > kMaxDistance)` to `>=`. Refusing a framed distance of
   exactly `kMaxDistance` needs a box whose `radius / sine * margin` is
   exactly 10000.0 in IEEE double, and neither factor is under a fixture's
   control: `sine` comes from the polynomial `det_sin_deg`. Both
   neighbourhoods are tested.
2. Removing the `validate_camera` self-check at the end of
   `frame_track_bounds`. Every clamp there is one of `validate_camera`'s own
   bounds and the explicit gates catch everything that would reach it, so
   the two are mutually redundant by design. Removing **both** is caught,
   which is the evidence that neither is dead. The reviewer's independent
   pass classed this one as provably equivalent.
3. `if ((first + count) / 3 > first / 3)` to `>=`. No file can produce an
   entry that draws nothing: `src/track.cpp` refuses a material record with
   no indices and a group with an empty range is not drawable. The header
   says `entries_drawn` equals `entries` for that reason.
4. Scaling ONE lane of the fixed light by its own length instead of
   dividing. The length is `sqrt(1.005)`, so this moves one lane by one part
   in four hundred, and the shade factor is an integer 0..256: over every
   normal the four fixtures carry, the two answers floor to the same shade.
   The whole-vector version IS caught. The reviewer disagrees that closing
   this would pin a coincidence and suggests one triangle with a chosen
   normal would do it; that is a fair point and it is left open rather than
   argued away.

One command mutant survives the author's 34: printing the draw-list trailer
at a limit of zero. Catching it needs a NON-EMPTY draw list, and every case
in `tests/track_render_guards.cmake` runs without input files, so the list
is always empty and the mutant prints nothing either.

A case for it WAS written, and it has been deleted. It could not fail: a
reviewer removed the `args.list_limit > 0` guard, rebuilt, ran the retail
region, watched the trailer come back, and saw the guard script still report
Passed. An earlier draft of this paragraph then said the retail acceptance
was where the behaviour was covered, and that was not true either - none of
the acceptance expectations mentioned it. **No automated test pins this
line.** Acceptance item 3 now names it, both at the default limit and with
`--list 5`, which makes it a manual check that someone performs rather than
a claim about a gate that does not exist.

### What the command is tested for

An independent mutation pass over the `track-render` block of
`src/main.cpp` scored **7 caught out of 34**: nothing in the repository ran
the command's body.

An earlier draft of this note justified that by saying the command was "in
the same position every other `nfsu_probe` command is in", because "no test
in this repository runs a probe command against real data". **The code
contradicts that, and the contradiction is easy to demonstrate:**

```text
build/nfsu_probe track-render <a path that does not exist> 0 16 0 16 \
    --out <a new path> --view-distance 10 --view-target 0 0 0

  -> exit 3
  -> "geometry issue at 0: failed to read file size" on standard error
  -> the whole render: block, down to "render: drawn bounds none"
  -> a valid 1280x720 TGA, 3686418 bytes
```

The framing fallback, every `--view-*` override, `validate_camera`,
`render_track_draw_list`, the report, `write_frame_tga` and the exit status
all run with no input file at all. The gap was a choice, not a structural
limit, and it is now closed:

- `tests/track_render_guards.cmake`, one CTest entry driving **36 cases**,
  each asserting the exact exit status, the diagnostic, and whether an
  output file exists afterwards. A CMake script rather than more `add_test`
  entries because `PASS_REGULAR_EXPRESSION` makes CTest **ignore the exit
  status**, and the status is half of what is being pinned: the survivor
  that mattered most turned a run whose reports failed into exit 0, which
  changes no printed byte.
- `nfsu_probe_track_render_needs_out` and
  `nfsu_probe_track_render_keeps_its_output`, the two simpler entries, kept
  because they fail with a one-line message.

Four faults that survived before and do not now are worth naming, because
they are what the gap actually cost:

- **exit 0 on a failing run.** The exit status appears nowhere in the
  printed report, so every automated caller silently stopped seeing
  failure.
- **a TGA written after a fatal diagnostic.** Dropping the early return
  after the scene pipeline left the command printing "the camera is
  degenerate and builds no frustum" and then rendering and writing the file
  anyway - the data-loss class the two output guards exist for.
- **`--alpha-cutoff 300` wrapping to 44** and rendering, the only fault
  that changed pixels with no diagnostic at any layer.
- **a `std::logic_error` escaping `main()`** when the positional guard goes,
  which is the failure class issue #87 and `tests/path_arguments.cmake`
  exist to prevent in another code path.

Three code fixes came out of the same pass, all in the command rather than
the library:

- **every `--view-*` option is range-checked at parse time**, against the
  same bounds `frame_track_bounds` enforces. Before, an out-of-range one
  was not refused at all: the framing failed on it, the explicit-camera
  fallback caught the failure, and when `--view-distance` and
  `--view-target` were both present the run drew from the renderer defaults
  and wrote a TGA with the option silently ignored.
- **an abandoned framing is reported.** That fallback now prints
  `render: framing failed (...)`, so a frame that is not the one the
  options describe says so.
- **the draw-list trailer is not printed at a limit of zero**, which is
  `track-render`'s default. It used to print "... 46 more entries not
  listed" under a default this note describes as showing none.

## Read-only retail acceptance

Every command below only reads the installation. The rendered images are
derived game data and are **not committed**; this note records their sizes
and hashes instead.

The default configuration is enough for the region view, whose hash does
not depend on it. The street view's does, so it gets its own Release recipe
in the sibling `build-release/` that `.gitignore` covers, which is the
convention [`TRACK.md`](TRACK.md) and
[`TRACK_COLLISION.md`](TRACK_COLLISION.md) use for the same reason: the
shared default `build/` keeps whatever configuration another session left
in it.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --out build/shots/region.tga
```

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

build-release/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --out build-release/shots/street.tga \
    --view-target 600 -20 -85 --view-distance 55 --view-yaw 0 \
    --view-pitch 6 --view-fov 55 --view-near 0.5 --view-far 2000
```

Expected:

1. Both `ctest` runs report 48 tests passed, 0 failed.
2. Both renders exit 0 and write nothing to standard error.
3. The region view prints the scene, cull and draw-list lines
   [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) pins for this region,
   unchanged, then the `render:` block recorded under "Observed" above, and
   writes 3686418 bytes with SHA-256
   `D17D632B1648B46C26852E91B71BC27CC69D25216C8056AD15FD87E049EA9FD2`.
   **That hash is the same in all three configurations** - default,
   Release and Debug - which was measured, not assumed.

   Its output contains **no** `more entries not listed` line, because
   `track-render`'s default `--list` limit is zero and this note describes
   that default as showing none. Adding `--list 5` to the same command
   prints five `  instance ...` lines followed by
   `  ... 41 more entries not listed`. **This is the only place that
   behaviour is checked**, and it is a manual check rather than a gate;
   `tests/track_render_guards.cmake` explains why it cannot cover it.
4. The street view is the eye on the road at `600,-20,-85` looking along
   `-x`, with the road surface, its kerb and sidewalk, a brick warehouse
   facade and the tower above it. 365723 pixels drawn, drawn bounds
   `x 248..1156 y 0..719`, 3686418 bytes, SHA-256
   `20CCC85C62993298989CEDE3B3BFBBC0099F246573F2C3ED42BA9D0678B875A4`.
   Debug, Release and the default configuration now give the same hash.
   See "Determinism" for the pre-fix 61-pixel difference and its cause.
5. Running either twice in separate processes, from the same build
   directory, gives byte-identical output.
6. Re-running with `--out` naming a file that exists is refused with
   `already exists; this command never replaces a file`, exit 1, and
   nothing written; `--out` naming one of the inputs is refused with
   `--out must differ from every input file`, exit 1.
7. A run with no readable geometry still exercises the whole command:
   `track-render <a path that does not exist> 0 16 0 16 --out <new>
   --view-distance 10 --view-target 0 0 0` exits 3, prints the whole
   `render:` block and writes a 3686418-byte frame of background. That is
   what `tests/track_render_guards.cmake` drives, and it needs no retail
   data at all.
8. The 476 files of the installation are unchanged in path, size and
   modification time before and after.
