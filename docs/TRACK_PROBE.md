# Inspecting one bounded track region, and why anything is on screen

[`TRACK.md`](TRACK.md) decodes the geometry a bounded region draws.
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry goes.
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) and [`TRACK_ZONES.md`](TRACK_ZONES.md)
decode which of it a camera sees. [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)
turns those three into one owned scene and one per-frame draw list, and
[`TRACK_RENDER.md`](TRACK_RENDER.md) draws it.

This note decodes nothing either. It is the read-only inspection layer over
all six: for one instance of one region it prints where every byte came from,
what places it, what it draws, and - the part no other command in this
repository can print - **why it is or is not on screen**.

Header: [`include/nfsu/track_probe.h`](../include/nfsu/track_probe.h).
Implementation: [`src/track_probe.cpp`](../src/track_probe.cpp).
Tests: [`tests/track_probe_tests.cpp`](../tests/track_probe_tests.cpp) and
[`tests/track_probe_guards.cmake`](../tests/track_probe_guards.cmake).
Command: `nfsu_probe track-probe`.

## The gap this closes

`nfsu::cull_track_region` answers one question - which instances are visible -
and, like the retail path it reproduces, it stops at the first gate that
rejects one. `nfsu_probe track-visibility` prints that answer. A reader can
therefore see **that** an instance is missing from a frame and cannot see
**why**, and the four possible whys are not interchangeable:

- the section it belongs to is not resident at all;
- a node above it in the bounding tree was outside the frustum, so the
  traversal never reached it and no gate ever ran;
- a gate rejected it, and which of the three matters;
- it is visible and the region places no mesh for it, so the frame has a hole
  where the game draws scenery.

`track-probe` reports all four, per instance, beside the source offsets, the
hashes, the transform and the materials. **It is the fifth acceptance
criterion of issue #42 and it is the reason this layer exists at all.**

## Provenance, in four parts

### Residency, with a reason

`TrackResidency` carries a rule; this layer carries which ARM of that rule
decided, as `TrackResidencyReason`:

| Reason | Rule | What decided |
| --- | --- | --- |
| `every-section` | `every_section` | the explicit fallback, which is not a retail rule |
| `sub-index-in-range` | `no_zone` | `number % 100` is below `kTrackNoZoneSubIndexLimit` |
| `sub-index-out-of-range` | `no_zone` | it is not, so the section is dropped |
| `always-resident-band` | `zone_list` | the number is in the 2600..2699 band speed.exe 0x00446473 adds unconditionally |
| `zone-list-entry` | `zone_list` | the zone's own list names it, at `zone_position` |
| `not-in-zone-list` | `zone_list` | it does not |

The boolean is not recomputed and then trusted: the probe derives the reason
itself and **compares its own resident/not-resident answer with the one in the
`TrackCullReport`**, raising an issue where the two differ. Where they differ
the culler's answer is the one carried forward, because the culler is what
produced the visible set being explained.

### The bounding tree

For every instance of a section culled with `TrackCullPolicy::retail_tree`,
the probe reports the leaf node whose entry names it, that node's depth with
the root at 0, and the code the traversal carried into it. When the traversal
never reached the instance, `pruned_by_node` names the ancestor - possibly the
naming node itself - that classified `outside` and stopped it.

That is a different fact from "its box is outside", and the probe reports both
(see the retail run below, where 19 of 39 instances are pruned at three
different nodes and 14 more are rejected by their own box).

### The three gates, and their measurements

`TrackGateVerdict` is what the culler actually did at each gate - `passed`,
`rejected` or `not-applied` - in the retail order of speed.exe 0x00445EC0:
the flags mask, then the box test the node may have made unnecessary, then the
screen-size level. A gate after a rejection is `not-applied`, because the
retail path returns.

Beside each verdict the probe prints the MEASUREMENT the gate is made of, and
those are taken for **every probed instance whether or not its gate ran**:

| Field | What it is | Measured when |
| --- | --- | --- |
| `mask_bits` | `(flags ^ kTrackInstanceMaskBias) & mask`, the 8-bit test at 0x00445EDA | always |
| `own_box_class` | the instance's own stored box against the frustum | always |
| `screen_level` | `track_screen_size_level` over the object's own radius plus `kTrackRadiusBias` | only with a scale; `screen_level_measured` says which |

That separation is the whole point. It lets the report make a sentence no
other output can:

```text
tree: policy retail-tree node 5 depth 2 carried outside pruned by 1
gates: mask not-applied bits 0x00000000 box not-applied tested inside
       own box outside size not-applied level unmeasured
```

- no gate ran, because node 1 pruned the subtree two levels up;
- and the instance's own box is outside anyway, so the tree lost nothing
  here. An `own box inside` on that same line would say the opposite, and
  that is a tree worth looking at.

The screen-size scale was **not recovered** from the executable
([`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md)). With no `--screen-scale` the
gate is off, the level is not measured, and the report says `unmeasured`
rather than printing a zero that would read like a computed answer.

### The answer, twice

`TrackInstanceProbe::visible` is what this layer concludes; `::culler_visible`
is what `cull_track_region` said. `probe_track_scene` compares them for every
instance - and, for a visible one, compares the node, the two box classes and
the screen level as well - and raises one issue per difference.

**A disagreement is not a failure.** The report is still produced, with the
count in `TrackProbeCost::disagreements`, because a caller needs both answers
to know which to distrust. `nfsu_probe track-probe` exits 3 when any of them
differ, which is the same status it uses for a region report that raised an
issue.

## The free camera

A car sits at the origin and is looked AT, so `OrbitCamera` - whose eye is a
function of its target - is the right model for it. A region is a place, and
the two questions a probe is asked about one, *what does the camera at the end
of this street see* and *why is that tower missing*, both need an eye and a
direction that move independently. So `TrackFreeCamera` is six degrees of
freedom and a lens:

```text
eye, target, up            in the renderer world frame of docs/FORMATS.md:286
fov_y_deg, near, far       the lens
width, height              the aspect, and the framing input. NOTHING IS
                           RENDERED: this command writes no file.
```

Every value is bounded by `nfsu::validate_camera`'s own constants - `kMinFov`,
`kMaxFov`, `kMinNear`, `kMaxNear`, `kMaxFar`, `kMaxTargetMagnitude`,
`kMinDistance`, `kMaxDistance` - rather than by a second policy, so a camera
this layer accepts is one `track_frustum_look_at` builds a frustum from.

### The default is the render branch's framing, reused

With no `--eye` the camera is `nfsu::frame_track_bounds` read out as an eye and
a target with `nfsu::orbit_eye`. It is **called**, not restated, and
`tests/track_probe_tests.cpp` pins the two against each other field by field.

The property that rule was chosen for is the one that matters here. The
distance is fitted to the drawn box's bounding SPHERE, so it does not depend on
the view direction, and two runs at different angles are comparable. The suite
asserts that at 30 yaw-and-pitch combinations, and in two strengths, because
the two are not the same claim:

- the FITTED distance, which is the rule, is bit-identical at every angle;
- the distance READ BACK out of the eye and the target is not quite. An eye is
  `target + distance * (cos p cos y, sin p, cos p sin y)`, and that triple has
  length 1 only in exact arithmetic, so recovering the distance through
  `det_sin_deg` and `det_cos_deg` costs a few ulp. Requiring bit identity
  there would be requiring a property the representation does not have. The
  suite allows 1e-9 on that half and says so.

**The bounds it frames are the region's own, not the camera's.** They are the
drawn vertices of every instance the scene placed, selected by no camera at
all. That is not a detail: the camera decides the visible set, so a default
framed on what a camera sees would be a camera defined in terms of itself, and
the comparability above would be gone. `track-render` frames on its draw list
because its draw list is the frame; this command cannot.

Those bounds come out of `nfsu::track_draw_list_bounds` over a throwaway draw
list holding every placed instance, so the box is measured by the same function
`track-render` frames on rather than by a second vertex walk here.

### The two moves, and their order

```text
--turn <yaw> <pitch>        rotate the VIEW about the eye. The eye does not
                            move and the eye-to-target distance is kept.
--fly <forward> <right> <up>  translate the eye AND the target along the
                            camera basis.
```

Positive yaw turns the view towards the camera's own right; positive pitch
raises it. Neither convention is retail's - retail is handed a frustum and
builds none - and both are this project's, stated here and pinned in the
suite.

`turn` is the move an orbit cannot make, and `fly` is dolly, truck and
pedestal in one. Between them and the absolute `--eye` / `--look-at` they
reach every camera.

**Turn runs before fly, and the order is observable.** `fly` is stated in the
camera basis, and after a turn that basis is the turned one, so "forward"
means along the direction the camera is now pointing. From the street camera
below, a quarter turn and then a 5-unit dolly lands at `x = -1`; the same two
moves the other way round land somewhere else, and the suite asserts that they
differ rather than asserting the order in a comment.

No libm transcendental is used: `det_sin_deg`, `det_cos_deg` and `det_sqrt` only,
the same restriction the renderer and the framing keep.

### What the defaults are

With no camera option at all: the framing above, at yaw 35 and pitch 30 with
a margin of 1.05 and a 35-degree vertical field of view over a 1280 by 720
frame, and the near and far planes the framing derives - `(distance - radius)
/ 2` and `(distance + radius) * 1.25`, each clamped into the renderer's own
range. Up is `0,1,0`.

With `--eye` and `--look-at` the framing does not run, so there is nothing to
derive a lens from, and the defaults are the ones `TrackFreeCamera` carries:
`--view-fov 35`, near 1, far 2000, up `0,1,0`, frame 1280 by 720. Every one
of them is printed on the `probe: camera` lines, so a run never leaves a
reader guessing which it used.

### One camera, not two

`nfsu_probe track-render` keeps a CULL camera and a RENDER camera apart
because they answer different questions. This command has one question, so it
has one camera: the same `TrackFreeCamera` becomes the `TrackCamera` the
frustum is built from, supplies the eye and the forward axis the screen-size
gate reads, and is the camera the report prints. A second one would let the
provenance describe a frustum the report never named.

## `nfsu_probe track-probe`

```text
build/nfsu_probe track-probe <STREAM.BUN> <geometry-offset> <geometry-size>
      <object-offset> <object-size>
      [--texture <file>]... [--lod-slot <0..5>]
      [--order <visible-instance|material-group>]
      [--policy <retail-tree|draw-everything|flat-instance-test>]
      [--residency <every|no-zone|zone>] [--zone <section-number>]...
      [--zones <TRACKB####.lzc> --zone-point <slot0> <slot1>]
      [--mask <0xNN>] [--screen-scale <s>]
      [--width <N>] [--height <N>] [--view-fov <deg>]
      [--view-yaw <deg>] [--view-pitch <deg>] [--view-margin <m>]
      [--eye <x> <y> <z>] [--look-at <x> <y> <z>] [--up <x> <y> <z>]
      [--view-near <n>] [--view-far <f>]
      [--turn <yaw> <pitch>] [--fly <forward> <right> <up>]
      [--section <n>] [--instance <n>]... [--mesh <NAME>]
      [--limit <count>]
```

Nothing in that list is new spelling. `--texture`, `--lod-slot` and `--order`
are `nfsu_probe track-scene`'s; `--policy`, `--residency`, `--zone`,
`--zones`, `--zone-point`, `--mask` and `--screen-scale` are
`nfsu_probe track-visibility`'s; and `--width`, `--height` and the `--view-*`
angles are `nfsu_probe track-render`'s. Each means what it means there, and
`track-scene`, `track-render` and `track-probe` print the same file, region,
texture, placement, cull and draw-list lines for the same inputs because those
lines are printed by two shared functions.

Three things differ from where they came from, and each one is a refusal
rather than a silent change: `--policy` defaults to `retail-tree` rather than
to the fallback, because this command always has a camera; `--zone` without
`--residency zone` is refused here and ignored by `track-visibility`; and
`--residency` beside `--zones` is refused here, where `track-visibility`
refuses only `--zone`.

**This command writes nothing at all**, which makes it the one track command
whose whole machine-readable contract is the exit status.
`tests/track_probe_guards.cmake` asserts the absence of a `wrote:` line on all
123 of its cases.

### Every selection is explicit and deterministic

That is the second acceptance criterion, and it is enforced rather than
described:

- **Every option is given at most once**, except `--texture`, `--zone` and
  `--instance`, which are ordered lists. A repeat is a usage error, so the
  answer can never depend on the order the command line was written in.
- **An unknown option is refused by name**, before anything else is decided.
- **An option that would be silently ignored is refused.** `--view-yaw`,
  `--view-pitch` and `--view-margin` describe the automatic framing, which
  `--eye` replaces; `--zone` without `--residency zone` would be read by
  nothing; `--mask` and `--screen-scale` are not consulted under
  `--policy draw-everything`; `--instance` without `--section` names nothing.
  Each of the four is a refusal with its own diagnostic.
- **A selection that matches nothing is refused, not answered empty.** A
  section index outside the region, an instance index outside the section, a
  repeated instance and a mesh name no prepared mesh carries are all errors,
  because an empty report cannot be told from a region that holds nothing.
- **The report is always in ascending (section, instance)**, whatever order a
  selection was written in.

### The two listings have no conditional trailer

Both the section listing and the instance listing print
`probe: listed <L> of <M>` unconditionally and then list `L` records. There is
no "... N more not listed" line.

That is deliberate and it is a lesson taken from
[`TRACK_RENDER.md`](TRACK_RENDER.md): a trailer that only appears for a
non-empty listing cannot be reached by any case that runs without input files,
so `tests/track_render_guards.cmake` documents it as the one line no automated
test pins. Printing the count either way removes the branch instead of
excusing it, and eight of this command's guard cases assert the line.

`--limit` bounds the instance listing and defaults to **4**, because an
instance record is fourteen lines plus two per material group and a region's
worth of them is not a thing anyone reads. `--limit 0` lists none and still
prints the count. The section listing is capped at 32 by a constant rather
than an option: a bounded region holds one or two sections, and the one
retail case measured here that holds more holds two.

## The retail region

Container `Tracks/STREAML1RB.BUN`, geometry at 23015540 size 178268, scenery
at 23011344 size 4168, textures from `Tracks/STREAML1RB_1210.BUN`. It is the
region [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) and
[`TRACK_RENDER.md`](TRACK_RENDER.md) measure, and its scene and draw-list
lines are the same ones those notes record.

### The default camera is the render branch's, to every printed digit

```text
probe: region bounds: x 436.150390625 .. 864.8084716796875
                      y -23.55398178100586 .. 136.62313175201416
                      z -208.3073980808258 .. -69.70453643798828 points 8481
probe: camera eye 1242.6750802612487,473.92267748693627,275.65389034164036
       target 650.4794311523438,56.53457498550415,-139.00596725940704
probe: camera fov_y 35 frame 1280x720 aspect 1.7777778
       near 10 far 1342.3060254589725
```

Those are [`TRACK_RENDER.md`](TRACK_RENDER.md)'s `render: eye` and framed
camera, digit for digit, reached through a different command and a different
type. The bounds are its drawn box over the same 8481 visited vertices.

### What the default camera sees, and what a street camera sees

The default frames the whole region, so nothing is culled at all. A street
camera at `450,20,-80` looking along `+x` with a 60-unit far plane is where
the provenance has something to say:

| | default | street |
| --- | ---: | ---: |
| instances probed | 39 | 39 |
| visible | 39 | 6 |
| rejected by mask | 0 | 0 |
| rejected by own box | 0 | 14 |
| rejected by screen size | 0 | 0 |
| pruned by a node | 0 | 19 |
| disagreements with the culler | 0 | 0 |

`6 + 14 + 19 = 39`, which is the accounting the probe exists to produce: the
19 pruned instances never reached a gate, and saying "14 were culled" about
this camera would be wrong by more than a factor of two.

The 19 are pruned by **three different nodes** of a 10-node tree that is 5
deep, and the naming node is not always the pruning one:

| named by node | at depth | carried | pruned by | instances |
| ---: | ---: | --- | ---: | ---: |
| 1 | 1 | outside | 1 | 4 |
| 2 | 1 | outside | 2 | 5 |
| 5 | 2 | outside | **1** | 5 |
| 6 | 3 | outside | 6 | 5 |

The third row is the carry: node 5's five instances are stopped by node 1 one
level above them, and a report that only said "outside" would not distinguish
that from node 5 being tested and failing - which it never was. The 14 that
survive the tree and lose their own box test reach nodes 3, 4, 7, 8 and 9.

**The tree loses nothing here, and that is measured rather than assumed.**
`--policy flat-instance-test` tests every instance box and prunes nothing, and
on this camera it rejects 33 by box - exactly the 19 + 14 the tree splits into
- and arrives at the **same six instances**, `0:6 0:7 0:30 0:33 0:34 0:38`, not
merely the same count. So the retail tree is conservative on this section: it
is an optimisation over the flat test rather than a second rule, which is the
thing a bounding tree has to be and the thing only a per-instance report can
check.

### The three residency arms, on retail data

The region above holds one section, so its residency is the same under every
rule and proves nothing about the reason. Two other regions of the same
container do better.

`3072 161176 80 177200` is the geometry root at 3072 with an object region
covering the two scenery sections at 80 and 165888, numbers **2608 and 2606**.
Both are letter `Z`, which is what `0x40 + number / 100` gives for the
2600..2699 band, and the three arms answer:

| `--residency` | both sections | why |
| --- | --- | --- |
| `every` (the default) | `every-section` | the explicit fallback |
| `no-zone` | `sub-index-in-range` | `2608 % 100 = 8` and `2606 % 100 = 6`, both below 10 |
| `zone`, with a list naming neither | `always-resident-band` | the band speed.exe 0x00446473 adds unconditionally |

226 instances, 38 visible, 188 pruned, and `disagreements 0` in all three.
The third is the interesting one: a zone list that names neither section
still leaves both resident, and only the reason says why.

The fourth arm needs the game's own zone data, and the region above supplies
it:

```text
--zones X:/Games/NFSU/Tracks/TRACKB1210.lzc --zone-point 550 80

zone: index 20 key 303 polygon 20 sections 22
section 0 number 303 letter C offset 23011344 valid yes
  residency: resident reason zone-list-entry zone position 13
```

`550, 80` is a point on the region's own road, in the raw slot order the
polygon test takes. The recovered test of [`TRACK_ZONES.md`](TRACK_ZONES.md)
picks zone 20; that zone's key is 303, which is the region's own section
number; and its 22-entry list names 303 at **position 13**. That is the game's
rule on the game's data, and the position is the part no other command prints.

### The same visible count is not the same visible set

Turning that street camera 20 degrees right and 5 down and flying 40 units
forward and 3 up gives `visible 6` and `rejected by box 14 pruned 19` again -
the same three numbers. **The sets are different**: `0:6 0:7 0:30 0:33 0:34
0:38` before and `0:6 0:7 0:11 0:12 0:34 0:38` after. It is a coincidence of
this region and this pair of cameras, and it is recorded here because a reader
comparing only the totals would conclude the moves did nothing.

### One instance, end to end

`--mesh TRSABC_ROAD_CHOP14 --screen-scale 120` selects the one instance that
places the road:

```text
instance 0:6 section number 303 letter C object 2
  offsets: section 23011344 instance 23011728 object 23013464
           mesh container 23162276 mesh record 23162304 hash record 23015848
  object: radius 656.7678 flags 0x00000010 flags2 0x00000000
          slots 0x97C329F1 0x00000000 0x00000000 0x00000000 0x00000000 0x97C329F1
  instance: flags 0x00000220 stored raw px 440 .. 651 pz 70 .. 92 py -23 .. -21
  transform: linear 1 0 0 0 1 0 0 0 1 translation 0,0,0
  stored world: x 440 .. 651 y -23 .. -21 z -92 .. -70 points 2
  drawn world: x 440.0032653808594 .. 650.0095825195312
               y -22.753185272216797 .. -21.275707244873047
               z -91.2575454711914 .. -70.25596618652344 points 534
  residency: resident reason every-section zone position none
  tree: policy retail-tree node 4 depth 2 carried inside pruned by none
  gates: mask passed bits 0x00000000 box not-applied tested inside
         own box inside size passed level 113
  visible: yes culler yes
  placement: mesh 16 'TRSABC_ROAD_CHOP14' hash 0x97C329F1 source mesh 16
             slot 0 requested 0 fallback no scene instance 1
  materials: groups 3 drawable 3 triangles 178 resolved 3 unresolved 0
             unusable 0 invalid slot 0
    group 58 material 0 record 23169168 first 0 count 12 slot 0
             hash 0x120CACCB binding resolved drawable yes
      texture 32 source 0 pack 0 record 27 key slot 0 flags 0x00000040
              record bounds min 600.99225,70.24618,-22.761677
                           max 608.01227,77.266266,-22.725903
```

Four things in that block are worth reading twice.

- **`box not-applied` with `carried inside`.** Node 4 tested inside, so the
  retail traversal skips the instance's own box test entirely, and the probe
  says so rather than reporting a test that did not happen. `own box inside`
  beside it is the probe's own measurement.
- **`drawn world y -22.753185272216797 .. -21.275707244873047` over 534
  points** is, to every digit, the row [`TRACK_RENDER.md`](TRACK_RENDER.md)
  records for this mesh in its ground-plane cross-check against the static
  collision of [`TRACK_COLLISION.md`](TRACK_COLLISION.md). Two commands, two
  measurement paths, the same number.
- **The stored box and the drawn box disagree**, by about a unit on every
  face, which is what the int16 rounding of the stored box predicts. The
  stored box is reported and never trusted;
  [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) measured it failing to bound its own
  mesh for 327 of 29674 retail instances.
- **`level 113` is not a retail answer.** The scale 120 is this caller's, the
  retail one was not recovered, and the level is the retail arithmetic over an
  invented input. The report prints the scale it used on its own line so that
  cannot be missed.

## Determinism

**Four commands, two separate processes each, three build configurations: all
twelve pairs byte-identical, and all four commands identical across the three
configurations.** Each output was deleted by exact name before its run, each
run was required to have written a non-empty file, and every exit status was
0.

| Command | SHA-256 of the standard output | bytes |
| --- | --- | ---: |
| the default framing, `--limit 2` | `B5C806B29CAAF5A931DAB2793D6EC81E4C5CE193FADEE256AC61236A13C07E0F` | 4916 |
| the street camera, `--limit 2` | `CFF94B4AAA5F5DFE2D4FCEBEE3688FBDC03056A746E830962FA51917D1A9E5E1` | 4523 |
| `--mesh TRSABC_ROAD_CHOP14 --screen-scale 120 --limit 1` | `D234ECD0CC96F3E5F15C85E841A2168510F8CD6655389545CC70F9F81E3F8713` | 5143 |
| the street camera turned and flown, `--limit 0` | `E666BC2F8D05B6DC42D6C25AFB582F20CBDFADC4B283BAE013202BEA5D3EBF08` | 3139 |

The fix for issue #125 reran all four commands in default, Debug, Release,
RelWithDebInfo and MinSizeRel. For each command, all five raw outputs are
byte-identical, and each also matches the pre-fix Release binary at the same
installation path. No numeric field or pin moved. The absolute source paths
are printed in the output, so their spelling is part of each SHA-256. Passing
the retail paths with forward slashes reproduced all four hashes above;
passing the same paths with backslashes changed the hashes but not the
reported values.

**Capture those in a shell that writes raw bytes.** An earlier round of
this table was measured through PowerShell's `>`, which in this environment
writes UTF-8 WITH A BYTE ORDER MARK: every hash was the hash of three bytes
the command never emitted, and every size was three too large. The four runs
were still identical to each other, so nothing about the determinism claim
was wrong - but no reader reproducing them from `bash` or `cmd` would have
matched a single row. It is the same decoder that makes the ASCII scan in
[`CLAUDE.md`](../CLAUDE.md) blind to a leading mark, and it is recorded here
for the same reason.

The three configurations are the default with no build type, an explicit
Release and an explicit Debug, which is the same set
[`TRACK_RENDER.md`](TRACK_RENDER.md) measures.

**The camera shares the renderer's square-root rule.** Issue #125 found that
MinGW's out-of-line x87 `sqrt` can double-round at `-O0` and `-Os`, while
optimized code uses a correctly rounded hardware square root. The probe
does not rasterize, but its camera distance and basis used to reach that
same library call. Both the probe and the renderer now use `det_sqrt`, so
the camera arithmetic takes the same single-rounded path across build
types. `-ffp-contract=off -fno-fast-math` still prevents FMA contraction;
those flags alone did not prevent the square-root divergence. The table
above is a measurement of the listed commands, and the issue #125 camera
case is pinned in `nfsu_track_probe_tests`.

The review's separate camera case, `--eye 450 20 -80 --look-at 599 62 -80
--view-far 60 --limit 2`, now prints a 4667-byte report, using forward-slash
source paths as above, with SHA-256
`BA2565408F95DB3E3178704A6A25FD5408749A1B954360D13BDE856EA4B9E479`
in each of those five build types on the same installation. Its camera
distance is `154.80633061990713`; before this fix, the default and Debug
builds printed `154.80633061990716` and 4669 bytes. Each run checked the
retail-file manifest before and after and found it unchanged.

Two numbers are printed as the FLOAT's own shortest round trip rather than the
widened double's, through `format_float`: every decoded field of a track
record is a float, and printing one through `format_double` would spell it as
its exact double value - `0.1F` reads `0.10000000149011612` - which is a true
number no reader can compare against the file.

## Tests

`tests/track_probe_tests.cpp` builds its fixtures as byte images at absolute
offsets and parses them with the real parsers, so the vertex field order, the
int16 instance matrix, the stored boxes and the alignment rules all apply as
they would in a retail file.

The first fixture is one section of seven instances over a three-node tree - a
root with two children - so that a camera can **prune a subtree** rather than
only reject a box. One instance names a mesh hash no mesh of the region
carries, so it places nothing; one carries flags a mask byte can single out;
one carries flags the mask BIAS decides, in the opposite direction from the
rest; and the two meshes between them give a resolved group and a group whose
texture hash no source claims.

Three things about it are deliberately not symmetric, because each one was a
hole before it was asymmetric:

- the stored boxes read `-1 .. 3` on slot 1, not `-2 .. 2`. The world map
  negates that slot and exchanges the ends, so a symmetric box is its own
  image and a conversion that forgot the negation would look right;
- the second fixture's section is **three levels deep** with a one-entry root
  and a five-entry leaf, so a prune root has to be CARRIED DOWN past the node
  it was found at, and both ends of a node's entry-count range are exercised;
- a second section exists at all, so the per-section instance table is read at
  a non-zero base. With one section every base is zero, and a table that
  always answered zero passed the whole suite.

**Every visibility assertion is double-entry.** Each case asserts the verdict
AND that `disagreements` is zero, so a probe that reproduced the traversal
wrongly fails whichever half it got wrong. Two cross-checks do the same for
numbers rather than verdicts:

- the per-instance drawn and stored boxes are asserted against
  `nfsu::track_draw_list_bounds` over a draw list of that one instance, over
  at least five placed instances, so the probe's vertex walk cannot become a
  second rule for what a frame draws;
- the screen-size levels are asserted against `track_screen_size_level` over
  the object record's own radius, so the gate's answers are derived rather
  than recorded. That case also proves it is measuring something: at a scale
  of 4 the three instances the frustum kept sit at the same distance, and the
  one that survives is the one whose object carries the larger radius.

The two routes to one answer are compared directly: `retail_tree` and
`flat_instance_test` reach the same visible set on the same camera - 19 pruned
plus 14 box-rejected on one side, 33 box-rejected on the other - which is what
makes the tree an optimisation rather than a second rule.

Three cases exist because the code they cover cannot be reached from a file at
all, and each says so where it stands. A section the parser REFUSED, built by
shrinking a node box below what it contains. A section whose containment fails
at cull time, so the culler degrades and the probe must follow the policy it
USED rather than the one it was asked for. And the probe's own tree walk,
every refusal of which is unreachable from a well-formed pair of reports -
because a tree the probe would refuse makes the culler degrade first, and then
no tree is walked. That last one is reachable the way a caller reaches it,
`probe_track_scene` being public and taking two reports it did not produce, so
six damaged trees plus the pending-node limit and its boundary drive it and
the report has to answer rather than read past the end of a vector.

`tests/track_probe_guards.cmake` is one CTest entry driving **123 cases**, each
asserting the exact exit status and the diagnostic and each banning any
`wrote:` line. It exists for the reason `tests/track_render_guards.cmake`
exists: an independent mutation pass over one command's block of
`src/main.cpp` scored 7 caught out of 34 because nothing in the repository ran
the command's body. A CMake script rather than more `add_test` entries because
`PASS_REGULAR_EXPRESSION` makes CTest ignore the exit status, and here the
status carries the whole machine-readable contract: 1 for a usage error, 2 for
a pipeline or selection failure, 3 for a run whose reports failed.

**80 of the cases need no input file, and 43 need one**, and the split is the
one thing this script does that the render guards could not. A command driven
with no input reaches every refusal and the whole body of a run that reads
nothing - and reaches no line that only prints when a region holds an
instance. The listing limit, the per-instance block and every total that is
non-zero only over real data were therefore covered by a manual acceptance and
by no gate at all, which `TRACK_RENDER.md` records as the gap it could not
close.

The gate closes it by asking `nfsu_track_probe_tests` for the fixture it
already builds in memory: `--write-fixture <path>` lays the same bytes down as
a file, at the same absolute offsets its own cases parse, and prints the two
regions it wrote so the script reads them back rather than repeating them.
Nothing is checked in that mode and nothing is compared; it is the unit
tests' fixture, used twice.

Two properties of that are deliberate. The binary writes **only where it is
told** - there is no default path and no fixed one, and issue #124 is about
test binaries that write to a fixed path under the system temp directory,
where two concurrent runs collide; this one is handed a path inside the
caller's own build directory. And the 43 cases pin **exact strings** from that
fixture - offsets, a transform, a material record - which couples them to it
on purpose: a fixture change that moves an offset has to be acknowledged here,
and a change to what the command prints cannot pass unnoticed.

### Mutations

**A mutation score describes a SET, not a suite.** The numbers below are what
they are and no single one of them is the coverage of this code. Every mutant
was applied one at a time, rebuilt, and the suite re-run, and a run is scored
by what it PRINTED: a non-zero exit with neither a failure line nor the
suite's own summary is an UNKNOWN result, not a kill, which is the rule
[`CLAUDE.md`](../CLAUDE.md) gained while issue #41 was in review.

Five sets were run. Two of them are the ones to read: the first row, which
is a pass the author wrote against code the author had just written, and
the second, which is a pass a subagent wrote without seeing the suite. The
other three are re-runs of survivors, which say whether particular holes
closed and nothing at all about the rest of the code.

| Set | Written | Applied | Caught | Survived | Unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| The worker's own set, at the branch's first head | 140 | 139 | 105 | 33 | 1 |
| **An independent pass by a subagent that had not seen the suite**, at the same head | **450** | **449** | **272** | **162** | **15** |
| the independent pass, over `src/track_probe.cpp` alone | 300 | 300 | 202 | 83 | 15 |
| the independent pass, over the `track-probe` block of `src/main.cpp` | 150 | 149 | 70 | 79 | 0 |
| The worker's survivors, re-run at the final head | 34 | 34 | 29 | 5 | 0 |
| The independent pass's survivors, re-run at the final head | 82 | 82 | 40 | 42 | 0 |

**The second row is the number to quote.** It was written by a subagent
given the header, the source and the rule it reproduces, and told not to
open `tests/track_probe_tests.cpp` or `tests/track_probe_guards.cmake` until
its list was fixed - because a set written against the tests measures the
set and not the suite. Its 79 command survivors are the honest measure of
what a gate with no input files cannot reach, and they are the reason the
43 data-driven cases exist.

The last row is a re-run and not a fresh pass, so read it as "did these
particular holes close" and as nothing else. Four more cases were written
AFTER that sweep, each against one of its 42 survivors and each verified by
applying the same mutation by hand and watching the guard fail by name: the
default instance limit (`C075`), the section line's letter arm (`C078`) and
its tree-walked arm (`C080`), and one case that reads the two output streams
apart rather than joined. That leaves **39 of the 82 surviving**, and the
three IDs above are counted as caught nowhere in the table, because they
were not caught in the sweep the table reports.

The two passes ran concurrently in separate worktrees, each restricted to
the seven CTest entries a change to those two files can reach. That is not
only speed: the other entries of this branch's base write to fixed paths
under the system temp directory, which issue #124 fixed on `main` after this
branch was cut, and two overlapping runs of them can manufacture a false
CAUGHT. None of the seven does.

#### What the worker's own pass found

The survivors of the first row were not spread evenly, and most of them came
from properties of the FIXTURE rather than from gaps in the assertions. That
is the useful finding, and it is the one worth carrying to the next suite: a
test can assert exactly the right thing about data that cannot tell two
answers apart.

- **The fixture was symmetric where the code is not.** Every instance box
  read `-2 .. 2` on slot 1, which is its own image under the world map's
  negation, so a conversion that dropped the negation looked right. Every
  instance carried flags whose low byte the mask bias leaves alone, so
  dropping the bias changed no answer.
- **The tree was too shallow and too uniform.** One level, so a prune root
  never had to be carried past the node it was found at; no node with one
  entry or five, which are the ends of the range; and no node that ever
  tested INSIDE, so the box test the retail traversal skips was never
  skipped.
- **Six boundaries were tested away from the boundary.** A child index, an
  instance index and a selected instance index were all driven far past the
  end rather than one past it. The instance limit was driven well below the
  count, where `>=` and `>` behave alike, rather than at `count - 1`, where
  they do not. The turn bound was driven positive only. An invalid camera was
  only ever handed a move, where the result's own validation refuses it under
  another name.
- **One region held one section**, so the per-section instance table was only
  ever read at base zero. A table that always answered zero passed the whole
  suite.

Four survivors were not fixture defects but untested paths, and each needed a
case that could not come from a file at all: a section the parser refused, a
section whose containment failed at cull time, the probe's own tree walk, and
a framed camera whose EYE leaves the range its target stayed inside.

One survivor was a test that asserted the right invariant over the wrong
input. The screen-size gate normalizes its forward axis, and the first case
compared a unit axis against a 37.5-times-longer one from a point in FRONT of
the screen eye - where the dot product is positive and any positive multiple
of it clears the comparison. The comparison only bites just behind the eye,
inside the radius, and only when no slot of the forward or of the difference
is zero; otherwise a normalization dropped on one lane is invisible. The case
now stands one unit past the instances on every axis at once, with the raw
diagonal `(1, 1, 1)`, and each of the three lanes is caught on its own.

#### What the independent pass found

The subagent's pass is the one that changed this suite, and the shape of its
result is worth more than its score. **It caught 202 of 300 over
`src/track_probe.cpp` and 70 of 149 over the `track-probe` block of
`src/main.cpp`** - 67 per cent against the library and 47 per cent against the
command, measured at `dc06f51`, where the suite held 18 unit tests for the
library and 61 command cases. The library was better covered by the smaller
half of the suite, and that inversion has one cause.

**Nothing ran the command's printer over a region that held anything.** All
61 of the cases the branch started with drove the command with no input
files at all, because a case that needs input needs a file the repository
may not ship. Every one of them therefore stopped at a refusal or at a run
that printed totals of zero, and **79 of the 149 command mutants survived**,
including mutations to the per-instance block, the listing limit, the region
bounds, the gate totals and every line that prints a number derived from real
geometry. This is the same hole [`TRACK_RENDER.md`](TRACK_RENDER.md) records
for issue #41 at one command's block, found again at another, and it is why
`tests/track_probe_tests.cpp` gained a `--write-fixture` mode: the unit tests
already build a byte-image the real parsers accept, so the gate now asks them
to write it and drives the command over it. That is the whole of the 43
data-driven cases, and re-running those 79 survivors against them is the
fourth and fifth rows of the table above.

Four classes of hole were general enough to state, and each is a rule rather
than a repair:

- **A bound tested only from outside cannot tell `<` from `<=`.** Every parse
  bound had a case one past it and none had a case AT it, so a mutation that
  moved `>` to `>=` changed nothing the suite ran: `--mask`, `--zone`,
  `--height`, `--view-fov` and the shared `take_ranged` helper all survived
  that way, as did the library's move bounds and the camera validator's
  position range. Fourteen command cases and six library assertions now drive
  each of those bounds at its own limit.
- **An enumeration tested only with a wrong word cannot tell one right word
  from another.** `--order`, `--policy` and `--residency` each had a case for
  an unrecognised value and none that confirmed a recognised one reached the
  stage that uses it, so a branch that fell through to the default looked
  correct for every input the suite offered.
- **A printer whose two operands are equal in the fixture is a printer with
  one operand.** Instance `0:0` has the same section index and instance index,
  its mesh's groups begin at zero, it is pruned by the node that names it, and
  its own box class equals the class it was tested as. Eight mutants exchanged
  one of those pairs for the other and printed a line identical to the
  pristine one. The data cases now select instances where each pair is two
  different numbers.
- **A guard that asserts a diagnostic proves the refusal, not the value.** The
  library's own survivors clustered on paths where the fixture could not
  distinguish two answers at all: a section with a one-node tree, an object
  with a non-zero last mesh slot, an instance the selection drops, and a
  non-resident section under the retail rule. Each needed a new fixture shape
  rather than a new assertion.

Two of its survivors were code defects rather than test gaps, and both are
fixed on this branch:

1. **An issue raised about an instance the selection then dropped still
   reached the report.** `probe_track_scene` appended to `issues` while
   walking, before the mesh filter decided whether the instance was in the
   listing at all, so `--mesh NAME` could exit 3 over an instance its own
   output never showed. Per-instance issues are now held in a pending vector
   and flushed only once the filter keeps the instance;
   `test_a_dropped_instance_raises_nothing` covers both halves.
2. **The camera's up vector was bounded for finiteness but not for
   magnitude.** `normalize` squares each component, so an up of `1e200` -
   finite, and accepted by the validator - overflowed inside the basis and was
   refused under a diagnostic that named the wrong thing. `validate_track_free_camera`
   now bounds each component of `up` by `kMaxTargetMagnitude`, which the
   accepted-side assertion drives at its own limit.

Its 15 UNKNOWN results were all one shape: a mutation whose behaviour is
undefined - a dropped null check, an index past the end of a vector - where
the process dies without printing a failure line. Under the rule
[`CLAUDE.md`](../CLAUDE.md) states they are scored UNKNOWN and not as kills,
and no assertion can close them, because no assertion can distinguish "the
check refused" from "the process died".

#### What still survives, and why

Five mutants survive the worker's set at the final head, and each is stated
rather than hidden.

**Three are provably equivalent.**

1. `incoming == TrackBoxClass::intersecting` to `!= outside` in
   `resolve_tree_codes`, which re-tests a node its parent had already
   admitted. `src/track_objects.cpp` refuses a node whose box does not
   contain what is below it, so a child of a node that tested `inside` is
   inside as well and the second test returns the same code.
2. `!(length > 0.0)` to `>= 0.0` in `normalize`. A zero vector divides to a
   NaN, and the `finite_vec` check on the result refuses it anyway.
3. Dropping the `track_camera_basis` call in the command after
   `track_move_free_camera`. That function computes a basis at entry and
   again after a turn, so on a camera it returned there is always one.

**Six were command-layer statements that needed a region to observe.** Four
of them are now caught, by the guard cases that drive the command over the
fixture the unit tests write: the listing limit, the region bounds, the cull
options reaching the probe, and the gate totals with a non-zero count in them.
That is the whole reason those cases exist.

Two of the six are not caught, and neither is a test that could be written:

| Mutation | Why no input distinguishes it |
| --- | --- |
| the screen-size eye is not mapped to the raw slots | every instance of the fixture sits at world y and z of zero, so the mapped eye and the unmapped one are the same distance from all of them and the level does not move. The retail acceptance covers it, but not through the level: `level 113` under "One instance, end to end" does NOT move either, because that instance also sits at world `0,0,0` and the raw map is a signed axis permutation, so its distance from the eye is invariant under the mapping going wrong. What does move is the `cull: ... visible` and `draw list: ... visible` counts above the instance block, from 36 to 38 - measured directly, by copying the eye in world order in place of the mapping. |
| the totals print a literal zero for `disagreements` | on code that is right the count IS zero however it is computed. It is a tripwire whose non-zero case is unreachable by construction, which is the same shape as the dead statistics [`TRACK_RENDER.md`](TRACK_RENDER.md) records. |

The first of those two is closeable, and the cost is what left it open: one
instance of the fixture placed off the `y = z = 0` plane would do it, and
every pinned string of the data cases is derived from that fixture's
geometry. It is recorded here as a known hole with a known fix rather than
argued away.

**One result is UNKNOWN rather than caught or survived**, under the rule
[`CLAUDE.md`](../CLAUDE.md) states. Removing the null check on the cull
report's per-section summary dereferences a null pointer, and
`nfsu_track_probe_tests` dies with a segfault rather than a failure line.
The mutant's behaviour is undefined by construction, so no assertion can
tell "the check refused" from "the process died"; what the suite CAN do, and
does, is require the pristine refusal's own diagnostic.

#### What survives of the independent pass, and why

38 of that pass's 450 mutants are alive at the final head. They are listed
here by ID so a later session can re-run exactly these rather than guess,
and grouped by the reason each is alive. **The grouping is a reading of each
mutant, not a proof of each**: only the first group claims equivalence, and
the claim is stated so it can be contradicted.

**No observable difference (14).** `L015`, `L032`, `L033`, `L051`, `L058`,
`L103`, `L132`, `L146`, `L213`, `C022`, `C027`, `C041`, `C050`, `C069`. Two
shapes: a value recomputed where an earlier computation already fixed it
(`L103`, `L132` and `C050` all re-check a camera the callee validates
again), and a pair of fields that are equal on correct code, so exchanging
them prints the same bytes (`C022` and `C069` exchange the probe's answer
with the culler's, which differ only when `disagreements` is non-zero, and
`disagreements` is zero whenever the probe is right).

**A bound whose other side is unreachable from outside (15).** `L011`,
`L012`, `L014`, `L075`, `L097`, `L173`, `L174`, `L179`, `L214`, `L258`,
`L259`, `L266`, `L279`, `L280`, `L281`. Each relaxes a `>=` to a `>` or
drops a guard on an index the parser has already bounded, so reaching the
changed comparison needs a report the parsers cannot produce. They are
reachable only by hand-building the two reports `probe_track_scene` takes,
which is how the six damaged-tree cases already reach the tree walk; each of
these would need its own such construction and none has one yet. That is the
largest closeable block left and it is stated as work, not as equivalence.

**The fixture cannot tell the two answers apart (9).** `L182` and `L183`
are the normalized forward axis, alive for exactly the reason the
screen-size eye mapping is: every instance of the fixture sits at world
`y = z = 0`. `L265` and `L276` need a mesh record the fixture does not
hold. `C037` and `C042` need a region whose bounds cannot be framed.
`C074` is the 32-section listing limit, which needs a 32-section fixture.
`C083` moves the probe's OWN issues to stdout, and the fixture raises none -
the new stream case pins the parser's diagnostic instead, which is the same
property one stage earlier. `C147` drops a line from the usage text that
`nfsu_probe_usage_commands` does not require.

`C132` was listed here once, for accepting an empty `--mesh` name on the
claim that an empty name selects nothing and prints an empty listing either
way. That reason was never true - an empty name is not "no filter", and the
pristine binary refuses it with `--mesh needs a prepared mesh name` exit 1
the same as no name at all - and the real reason it survived was a gate
blind spot: `tests/track_probe_guards.cmake`'s own "--mesh with an empty
name" case built its command line through `nfsu_case`'s unquoted `${ARGN}`
forwarding, which drops an empty list element, so the case silently handed
the probe `--mesh` alone and duplicated the case above it rather than
testing an empty value at all. The case now reaches the child process
directly with the empty argument as a literal, `C132` exits 2 instead of 1
under the mutant where the pristine binary exits 1, and it is CAUGHT. See
"Mutation record: review of PR #138" below.

One fixture change - a single instance placed off the `y = z = 0` plane -
would close `L182`, `L183` and the screen-size eye mapping together. It is
not made here because every pinned string of the 43 data cases is derived
from that fixture's geometry, so the change is a rewrite of the data half of
the gate rather than an edit. It is recorded as a known hole with a known
fix.

#### Mutation record: review of PR #138

A third, independent pass, for the review of pull request #138 (Gitea issue
comment 2928), applied 15 mutants one at a time against the branch at
`95a899d`, rebuilt, and re-ran the full suite plus the four pinned retail
cases. It scored **5 caught, 10 survived, 0 unknown**.

| ID | Location | `ctest` | Pinned retail |
| --- | --- | --- | --- |
| `L-sortfirst` | `src/track_probe.cpp:723`, drop the selection's `std::sort` (control) | CAUGHT | same |
| `L-notree` | `src/track_probe.cpp:1062`, `visible = false` when the tree cannot be walked | CAUGHT | same |
| `L-unplacedkey` | `src/track_probe.cpp:1203`, the unplaced lookup ignores the section | CAUGHT | same |
| `L-sizebound` | `src/track_probe.cpp:1048`, `<` becomes `<=` | CAUGHT | same |
| `L-flyafterturn` | `src/track_probe.cpp:561`, fly uses the pre-turn basis | CAUGHT | D differs |
| `C-eye` | `src/main.cpp:2633`, screen eye copied in world order | SURVIVED | C differs (`cull: ... visible` 36 -> 38) |
| `C-fwd` | `src/main.cpp:2637`, screen forward copied in world order | SURVIVED | same |
| `C-exit0` | `src/main.cpp:2781`, exits 3 even when everything is ok | SURVIVED | all four exit 3 |
| `C-exitok` | `src/main.cpp:2781`, exit status ignores `report.ok()` | SURVIVED | same (equivalent in practice) |
| `C-zonenone` | `src/main.cpp:2557`, the no-polygon fallback uses `every`, not `no-zone` | SURVIVED | same |
| `C-meshempty` | `src/main.cpp:4653`, drop `value.empty()` for `--mesh` | SURVIVED | same |
| `L-levelcmp` | `src/track_probe.cpp:1082`, drop the screen-level comparison arm | SURVIVED | same |
| `L-residissue` | `src/track_probe.cpp:866-873`, drop the residency issue and its count | SURVIVED | same |
| `L-pruneroot` | `src/track_probe.cpp:245`, `prune_root[index] = up` | SURVIVED | same |
| `L-bandorder` | `src/track_probe.cpp:283-293`, the zone list checked before the 26xx band | SURVIVED | same |

**Four of the ten survivors are fixed on this branch and are now CAUGHT:**

- `C-meshempty` - `tests/track_probe_guards.cmake`'s "--mesh with an empty
  name" case now reaches the child process with a literal empty argument
  instead of losing it to `${ARGN}`'s unquoted expansion, and the mutant
  exits 2 where the pristine binary exits 1. See finding 3 above and `C132`
  in "What survives of the independent pass, and why", which was the same
  mutation under the older pass's numbering.
- `L-residissue` - `tests/track_probe_tests.cpp`'s
  `test_disagreement_is_reported` now asserts the exact disagreement count
  (8: the section-level residency issue plus the seven instance-level ones
  it cascades into) and the section-level issue's own text, rather than
  `>= 1`, which the seven cascading issues alone satisfied. See finding 4.
- `L-levelcmp` - the same test gained a fifth tampered case, over its own
  cull (screen size on, a generous scale, so nothing else disagrees), that
  flips a visible instance's recorded `screen_level` and asserts the
  resulting disagreement.
- `L-bandorder` - `test_residency_reasons` in `tests/track_probe_tests.cpp`
  gained a case with section `2650` (inside the always-resident band) also
  named at index 0 of its own zone list; the pristine binary reports
  `always-resident-band` with no zone position, and the mutant reports
  `zone-list-entry` at position 0.

Each of the four was verified failing on the unfixed code and passing on the
fixed code, and again by re-applying the mutation itself and watching the new
case fail by name before being reverted.

**Six survivors remain alive**, all for reasons the reviewer already
recorded and none required by this fix round:

- `C-eye` and `C-exit0` are caught by the pinned retail acceptance, same as
  the reviewer found; nothing in `ctest` catches either.
- `C-fwd` survives because no case has a camera off the world x axis with an
  in-frustum instance above or below eye height, which is where a forward
  axis left in world order wrongly size-rejects.
- `C-exitok` is equivalent in practice: no probe issue is reachable on data
  that parses, so nothing distinguishes "ignores `report.ok()`" from
  "reads it".
- `C-zonenone` survives because the `--zones` no-polygon fallback
  (`src/main.cpp:2555-2558`) is never exercised by any case; a manual retail
  check confirmed the pristine behaviour but no case pins it.
- `L-pruneroot` needs an instance two or more levels below the node that
  prunes it, so that `prune_root[up]` (the carried root) and `up` (the
  immediate parent) differ. Both the shared fixture's tree and the retail
  region's are one level deep (see "What the worker's own pass found" and
  the retail provenance notes), so no existing data distinguishes the two,
  and building a new, deeper tree fixture was judged bigger than the small
  test this fix round's optional coverage was scoped to. It is recorded here
  as a known hole with a known fix, the same as the screen-size eye mapping
  above.

## Read-only retail acceptance

Every command below only reads the installation, and this one writes nothing
at all, so there is no derived file to withhold from the repository.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --limit 2

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --eye 450 20 -80 --look-at 900 20 -80 --view-far 60 --limit 2

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --mesh TRSABC_ROAD_CHOP14 --screen-scale 120 --limit 1

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --eye 450 20 -80 --look-at 900 20 -80 --view-far 60 \
    --turn 20 -5 --fly 40 0 3 --limit 0

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --zones X:/Games/NFSU/Tracks/TRACKB1210.lzc --zone-point 550 80 --limit 0

build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    3072 161176 80 177200 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --residency no-zone --limit 0
```

Expected:

1. Both `ctest` runs report `100% tests passed, 0 tests failed`.
2. All six commands exit 0 and write nothing to standard error.
3. The first prints `probe: instances 39 probed 39 visible 39 culler 39
   disagreements 0`, `probe: rejected by mask 0 box 0 size 0 pruned 0` and
   `probe: placed 18 unplaced 21 groups 46 triangles 2827`, and its framed
   camera is the one under "The retail region" above. It also prints
   `probe: region bounds: x 436.150390625 ..` and, because it was given
   `--limit 2`, `probe: listed 2 of 39 instances`. Those two lines are named
   here rather than left implied: they are what two of the command-layer
   survivors above change, and this is the only place either is checked.
4. The second prints `probe: instances 39 probed 39 visible 6 culler 6
   disagreements 0` and `probe: rejected by mask 0 box 14 size 0 pruned 19`.
5. The third prints `cull: policy retail-tree held yes resident 1 degraded 0
   visible 36` and `draw list: order visible instance visible 36 placed 17
   unplaced 19 without groups 0` before its `probe:` lines, then `probe:
   instances 39 probed 1 visible 1 culler 1 disagreements 0` and the
   instance block under "One instance, end to end", whose gate line reads
   `size passed level 113`. The `visible 36` on the two lines above - not
   the level - is what a command-layer survivor changes: it is the only
   check that the screen-size eye reaches the gate in the raw slot order
   rather than the world one. `level 113` does NOT move under that survivor:
   this instance sits at world `0,0,0`, and the raw map is a signed axis
   permutation, so its distance from the eye is the same either way. A
   reader checking this line by `level 113` alone would see no difference;
   `visible 36` moving to `38` is the difference that exists.
6. The fourth prints the same three counts as the second over a different
   visible set; see "The same visible count is not the same visible set".
7. Every one of the six prints `probe: listed <L> of <M> sections` and
   `probe: listed <L> of <M> instances`, at every limit including zero.
8. The fifth exits 0 and prints `zone: index 20 key 303 polygon 20 sections
   22` and `residency: resident reason zone-list-entry zone position 13`.
9. The sixth exits 0 and prints `probe: sections 2 valid 2 resident 2`,
   `probe: instances 226 probed 226 visible 38 culler 38 disagreements 0`
   and, for both of its sections, `reason sub-index-in-range`. Replacing
   `--residency no-zone` with `--residency zone --zone 303` gives
   `reason always-resident-band` and the same 38 visible.
10. Running any of the first four twice in separate processes gives
    byte-identical output, and so does running them from `build-release` or
    from a Debug build; the four hashes are in the Determinism table above.
    Capture them in a shell that redirects raw bytes - `bash` or `cmd`, not
    PowerShell, whose `>` adds a byte order mark here and shifts every hash
    and every size by three.
11. The 476 files of the installation are unchanged in path, size and
    modification time before and after.

## What this layer does not claim

- **It is not a second culler.** It reproduces the traversal in order to
  explain it, and it takes the residency answer, the policy actually used and
  any degradation from the `TrackCullReport` rather than deciding them again.
- **It is not the retail rule for what the game draws.** Every caveat
  [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) states still holds: which mesh
  slot an instance draws at a given distance is undecoded, the projection
  scale behind the screen-size gate was not recovered, and which sections are
  resident together is upstream of everything here.
- **`--up` changes the answer.** The reference up vector is part of the
  frustum, so a probe run with a rolled camera reports a different visible set
  from one without. That is correct and it is worth stating, because the
  default is `0,1,0` and nothing in the report shouts when it is not.
- **The `--zones` arm is only as good as the zone data.** It replaces the
  residency with the zone the recovered polygon test picks for a point, which
  is the game's own rule; what puts a section in the streaming table in the
  first place is still unknown.
