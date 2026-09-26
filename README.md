# nfsu-switch

Experimental clean-room engine research for Need for Speed: Underground.

The repository contains no Electronic Arts code or game data. A user-provided
PC installation is required for local research and testing.

## Current milestone

The first milestone is `nfsu_probe`, a read-only installation and chunk-format
inspector. It validates the expected game layout and walks NFS chunk containers
without loading whole asset files into memory. `nfsu_viewer` renders a decoded
car to a screenshot with a portable CPU reference renderer, and with
`--interactive` opens a desktop window on the same scene for inspection.

## Build

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The Nintendo Switch build is opt-in and needs devkitPro; see
[`docs/SWITCH_BUILD.md`](docs/SWITCH_BUILD.md). Its shaders are compiled
offline and held to per-program interface manifests, as
[`docs/SHADER_PIPELINE.md`](docs/SHADER_PIPELINE.md) describes. Its
`nfsu_switch_car` application draws the stock 240SX with the CPU reference
renderer on the console, from six game files on the SD card, painted with
the runtime body skin, reports whether the frame is the one pinned on the
desktop, and lets the left stick orbit the camera.

## Run

```text
build/nfsu_probe summary X:/Games/NFSU
build/nfsu_probe chunks X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN 64
build/nfsu_probe meshes X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN
build/nfsu_probe obj X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN build/240sx.obj
build/nfsu_probe textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe pack-set X:/Games/NFSU/GLOBAL/GLOBALB.BUN
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/tex tga
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/dds dds
build/nfsu_probe texture-bindings X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe car-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe stock-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN
build/nfsu_probe track-region X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN
build/nfsu_probe stock-scene X:/Games/NFSU/Global/GLOBALB.BUN 240SX A X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN --place-wheels
build/nfsu_probe track-objects X:/Games/NFSU/Tracks/STREAML1RB.BUN 23011344 4168 23015540 178268
build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RB.BUN 23011344 4168 450 20 -80 900 20 -80 --far 60
build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc
build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc 0 -1200
build/nfsu_probe track-scene X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group
build/nfsu_probe track-render X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group --out build/shots/region.tga
build/nfsu_probe track-budget X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --order material-group --repeat 9
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --eye 450 20 -80 --look-at 900 20 -80 --view-far 60 --limit 2
build/nfsu_probe track-probe X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN --mesh TRSABC_ROAD_CHOP14 --screen-scale 120
build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc
build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc 783 -8.5
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76
build/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76 56.5 944 -74 56.5
build/nfsu_probe car-placement X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe race-events X:/Games/NFSU/Global/GLOBALB.BUN X:/Games/NFSU/FrontEnd/FrontB.lzc
build/nfsu_probe track-whole X:/Games/NFSU/Tracks/STREAML1RA.BUN X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN --texture X:/Games/NFSU/Global/INGAMEB.BUN --texture X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe track-stream-path X:/Games/NFSU/Tracks/STREAML1RA.BUN X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --hand-path 1003 --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN --texture X:/Games/NFSU/Global/INGAMEB.BUN --texture X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe part-markers X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN exhaust
build/nfsu_probe body-skin-inputs 240SX --source car-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN --source car-vinyls X:/Games/NFSU/Cars/240SX/VINYLS.BIN --source other X:/Games/NFSU/Cars/TEXTURES.BIN --target-texture DUMMY_SKIN1 --vinyl 0 TRIBAL5 16 no-remap --doorline plain --base 200 30 30 128 --assemble
```

Run `nfsu_probe` with no arguments to print the usage text. It lists every
command the program accepts: a command is registered in one place, the
`kCommands` table in `src/main.cpp`, and its usage line is composed from the
same name the program dispatches on, so a registered command is listed under
that name whatever else its entry says. Adding a command is that one table
entry plus its `build/nfsu_probe` line in the list above:
`tests/usage_commands.cmake` reads the command inventory back out of the
table, out of the built program's usage text and out of this file, and fails
unless all three name the same commands. A command that is not registered is
named as unknown and refused with exit 1.

`meshes` lists every mesh with its name, vertex count, face count and
validation status. `obj` writes all valid meshes to a diagnostic OBJ file at
the given path. Both commands print precise diagnostics for malformed meshes
and exit with status 3 if any issue was found. See `docs/FORMATS.md` for the
decoded layout and the exact OBJ axis and winding conventions.

`textures` lists every texture in a pack: hash, format, dimensions, mip
count, record and blob offsets and name. `export-textures` decompresses each
valid texture and writes it to the output directory. `tga` writes the
decoded top level as a 32-bit TGA. `dds` writes the decompressed data of all
levels unchanged, after a DDS header. File names are
`t<index>_<hash>_<name>`, so they stay unique. The export refuses to
overwrite anything. If it fails, it tries to remove the files it created,
and it names any file it could not remove.
`texture-bindings` lists every mesh material record with its texture slot,
texture hash, resolved pack texture, material-key slot and material-key hash.
Its summary also inventories the material flags and material-key usage.
`pack-set` finds every texture pack in a file, including packs nested in a
BUN such as `GLOBAL/GLOBALB.BUN`, and lists their metadata and records.
It never decodes or writes pixel data. Observed P8 records are parsed and
their indexed mip chain and 1024-byte palette can be extracted through the
library API. Diagnostic TGA and DDS export still refuses P8 because palette
channel semantics and runtime skin composition are not yet implemented.
`car-placement` parses the car type table of `GLOBAL/GLOBALB.BUN` and
prints, per car type, its name, class, geometry paths and brand, the four
wheel records with their file offsets, and the 14 placements the game
derives from them, each with the table its source indices belong to.
`part-markers` lists every `0x0013401A` attachment marker of a
`GEOMETRY.BIN` with its resolved name, role, translation and the local
placement the exhaust and licence-plate draw paths build from it; an
optional role argument filters the listing. It reports only meshes that
validated, and prints how many it skipped, so a mesh rejected after its
marker chunk parsed contributes nothing. Between them, the wheels and
brakes come from the car type table and the exhaust and plate from markers,
while every other installed part is drawn at the car's own matrix. See
`docs/PART_PLACEMENT.md`.
`race-events` lists the track table of `GLOBAL/GLOBALB.BUN` (or the
compressed `GLOBALB.LZC` the game loads) and the career event table of
`FRONTEND/FrontB.lzc`: per track its number, stream region and the stream,
stream pack, texture pack and bundle file names the game builds from them;
per event its number, kind, race type, and each race's track, lap count and
direction, every value with the image offset it was read from. It ends with
the Underground order - the event a new career enters, the events winning it
opens, race 1 and the first circuit from race 1. It exits 3 if either table
fails validation or a race names a track the track table does not hold. See
`docs/RACE_EVENTS.md`.
`track-whole` loads the whole track one race uses rather than one bounded
region of it. The track's `TRACKB####.LZC` carries the streaming table of
its `STREAM*.BUN`; the command applies the loader rule recovered from
`speed.exe` to pick the sections the game loads for that track number,
reads exactly their byte ranges and nothing else, places them as one scene
and prints each section's counts, the totals and the memory budgets of
issue #52 for the desktop and for the Switch arena plan. `--set zones` and
`--set stream` load the two bounds the rule sits inside, and `--out` draws
the whole track with the CPU reference renderer and never replaces a file.
See `docs/TRACK_WHOLE.md`.
`track-stream-path` runs the section streamer of `speed.exe` along a path:
the zone a car's look-ahead picks, the request rebuilt when that zone
changes, and the loads and releases that keep the requested sections within
a byte budget, 60 steps a second. It prints every step that did something
and a SHA-256 of the whole sequence, loads each distinct resident set -
exactly its byte ranges - to measure this project's own bytes for it against
the desktop and Switch budgets, and with `--frame-every` and `--out` draws
frames from only the resident sections. The path is a file of raw-slot
vertices, or `--hand-path 1003`: a HAND-GIVEN polyline around the circuit of
race 1, not the game's route. `--policy zone-only` and `--cost prepared` are
labelled bounds, not retail rules. See `docs/TRACK_STREAMING.md`.
`car-parts` parses the car part database of `GLOBAL/GLOBALB.BUN` and lists
every part record: car type hash, slot, upgrade level, part name hash and
the four per-LOD mesh hashes. A car type name lists only its records.
`track-region` decodes one explicitly selected byte range of a track
`STREAM*.BUN` container: a file, an absolute offset and a length, nothing
scanned and no other byte of the file read. It lists every mesh with its
vertex-format word and stride, reports every structural problem with an
absolute file offset, prints deterministic geometry and draw-list cost
statistics, and, when texture files are named after the length, resolves
the region's materials against them in the order given. Only the texture
hashes the region actually uses are decoded. It exits 3 if the region has
any problem. Track meshes use a 24-byte vertex; a mesh whose vertex-format
word selects the 36-byte or 60-byte retail layout is refused by name
rather than guessed at. See `docs/TRACK.md` for the byte evidence, the
decoded retail region and the read-only acceptance procedure.
`track-objects` decodes the scenery sections of one explicitly selected
byte range of the same container, bounded the same way: the per-section
object directory with its six mesh-hash slots per object, the instances
that place those objects with a float translation and a fixed-point 3x3
matrix, and the bounding tree that indexes them. It refuses a tree that is
not a tree, an instance that names no object and a record array that is
not a whole number of records, each with an absolute file offset, and it
exits 3 if the region has any problem. Given a second offset and length it
also resolves every object mesh hash against that region's geometry and
cross-checks each instance's stored box against the mesh it places; a hash
another section carries is reported unresolved rather than treated as an
error. See `docs/TRACK_OBJECTS.md`.
`track-visibility` answers which of those instances one camera can see. It
takes the same bounded region, an eye and a target in the world frame of
`docs/FORMATS.md`, and an optional field of view, aspect, near and far
plane, and applies the rule recovered from `speed.exe`: a six-plane box
test over the bounding tree, which prunes a subtree whose node box is
outside and stops testing under one that is wholly inside, then the
per-instance gates. It verifies before pruning that every node box
contains the boxes below it, and a section that fails falls back to testing
every instance box with an issue saying so. `--policy draw-everything` is
the explicit fallback for a caller that would rather draw the whole region
than trust a visibility rule: it consults nothing at all, not the frustum
and not the two gates whose retail inputs were never recovered, so
`--mask` and `--screen-scale` cannot drop anything under it. The report
names the policy each section was culled with. The zone layer that decides
which sections are drawn is recovered as a rule and its data is decoded by
`track-zones` below; `--zones` with `--zone-point` hands the residency
straight to it, and `--residency` still defaults to every section in the
region so that a caller who names no zone data gets the explicit fallback.
See `docs/TRACK_VISIBILITY.md`.
`track-zones` decodes that data: the `0x80034150` chunk of one
`Tracks/TRACKB<NNNN>.lzc`, whose four children are the zone polygons, the
zones with the section list each one draws, and two further section lists
keyed by the track number. Given a world point in the two horizontal raw
slots it also runs the recovered point-in-polygon test and names the zone
that holds it, or reports that the camera is in none, which is the case
retail's no-zone rule exists for. See `docs/TRACK_ZONES.md`.
`track-scene` composes those three into the one contract a render backend
consumes. It takes a geometry region and the scenery region that places it,
prepares the region's draw list against the texture sources given with
`--texture`, places every instance of every valid section on a prepared
mesh, and then builds the draw list one visible set selects out of the two.
Visibility selection happens entirely above any backend: the draw list is a
flat list of (instance, material group) pairs and nothing below it needs to
know how culling works. `--order material-group` reorders the same entries
so that every copy of one group is contiguous, which is one texture bind per
group instead of one per copy. Which of an object's six mesh slots places an
instance is `--lod-slot`, and it is a documented choice rather than the
retail rule, because the per-slot distance thresholds were never recovered;
an instance whose slot names a mesh that streams in with another region is
counted as unplaced rather than drawn as a stand-in. See
`docs/TRACK_DRAW_LIST.md` for the boundary, the placement rules, what a
prepared scene does not promise and the retail measurements.
`track-render` draws that draw list with the CPU reference renderer and
writes one TGA. It is the first backend of the track path, and it does not
reopen the boundary above: it reads a prepared scene and a draw list and
never builds a frustum or calls the culler. A car sits at the origin and
fits in a box; a region does not, so the render camera frames the drawn
region automatically - target at the centre of the box the vertices
actually cover, distance fitted to that box's bounding sphere - and every
`--view-*` option replaces one field of that camera and leaves the rest.
`--out` is required and is never replaced: an existing target, or one
naming an input, is refused before a file is opened, and every `--view-*`
option is range-checked there too. The command's own refusals, exit
statuses and output rules are pinned by
`tests/track_render_guards.cmake`, which drives 36 cases with no input
files at all. `src/render.cpp` is
not modified by the command's own commit, so the car path's reference
frames are unchanged by construction. See `docs/TRACK_RENDER.md` for the
placement, the framing rule, the retail region's numbers and hashes, the
ground plane cross-checked against the collision data, and what the frame
is still missing.
`track-budget` runs those same four stages and reports what each one costs,
separated by the stage that pays it. It writes no file at all: the frame is
rendered into memory and dropped, so the command cannot leave a derived
game asset on disk. Its output has two halves with deliberately different
stability. The `cost:` lines are counts - draw calls, triangles, texture
binds, bytes retained - which are a function of the inputs and are
byte-identical between runs, and `--limit <counter> <ceiling>` holds any of
them to a ceiling and exits 4 when one is exceeded. The `time:` lines are
wall-clock milliseconds, which on a machine several sessions build on are
not reproducible, so they are reported as a spread over `--repeat`
repetitions after one discarded warm-up rather than as one number, and no
automated limit in this repository is a time. Every repetition's counts are
compared against the first and a disagreement exits 5, which makes the
determinism the counts are quoted under a checked property of each run
rather than a claim. See `docs/TRACK_BUDGET.md` for the measurement
procedure, the retail figures with their variance, and the reasoning for
which limits are automated and which are documented and not.

`track-probe` answers the other question about the same region: not what is
visible but why. For every scenery instance it prints the source offsets,
the hashes, the transform and the materials, and then the visibility
provenance the culler reaches and discards - which arm of the residency rule
made the section resident and at what position in which zone's list, which
node of the bounding tree names the instance and at what depth, which
ancestor pruned it before any gate ran, and what each of the three retail
gates did, beside the measurement each is made of, whether or not that gate
ran. It writes nothing. Its free camera has six degrees of freedom -
`--eye` and `--look-at`, then `--turn` about the eye and `--fly` along the
basis the turn produced - and its default is the same view-independent
framing `track-render` uses, so two runs at different angles compare. See
`docs/TRACK_PROBE.md` for the provenance model, the camera's defaults and
order, the determinism evidence, the retail acceptance and the mutation
results.
`track-collision` decodes the static collision of one track. Collision is
not in the `STREAM*.BUN` containers at all: those hold geometry, scenery,
textures and animation and nothing else. It lives in
`Tracks/TRACKB<NNNN>.lzc`, a JDLZ-compressed image whose five chunks are a
polygon array, an index list, the leaf cells that slice that list, the
quadtree over those cells and a bounding box. The command decompresses the
file, parses the image, prints the counts and both bounding boxes, and
refuses a malformed tree, an index that names no polygon, a cell whose
polygons do not lie in it and a leaf the quadtree places somewhere other
than the cell's own stored centre, each with an absolute offset into the
decompressed image. It exits 3 if anything is refused. Given a column as
`px pz` it also runs the three bounded queries against the decoded
structure: the leaf cells that column touches, the polygons in a 16 by 16
box around it, and a ray straight down. See `docs/TRACK_COLLISION.md` for
the byte evidence, the retail code every record size comes from, the
coordinate units and what is still unknown.
`track-contact` loads the static collision of a whole race track - one
`TRACKB<NNNN>.lzc` is the whole circuit's collision, because the game keeps
exactly one such set resident and loads it once per race - and adds what a
vehicle needs: every triangle's unit normal and whether it is road or wall,
the resident and load-time bytes, the largest collision the loader admits,
and the load placed in the Switch arena plan of `docs/MEMORY_BUDGETS.md`.
Given `px pz` it probes the column there; given two points `px pz py` it
sweeps the segment between them. Either answers hit, clear (a cell lies
under the query and nothing on it is met), outside (no cell lies under any
part of it, which is out of bounds) or refused, and a hit carries the
polygon, the distance in world units, the point and a normal that faces the
query. A column exactly on the seam between two polygons meets them, and
a sweep reports the nearest surface it meets even where issue #39's ray
rounds that surface's edge away. It exits 3 for a collision that fails its
checks, 4 if the Switch plan cannot hold it and 5 for a refused query. See
`docs/TRACK_COLLISION.md`, "The whole circuit".
`stock-parts` applies the game's stock-part rules to one car type and
prints the chosen record per slot with each LOD mesh hash resolved against
the `GEOMETRY.BIN` files given after the car type, including the mesh the
game falls back to when the one a part names is missing. `car-parts`
reports every structural problem and marks the database invalid;
`stock-parts` refuses to run on such a database. See `docs/CAR_SCENE.md`
for the evidence and for what a "stock" part set does and does not cover.
`stock-scene` goes one step further and builds the whole assembly: the
stock part set, the mesh each slot draws at the LOD given as `A`, `B`, `C`
or `D`, and where each drawn copy goes. It reports all 120 slots with the
reason any of them draws nothing, the attachment markers it collected, and
the prepared mesh, material-group and instance counts. `--front-end`
selects the pass whose brake model is LOD A instead of LOD B. The wheel
and the brake are reported as `placement unreproduced` and drawn by
nothing unless `--place-wheels` opts in to the culling placements, because
the drawn wheel matrix is not reproduced by this project; see
`docs/PART_PLACEMENT.md`. No texture file is read, so every material group
is reported unresolved.

Both programs take paths that are not ASCII, including a game installation
or a build directory under a directory whose name is Cyrillic, Greek or
Japanese. On Windows they refuse exactly two spellings, each with a
diagnostic naming the argument and an exit status of 1 before any file is
opened: an argument whose bytes do not decode in the process's active ANSI
code page, and an argument holding `?` outside a leading `\\?\`
extended-length prefix, which Windows writes in place of every character
that code page cannot express and which is reserved in a Windows file name
either way. Nothing is refused on other platforms. See
`docs/PATH_ARGUMENTS.md` for what that second rule does and does not
prevent, and for why this is not the ASCII-only rule in `CLAUDE.md`: that
one is about tracked files and Git metadata, not about where the checkout
lives.

## Reference viewer

`nfsu_viewer` renders a complete car with a deterministic CPU rasterizer
and writes the frame as a TGA screenshot. Rendering needs no window and no
graphics library:

```text
build/nfsu_viewer X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/240sx.tga
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/front.tga --yaw 0 --pitch 5 --distance 3 --width 800 --height 600
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/shared.tga --shared-textures X:/Games/NFSU/GLOBAL/GLOBALB.BUN
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/part.tga --mesh-name 240SX_KITW01_BODY_A --mesh-index 4 --mesh-hash 0x013953B3
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/skin.tga --shared-textures X:/Games/NFSU/Global/GLOBALB.BUN --shared-textures X:/Games/NFSU/Cars/TEXTURES.BIN --skin-car 240SX --skin-vinyls X:/Games/NFSU/Cars/240SX/VINYLS.BIN --skin-vinyl 0 TRIBAL5 16 no-remap --skin-doorline plain --skin-base 200 30 30 128
```

Textures come only from files named on the command line. The car's
`TEXTURES.BIN` is searched first. Each `--shared-textures FILE` is searched
after it, in the order given. A
hash is taken from the first source that has any record for it, even an
unusable one, so a broken car texture is never silently replaced by a
shared one. At most 8 shared files are accepted, and at most 512 MiB is
read for `TEXTURES.BIN` and all shared files together (each file at most
256 MiB); a file that would exceed the total is refused before it is read.
See `docs/RENDERER.md`.

By default every valid mesh is drawn (the "all meshes" profile), so every
kit and style variant overlaps. `--mesh-index N` (source position) and
`--mesh-name NAME` (exact, case-sensitive stored name), and `--mesh-hash HEX`
(verified 0x00134003/0x00134004 directory hash) select an explicit subset
instead; all may be repeated and mixed, at most 256 selectors and
16384 name bytes. Selected meshes are always drawn in source order. An
out-of-range index, an unknown or ambiguous hash, an unknown, empty or
ambiguous name, or a mesh selected twice is a usage error (exit 1) and
nothing is written. Neither of those two profiles is the game's stock car:
they name meshes, and each named mesh is drawn once in its own space.

`--stock-car NAME` draws the third `PreparedCarScene` profile instead:
this project's reproduction of the stock assembly as far as it is decoded,
built by `nfsu::prepare_stock_car_scene`, with every part where the
recovered placement puts it. It needs `--stock-parts <GLOBALB.BUN>`, which
holds both the part database and the car type table, and one
`--stock-geometry FILE` per geometry file beyond the car's own.
`--stock-lod A|B|C|D`, `--stock-pass world|front-end` and `--place-wheels`
are the three choices the profile takes. It cannot be combined with the
mesh selectors above.

```text
build/nfsu_viewer X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/stock.tga --shared-textures X:/Games/NFSU/Global/GLOBALB.BUN --shared-textures X:/Games/NFSU/Cars/TEXTURES.BIN --stock-car 240SX --stock-parts X:/Games/NFSU/Global/GLOBALB.BUN --stock-geometry X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN --stock-geometry X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN
```

The report then lists every installed slot with what became of it, the
drawn copies apart from the meshes (`instances: N exact E stand_in S`) and
whether the wheels were placed. Without `--place-wheels` the wheel and
brake slots report `placement unreproduced` and put nothing in the frame;
with it they draw four copies each at the culling placements, which are a
documented stand-in and not the matrix the game draws them with, and the
report says so. `docs/CAR_SCENE.md` says what a "stock" part set does and
does not cover; paint and skin are still the caller's explicit selection
(see `docs/PART_PLACEMENT.md` and `docs/RENDERER.md`).

`--interactive` opens a desktop window on the same prepared scene once the
screenshot has been written and the report printed:

```text
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/240sx.tga --shared-textures X:/Games/NFSU/GLOBAL/GLOBALB.BUN --interactive
```

Left drag orbits, right drag pans, the wheel zooms and a click selects the
material group under the pointer and reports its mesh, record fields, bound
texture and retail render state. `V` cycles the shaded, per-group, per-mesh
and depth views, `O` cycles the overlay. Without the flag nothing of it
runs: the TGA, the report and the exit code are unchanged. The window is
Win32 and GDI, with no external dependency, and every rule it follows lives
in a backend-neutral layer a deko3d renderer will reuse. See
`docs/INSPECTOR.md`. The platform it runs on is reached through six
services, `docs/PLATFORM_SERVICES.md`, and a controller drives the same
camera, `docs/INPUT.md`.

## Body-skin compositor reference

`nfsu_body_skin` (`include/nfsu/body_skin.h`) is a bounded CPU reference for
the game's runtime body-skin compositor. It reproduces the weighted lane
sum both composite paths share, the mask arithmetic, the colour remap, the
target markers, and the palette the indexed path builds, for both target
types the game dispatches on. Every bound the game leaves unchecked is
refused with a message instead, and the colour quantizer the indexed path
falls back to is not implemented and never guessed. Its API numbers byte
lanes instead of naming channels, because the two paths store a colour with
two lanes exchanged; which channel each lane carries is documented once, with
its evidence. No pixel format is named anywhere, because no instruction names
one. See `docs/BODY_SKIN.md`.

`nfsu_skin_inputs` (`include/nfsu/skin_inputs.h`) feeds it. It takes an
explicit customization selection - which vinyl each of the four layers
uses with its `NUMCOLOURS` and `REMAP` colors, which hood/spoiler and
doorline variant is installed, and the five paint colors - and turns it
into a `BodySkinRequest` whose every buffer the result owns. It invents no
customization state: where the game itself substitutes a value, the
substitution is optional and reported back. It resolves each source and its
exact `_MASK` partner out of read-only parsed packs, keeps every raw
identifier it saw, converts only in the three cases retail evidence proves,
and refuses the rest. `nfsu_probe body-skin-inputs` prints the whole
assembled layer list with its provenance and writes nothing. See
`docs/SKIN_INPUTS.md`.

`nfsu_runtime_skin` (`include/nfsu/runtime_skin.h`) closes the chain. It
takes the compositor path from the real target record instead of from an
option - a record whose format code is 0x20 keeps that byte, a paletted one
has it rewritten to 0x20 by the upload, and anything else is refused with
the instruction that would have turned it away - reads the untouched target
so the marker scan sees it, runs the proven composite and converts the
result to RGBA8 with the same byte mapping a decoded A8R8G8B8 level gets.
It then binds the generated texture into a `RenderTextureSet` under an
explicit list of hashes, removing and reporting every pack record that
claimed one - and keeping every hash such a record also claimed that was
not bound, so those hashes do not silently stop matching anything - so a
prepared scene resolves those material groups to it. The surface it
composes carries the record and the buffer it was read from, and the
composite is given the source list and checks both against it, so a surface
read from another record - or from another list holding a pack of the same
layout at the same index - is refused instead of being bound under the
wrong hash. That identity ties coordinates and not content: a caller that
rewrites the same live buffer, or replaces a surface's pixels while keeping
its identity, is not seen, and `include/nfsu/runtime_skin.h` says so. No
pack is modified and nothing is parsed twice. `nfsu_viewer --skin-car NAME`
drives it; without that option nothing in this chain runs and a render is
byte-identical to before on its TGA, stdout, stderr and exit code. The one
thing that did change for every invocation is the usage text, which gained
the new options. See `docs/RUNTIME_SKIN.md`.

Selection and material lookup happen once, in `prepare_car_scene`
(`include/nfsu/scene.h`). It builds a self-contained `PreparedCarScene`
that owns copies of the selected vertices, triangles and resolved textures
and holds one validated draw per material group. The CPU renderer draws
only that scene and never looks up a texture hash; a future deko3d backend
is meant to consume the same draw list. Parsed texture packs become
`RenderTextureSet` through the separate backend-neutral
`nfsu_texture_decode` library, so neither backend depends on the other's
implementation.

The camera orbits `--target X,Y,Z` at `--distance`, with `--yaw` (0 = front,
90 = right side) and `--pitch` (positive = from above) in degrees. Other
options are `--fov`, `--near`, `--far`, `--flip-v`, `--alpha-cutoff`,
`--vertex-color`, `--cull` and `--shading`. Every numeric argument is
validated against a documented range. The same inputs and options always
give a byte-identical file. Textures that are in none of the given sources
are drawn with a hash-colored checker and counted; another texture is never
substituted for them. By default texel alpha is ignored and decoded RGB is
drawn opaquely, and `--alpha-cutoff 128` is a diagnostic alpha test.

`--material-state retail` applies the car material render state recovered
in `docs/MATERIALS.md`: the texture-info alpha test with its GREATER 0xAA
rule, the texture-info blend selection, the retail draw queue order, and
the flag-controlled coefficient gates. The retail order is recovered, not
guessed: the queue buckets a material by two more texture-info bytes,
batches by texture inside a bucket and keeps depth writes on for blended
car draws. The undecoded coefficients are explicit options
(`--material-alpha`, `--environment`, `--environment-color`,
`--environment-global`) whose defaults make the environment term vanish and
leave the alpha lane at the identity, which is an assumption and not a
retail value. The renderer never supplies a retail value for one. `off` is the default and
its frame is byte for byte what it was before the option existed. Decoded textures are
capped at 128 MiB in total, over all sources together. The viewer never overwrites an existing file:
it creates `<out>.partial` exclusively and publishes it with a no-replace
rename (Windows) or `link` (POSIX), and it refuses to write on other
platforms.
See `docs/RENDERER.md` for the camera, projection, clipping, fill, depth,
sampling and alpha rules.

All commands are read-only with respect to the game installation. Do not
commit exported meshes, images or screenshots; they are derived from
copyrighted game data.

## Project direction

1. Validate retail PC data and document chunk layouts.
2. Decode one car geometry and texture set.
3. Render the car in a portable desktop reference application.
4. Load one track region and add camera and collision tests.
5. Add a Nintendo Switch platform backend using libnx and deko3d.

