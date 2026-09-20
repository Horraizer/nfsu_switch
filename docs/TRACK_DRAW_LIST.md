# The prepared track scene and its draw list

[`TRACK.md`](TRACK.md) decodes the geometry a bounded track region draws.
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry goes.
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) decodes which of it a camera
sees. This note decodes nothing. It is the seam between those three and a
render backend: one owned scene, one per-frame draw list, and an explicit
statement of what the pair does and does not promise.

Header: [`include/nfsu/track_draw_list.h`](../include/nfsu/track_draw_list.h).
Implementation: [`src/track_draw_list.cpp`](../src/track_draw_list.cpp).
Tests: [`tests/track_draw_list_tests.cpp`](../tests/track_draw_list_tests.cpp).

## Where the boundary is

Issue #40's second acceptance criterion is that visibility selection stays
outside backend-specific code. That is the load-bearing one, and it is
settled here rather than left to a convention a backend could quietly break:

- a backend reads a `PreparedTrackScene` and a `TrackDrawList`;
- it never sees a `TrackFrustum`, a `TrackCullReport`, a bounding tree, a
  section number or a residency rule;
- `build_track_draw_list` turns a visible set into ordered (instance, group)
  pairs above every backend, so nothing below it has to know how culling
  works to draw the result.

The culler is therefore a producer of input to this layer, not something
this layer calls. The primary entry point takes a plain
`std::vector<TrackVisibleInstance>` and reads only the `section_index` and
`instance_index` of each entry, so a caller that selects visible instances
some other way uses it unchanged. The second overload takes a
`TrackCullReport` and, in addition, copies four provenance fields into the
list so that a caller holding only a draw list can still see what produced
it.

### Everything in the scene, a subset per frame

Two arrangements were possible, and the choice is not free.

**The one taken.** The scene holds the whole region and a draw list names a
subset of it. Preparation happens once; building a draw list touches no
vertex, index or texture byte, only handles.

**The one rejected.** A prepared scene per visible set. A prepared region
owns a copy of every vertex, every index and every decoded texture of the
region - 142440 vertex bytes, 22350 index bytes and 3006464 texture bytes
for the retail region this note measures below - and a visible set changes
every frame. Rebuilding that per frame would copy megabytes to draw a few
hundred instances.

**What the choice costs.** The mirror of the same fact: a scene occupies
memory proportional to the region rather than to the frame. A backend
uploads the region once and then draws whatever each list names; nothing
here streams and nothing here evicts. For a region whose visible set is
usually a small fraction of its contents, that is memory held for geometry
no frame draws. `TrackScenePlacementCost::resident_entries` and
`resident_triangles` are the ceiling a backend sizes its per-frame buffers
against, and the region cost statistics of [`TRACK.md`](TRACK.md) are the
upload it must budget for.

## Which car-scene conventions carry over

Issue #40 says to reuse the scene ownership and provenance patterns of
[`CAR_SCENE.md`](CAR_SCENE.md) and [`RENDERER.md`](RENDERER.md) "where
appropriate". These carry:

- **Ownership.** The scene owns every byte a backend reads. The
  `TrackRegionReport`, the `TrackObjectReport` and the `RenderTextureSet` it
  was built from may change or be destroyed afterwards.
- **Provenance as opaque numbers.** Section index, section number, source
  instance index, object index, mesh slot and mesh hash are carried and
  never interpreted here, exactly as texture source, pack and record are
  carried by `PreparedCarScene`.
- **One validator the builder runs itself.** `validate_prepared_track_scene`
  and `validate_track_draw_list` check every invariant a backend relies on,
  and the two build functions run them before returning true.
- **Deterministic cost statistics.** Every count is derived from the inputs
  in source order, so two runs over the same reports give the same numbers.
  The retail differential below measures that across separate processes.
- **`kNoHandle` for an absent handle**, and first-use ordering for textures.

These do not:

- **`SelectionProfile` and `MeshSelector`.** A car scene names the meshes it
  wants. A track region has nothing to select: every valid mesh is prepared,
  and the *instances* decide what is drawn. There is no selector list here
  and no profile enum.
- **`PreparedInstance::transform`, a row-major 4x4.** A scenery instance
  stores nine int16 and three floats, and `track_instance_transform` only
  divides the nine by `kSceneryMatrixOne`. `PreparedTrackInstance` carries
  that `TrackTransform` as it stands; a 4x4 would add a row no record holds.
- **`PreparedMeshInstances::copies`, and its rule that a scene carrying no
  instance draws each mesh once.** That rule exists because a car mesh with
  no placement is drawn in the car's own space. A track mesh with no
  instance has no place in the world at all and is drawn zero times.
  `TrackScenePlacementCost::undrawn_meshes` counts those, and a test pins
  that no draw list entry ever names one. This is the sharpest of the
  differences: a layer that copied the car rule would draw unplaced scenery
  at the world origin.
- **`prepared_draw_order(retail_order)` and `render_draw_layer`.** The
  retail draw queue is decoded for the car material path in
  [`MATERIALS.md`](MATERIALS.md). Nothing this project has read proves the
  track path uses the same queue, so neither order here claims to be it, and
  a backend that needs a transparency order builds one.
- **`prepared_triangle_ranges` and triangle ids.** Those exist to let the
  CPU reference renderer map a pixel back to a material group.
  `src/render.cpp` consumes a `PreparedCarScene`; nothing renders a track
  draw list yet, and an id scheme with no consumer would be a claim with no
  test behind it.
- **Preparation once, drawn whole.** A car scene is prepared and then drawn.
  A track scene is prepared once and *selected from* every frame, so the
  per-frame product is the draw list rather than the scene. That is the
  structural difference the two lists above follow from.

## Placement

`build_track_scene` prepares the region with `prepare_track_region` and then
walks every valid section, and every instance of it, in source order.

### Which mesh an instance draws

An object record names up to six mesh hashes
([`TRACK_OBJECTS.md`](TRACK_OBJECTS.md)). `TrackSceneOptions::lod_slot`
names the one this layer places, and defaults to 0, which is set in all 9681
retail objects.

**This is a documented choice, not the retail rule.** Which slot the game
draws at a given distance is not decoded: the per-slot thresholds at
`0x00034102 +24` and the projection scale the screen-size level needs were
not recovered. The header says so where a caller reads it.

Three rules are applied, and only the first two are recovered:

1. **The slots 4 and 5 fallback.** speed.exe 0x00445A81 and 0x00445A8F: when
   slot 5 or slot 4 resolved nothing, the loader fills it with the first
   solid that slots 0 to 3 resolved. This layer reproduces that exactly,
   for those two slots and no others, walking slots 0, 1, 2, 3 in order and
   stopping at the first that resolves. A slot from 1 to 3 that is empty
   places nothing, because no fallback for it was recovered.
   `PreparedTrackInstance::slot_fallback_used` records that it fired and
   `slot` records where it landed, while `requested_slot` keeps what the
   caller asked for.
2. **The two skipped hashes.** speed.exe 0x0044590B and 0x00445917 compare
   against `kSkydomeMeshHash` and `kReservedMeshHash` and skip the lookup,
   so neither ever resolves a solid and neither ever places an instance
   here. `SKYDOME` is in slot 0 of five retail objects, so this is not a
   hypothetical branch.
3. **The lowest mesh claiming a hash.** A hash binds to the lowest-numbered
   valid mesh of the prepared region that carries it. That is not a retail
   rule; it is the rule `resolve_track_objects` already uses, adopted so the
   two layers never disagree about an instance, and a test compares the two
   on a region holding two meshes of the same hash.

### What placing nothing means

An instance of a valid section that places nothing is recorded in
`PreparedTrackScene::unplaced` with one of three reasons - the slot is
empty, the hash is one the loader skips, or no valid mesh of this region
claims it - and counted separately in the cost. The failure keeps the
*requested* slot and its own reason: failing the slots 4 and 5 fallback does
not relabel it as the chain's failure.

The third reason is the common one and it is not an error. A section
routinely names meshes that stream in with another region: over the retail
region measured below, 21 of 39 instances are unplaced for exactly that,
naming 10 distinct hashes. **A backend drawing that scene leaves a hole
where the game draws scenery**, and `unplaced_unresolved` is how much.

## The draw list

One entry is one copy of one prepared material group: an index into
`PreparedTrackScene::instances` and an index into
`PreparedTrackScene::region.draws`. The group's mesh is always the
instance's mesh, so the mesh is not repeated in the entry and the validator
checks the two agree.

A backend uploads `region` once and then, for each entry, binds the texture
the group names, applies the instance's transform, and issues the group's
indices out of its mesh's index stream.

Only drawable groups enter a list. A visible instance the scene placed
nothing for contributes no entry and is counted in `visible_unplaced`; a
placed instance whose mesh has no drawable group contributes none either and
is counted in `instances_without_entries`. A visible entry naming a section
or an instance the scene does not hold is an error rather than a count,
because a silently shorter draw list would hide the two reports disagreeing.

### The two orders

Both are total and deterministic, and the two always hold the same entries.

| Order | Rule | What it is for |
| --- | --- | --- |
| `visible_instance` | the visible set's own order, and inside one instance its mesh's drawable groups in prepared order | under `TrackCullPolicy::retail_tree` this is the retail traversal order, so a backend walks scenery the way the game's tree reaches it |
| `material_group` | every copy of one group contiguous, groups in prepared order, copies in visible order inside a group | one texture bind per group, and a run of copies a backend may submit as one instanced call |

Neither is the retail draw queue; see the car-scene differences above.

Measured on the two retail regions below, over the whole region:

| Region | Entries | Binds, `visible_instance` | Binds, `material_group` |
| --- | ---: | ---: | ---: |
| `STREAML1RB.BUN` section at 23011344 | 46 | 45 | 41 |
| `STREAML1RB.BUN` section at 80 | 41 | 40 | 25 |

## How complete a draw list is

The header carries this and repeats it here because it is the thing a
caller is most likely to get wrong. A list holds exactly the instances the
visible set named that the scene placed. Three separate things narrow it:

1. **The visible set.** Under `TrackCullPolicy::draw_everything` nothing was
   tested at all. Under `retail_tree` it is the recovered frustum rule,
   degraded to a flat instance test for any section whose tree fails
   containment. `TrackDrawList::requested_policy` and `policy_held` carry
   which, when the list came from a cull report.
2. **Residency.** Which sections are resident is the caller's
   `TrackResidency`, and the retail zone list is undecoded (issue #96), so
   `TrackResidencyRule::every_section` is the only rule this project can
   serve for a real frame. The game draws a smaller set than that, and this
   layer cannot say which.
3. **Placement.** `cost.visible_unplaced` counts the visible instances whose
   mesh is not in this region.

There is no single flag for "is this the whole frame", because no single
fact answers it. `policy_held` false, a `requested_policy` of
`draw_everything`, or a non-zero `visible_unplaced` each mean it is not.

## `nfsu_probe track-scene`

```text
build/nfsu_probe track-scene <STREAM.BUN> <geometry-offset> <geometry-size>
      <object-offset> <object-size>
      [--texture <file>]... [--lod-slot <0..5>]
      [--order <visible-instance|material-group>]
      [--eye <x> <y> <z>] [--target <x> <y> <z>] [--far <f>]
      [--residency <every|no-zone>] [--list <count>]
```

The eye and the target are in the world frame of
[`FORMATS.md`](FORMATS.md). With no camera the run uses
`TrackCullPolicy::draw_everything`, which consults no frustum at all; with
one it uses the recovered rule. The command only reads, and it writes
nothing anywhere.

To find a geometry and an object region of one container, list its chunks
and take an adjacent `0x80034100` and `0x80134000` pair; the size to pass is
each payload size plus its 8-byte header:

```text
build/nfsu_probe chunks X:/Games/NFSU/Tracks/STREAML1RB.BUN 24
```

## Tests

The synthetic suite is `tests/track_draw_list_tests.cpp`. It builds a
geometry region and a scenery region as byte images at absolute offsets, so
every alignment rule of [`TRACK.md`](TRACK.md) and
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) applies as it would in a retail file,
parses them with the real parsers and asserts the **exact draw list** - not
a count - in both orders, for the whole region and for a camera that selects
a prefix of it. Order is part of every such assertion, because a list with
the same entries in another sequence binds textures a different number of
times.

### Mutations

Fifty mutations were written against `src/track_draw_list.cpp`, applied one
at a time, rebuilt and run. **49 are caught. One survives, and it is
provably redundant rather than a hole:**

> The validator reports an instance that no section handle names. Every
> handle that is not `kNoHandle` has already been checked to name the
> instance belonging at its own position, and to be the only handle naming
> it, so the count of named instances is exactly the count of placed
> handles, which the next check compares against `instances.size()`. An
> unnamed instance therefore always fails that comparison too. The check is
> kept because it names *which* instance, which the count cannot.

Two further mutations were written and then withdrawn as equivalent rather
than counted as caught: replacing `requested.reason` with `lookup.reason`,
and the same for the hash, on the unplaced path. `lookup` starts as a copy
of `requested` and is reassigned only inside the fallback loop's success
branch, which breaks, so on the path that records an unplaced instance the
two expressions are the same value.

The first run of the fifty caught 34. Closing the rest needed four fixture
changes and eight sharper tamperings, because most survivors were not holes
in the rules but redundancy between checks: swapping two instances breaks
four invariants at once, so removing any one of the four still fails the
test. The tamperings that replaced them move the handle table, the mesh
witnesses and the cost statistics along with the edit, leaving exactly one
invariant broken.

One class of defect this suite cannot reach from a parsed region:
`src/track.cpp` refuses an out-of-range texture slot outright and requires
every material group's index range to tile the mesh index stream, so every
group of every valid track mesh is drawable and a region parsed from a file
never produces an undrawable one. The tests that cover undrawable groups
clear the flag on a prepared region and say so.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and no
game data, screenshot or exported asset may be committed.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-scene X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --list 0

build/nfsu_probe track-scene X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order visible-instance --list 0

build/nfsu_probe track-scene X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --eye 450 20 -80 --target 900 20 -80 --far 60 --list 0
```

Expected:

1. The first exits 0, writes nothing to standard error, and prints
   `placement: slot 0 sections 1 of 1 instances 39 placed 18 fallbacks 0`,
   `unplaced: empty slot 0 skipped hash 0 unresolved 21 distinct 10`,
   `meshes: drawn 17 undrawn 5 resident entries 46 resident triangles 2827`
   and `draw list: entries 46 triangles 2827 groups 42 meshes 17 texture
   binds 41`. Its ten `unplaced` lines run from `0x045E44A6` to
   `0xCAF75482`; the fifth, `0x5848DF47`, is the hash
   [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) reads out of object 0 of this
   section, and it names a mesh that streams in with another region.
2. The second prints the same placement and the same
   `entries 46 triangles 2827 groups 42 meshes 17`, and `texture binds 45`
   rather than 41. The two orders draw the same 46 copies and bind four
   times more often in the order the culler produced them.
3. The third exits 0 and prints `cull: policy retail-tree held yes resident
   1 degraded 0 visible 9`, which is the visible set
   [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) records for that camera, and
   `draw list: order visible instance visible 9 placed 7 unplaced 2 without
   groups 0` followed by `draw list: entries 22 triangles 1389 groups 22
   meshes 7 texture binds 22`.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged afterwards.

### The retail differential

Sixteen pairs of runs, each pair two separate processes of the same command,
over two regions of `Tracks/STREAML1RB.BUN`: the section at offset 23011344
with the geometry at 23015540, and the section at offset 80 with the
geometry at 3072. Each pair covered both orders or one of six far planes
from 40 to 2000. **All sixteen pairs were byte-identical, with no run
writing to standard error and none exiting non-zero.**

Each output file was deleted by exact name before its run and each side had
to have written one, because two runs that produce nothing agree perfectly.
The harness also refuses to start unless `nfsu_probe` with no arguments
prints its usage text: a program whose libraries are not loadable exits with
no output and every run of it matches every other, which is the same failure
wearing a different hat.

Of the twelve camera runs, nine had a non-empty draw list and three did not,
and both numbers are reported because an agreement on two empty answers
proves nothing. The nine non-empty ones held from 20 to 44 entries.

| Region | Far | Visible | Entries |
| --- | ---: | ---: | ---: |
| section at 23011344 | 40 | 7 | 20 |
| section at 23011344 | 60 | 9 | 22 |
| section at 23011344 | 120 | 15 | 25 |
| section at 23011344 | 250 | 26 | 38 |
| section at 23011344 | 600 | 37 | 44 |
| section at 23011344 | 2000 | 37 | 44 |
| section at 80 | 40 | 0 | 0 |
| section at 80 | 60 | 0 | 0 |
| section at 80 | 120 | 0 | 0 |
| section at 80 | 250 | 13 | 30 |
| section at 80 | 600 | 20 | 30 |
| section at 80 | 2000 | 38 | 41 |

The cameras were `450 20 -80` to `900 20 -80` for the first region and
`-400 20 -786` to `600 20 -786` for the second, both in the world frame.

## Observed

- Of the 39 instances the retail section at 23011344 places, 18 resolve a
  mesh in the geometry region beside it and 21 do not, naming 10 distinct
  hashes between them. Of its 22 prepared meshes, 17 are named by an
  instance and 5 are not.
- The section at offset 80 places 38 instances against 11 meshes: 13 resolve
  and 25 do not, naming a single hash, and 10 of the 11 meshes are named.
- Grouping the same draw list by material cut texture binds from 45 to 41 on
  the first region and from 40 to 25 on the second. In both the saving is
  exactly the number of repeated copies: 46 entries over 42 groups is 4, and
  41 entries over 26 groups is 15. In both orders a bind falls almost
  exactly on every distinct group, so what grouping recovers is the rebind a
  second copy of a group would otherwise cost, and how much that is depends
  on how often one mesh is placed twice.
- Both regions report `fallbacks 0` at slot 0, which is what the recovered
  rule says: the fallback covers slots 4 and 5 and nothing else.

## Unknown

- Which mesh slot the game draws for an instance at a given distance. The
  per-slot thresholds at `0x00034102 +24` and the projection scale
  0x00445E10 needs were not recovered, so `lod_slot` is the caller's and
  this layer states that rather than guessing.
- Which sections are resident together. The visibility zone list is
  undecoded (issue #96), so a real frame's resident set cannot be
  reproduced and `every_section` is what a caller has.
- Whether the retail draw queue of [`MATERIALS.md`](MATERIALS.md) orders
  track material groups the way it orders car ones. Neither order here
  claims it does.
- What a backend should do about an unplaced instance. Drawing a stand-in
  would be inventing geometry; drawing nothing leaves a hole. This layer
  counts them and leaves the decision to whoever has a second region to
  stream in.
