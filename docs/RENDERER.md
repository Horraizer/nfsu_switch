# Reference renderer (`nfsu_render`, `nfsu_viewer`)

`nfsu_render` is a dependency-free CPU rasterizer written in portable C++17.
It is the deterministic reference for later GPU and deko3d backends: the
same inputs and options give a byte-identical frame. `nfsu_viewer` loads
`GEOMETRY.BIN`, `TEXTURES.BIN` and any explicitly named shared texture
files through `nfsu_core`, renders one frame
and writes it as a TGA screenshot. The screenshot is the acceptance
artifact. The render itself uses no window and no platform graphics API.

`--interactive` adds a desktop window over the same prepared scene, after
the screenshot has been written and the report printed
([`INSPECTOR.md`](INSPECTOR.md)). It is a diagnostic on top of this
renderer, not a second one: without the flag none of its code runs, and a
frame it shows is a frame this renderer produced.

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> <out.tga> [--shared-textures FILE]... [--mesh-index N | --mesh-name NAME | --mesh-hash HEX]... [--skin-car NAME [--skin-* ...]] [--interactive] [options]
```

| Option | Range | Default |
| --- | --- | --- |
| `--width N`, `--height N` | 1..8192 each, at most 16777216 pixels | 1280, 720 |
| `--yaw DEG` | -36000..36000 | 35 |
| `--pitch DEG` | -89..89 | 15 |
| `--distance D` | 0.01..10000 | 7 |
| `--target X,Y,Z` | each -10000..10000 | 0.2,0.35,0 |
| `--fov DEG` | vertical, 1..170 | 35 |
| `--near D` | 0.0001..10 | 0.05 |
| `--far D` | above near, up to 100000 | 100 |
| `--flip-v` | flag | off (raw UV) |
| `--alpha-cutoff N` | 0..255 | 0 (no alpha test) |
| `--vertex-color` | flag | off |
| `--cull MODE` | `none`, `back`, `front` | `none` |
| `--shading MODE` | `flat`, `none` | `flat` |
| `--material-state MODE` | `off`, `retail` | `off` |
| `--material-alpha MIN,RANGE` | each 0..16 | 1,0 |
| `--environment MIN,RANGE,POWER` | MIN and RANGE 0..16, POWER a whole 0..64 | 0,0,1 |
| `--environment-color R,G,B` | each a whole 0..255 | 0,0,0 |
| `--environment-global STATE` | `on`, `off` | `on` |
| `--mesh-index N` | 0..4294967295 decimal, repeatable | none (all meshes) |
| `--mesh-name NAME` | 1..28 printable ASCII bytes, repeatable | none (all meshes) |
| `--mesh-hash HEX` | 1..8 hex digits, optional `0x`, repeatable | none (all meshes) |
| `--interactive` | flag; needs a window backend | off |

The `--skin-*` options are listed in
[`RUNTIME_SKIN.md`](RUNTIME_SKIN.md) and summarized under "Runtime body
skin" below. They do nothing unless `--skin-car NAME` is given, and giving
one without it is a usage error (exit 1).

Each option may be given once, except `--shared-textures` (at most 8
times), the mesh selectors (see below) and the repeatable `--skin-bind`,
`--skin-bind-hash`, `--skin-vinyl`, `--skin-vinyl-color` and
`--skin-special`. `--material-alpha`, `--environment`,
`--environment-color` and `--environment-global` are refused without
`--material-state retail`, because they would otherwise be silent no-ops.
Numbers are parsed with `std::from_chars`
and must be consumed completely and be finite. Anything else is a usage
error (exit 1). An output path that exists or names an input is refused
before loading (exit 1). Rendering or writing failures exit with 2. If the frame is
written but the inputs reported parse issues, the exit code is 3.

The report on stdout is deterministic. It contains no paths except the
output file name and no timings. It covers the input counts, frame size and
background, every camera value, the eye position, every option (including
`flip_v`) and all statistics below.

## Texture sources and precedence

Texture source 0 is the car's `TEXTURES.BIN`. Each `--shared-textures
FILE` adds the next source, in command-line order. No other file is ever
read, and no directory is scanned. A shared file may hold any number of
texture packs, for example the two packs in `GLOBAL/GLOBALB.BUN` (see
`docs/FORMATS.md`). A shared file named twice, or naming the car's pack, is
a usage error (exit 1). An output path equal to any source is refused
before loading.

`decode_render_texture_sources` builds one texture set with these rules
(tested with synthetic sources in `tests/render_tests.cpp`):

- A record claims every hash it carries: info +36, hash table +0 and, with
  JDLZ storage, blob table +0. Invalid records claim their hashes too.
- A record of a later source that claims a hash already claimed by an
  earlier source is left out and listed as shadowed. The earlier record
  wins even if it is invalid or cannot be decoded. The hash is then
  `unusable` and drawn with the fallback, never silently taken from a
  lower-priority source.
- A shadowed record still claims all its hashes for later sources. If it
  claims hashes no earlier source claims (an invalid record whose hash
  fields disagree), each of those is listed in
  `RenderTextureSet::unusable_claims` with its source, pack and record,
  and resolves as `unusable`, whether or not a later source has it. This
  applies only if the record passes the wanted filter below.
- Records inside one source never shadow each other. The same hash twice
  in one source, including across two packs of one BUN, is `unusable`, as
  before. Each kept texture carries every distinct hash its record claims
  (`RenderTexture::claims`, at most three), and a material matches a
  texture through any of them. An invalid record whose hash fields
  disagree therefore makes each of its hashes `unusable` in its source,
  even if a valid record there has one of them.
- A source that may hold records it could not list (a walk problem, a
  limit, an unreadable file, or a pack whose tables failed) is incomplete,
  and no later source is used. A readable file without any pack root is
  complete: it claims nothing, but it is still reported as an issue. Later sources are reported
  as `blocked`.
- Shared sources decode only records that claim a hash a selected mesh
  material uses (`wanted_hashes`; every valid mesh without selectors). The other records still claim their
  hashes. The car's pack is decoded in full, as before.
- One budget covers everything decoded: at most 128 MiB of retained RGBA8
  over all sources together, checked before each allocation. A budget
  failure names the texture, source and pack.
- Every pack must have been parsed from the exact buffer it is paired
  with: `TexturePack::parse_source` must equal the buffer address and
  `file_size` its size, otherwise decoding fails.

Texture input read into memory is bounded too. At most 8
`--shared-textures` files are accepted (a ninth is a usage error, exit 1,
before anything is loaded). Each file is at most 256 MiB, and the car's
`TEXTURES.BIN` plus all shared files together are at most 512 MiB. Before
each shared file is read, its per-file limit is lowered to what remains of
the 512 MiB, so a file that would exceed the total is refused without being
read. It is then reported on stderr and as a source issue, the source is
incomplete, later sources are blocked, and the exit code is 3, as for any
unreadable source.

Without `--shared-textures` the viewer calls `decode_render_textures` as
before. Its stdout, stderr and TGA are byte-identical to the previous
version. With shared sources, stdout gains per-source lines (`texture
source N: bytes, packs, records, valid, decoded, decoded_bytes, issues,
complete`), a `texture sources:` summary (total decoded bytes,
`wanted_hashes`, `not_wanted`, `shadowed`, `blocked`), and one line per
shadowed or blocked record and per unusable claim (`unusable claim: hash
H source S pack P texture R`, in source, pack, record and claim order as
the decode left it; a runtime-skin binding appends its preserved claims
after those, so with `--skin-car` the tail of the block may be out of that
order and repeats the `skin preserved claim:` lines). The `textures:` line
still describes the car's pack alone. Problems in shared sources are
printed on stderr as `texture source N [pack P] issue at ...`. The exit
code is 3 if any source reported an issue.

## Runtime body skin

Off unless `--skin-car NAME` is given. Without it no extra file is read, no
line of the report changes and the TGA is unchanged; that is checked
against a build of the previous commit, byte for byte
([`RUNTIME_SKIN.md`](RUNTIME_SKIN.md)).

With it, the viewer composes the car's body skin from an explicit
customization selection and binds the result so that a material group
naming one of the bound hashes resolves to it.

- The composite target is named by `--skin-target NAME` (default
  `DUMMY_SKIN1`) or `--skin-target-hash HEX`. Which composite routine runs
  is taken from that record, never from an option.
- Skin input packs are searched in a fixed order: the car's
  `TEXTURES.BIN`, then `--skin-vinyls FILE`, then each `--shared-textures
  FILE` in the order given, first claim wins. This is the rule
  `decode_render_texture_sources` uses, over a different list. Every one of
  these packs is parsed exactly once, by the viewer, for both purposes;
  nothing is parsed twice.
- `--skin-vinyls` is a skin input source only. It never becomes a material
  texture source and is not a `--shared-textures` file, so texture source
  numbering is unchanged. It is bounded by the same 256 MiB per-file cap,
  and the bytes it takes come out of the same 512 MiB total, so it lowers
  what is left for the shared files read after it. The car's pack and this
  file cannot exceed that total between them, so the clamp on this one file
  never binds; it is kept so the rule holds if either constant changes.
- The generated texture is bound to the target hash and, unless
  `--skin-no-body-bind` is given, to `bin_hash("<NAME>_SKIN1")`, the hash
  the retail render path replaces with `RideInfo+0x444`. `--skin-bind NAME`
  and `--skin-bind-hash HEX` add more, up to three hashes in total.
- It outranks every pack source for those hashes: any decoded texture or
  unusable claim that claims one is removed from the set and reported, so
  the generated texture is the single claimant `resolve_material` needs.
  No pack is modified and `RenderTextureSet::shadowed` is left alone,
  because it records what the decode did.
- A removed record may have claimed hashes this run never bound, which one
  whose three hash fields disagree does. Those hashes are appended to
  `RenderTextureSet::unusable_claims` and reported, so they go on resolving
  as `unusable` instead of silently becoming `unresolved`. They are
  appended, not merged in order, so the `unusable claim:` block prints them
  last; they appear there as well as on their own `skin preserved claim:`
  line, because that block describes the set the scene is prepared from.
- The per-source decode counts on stdout are taken before binding, so they
  keep describing the decode.
- `--skin-dump FILE` writes the generated texture as a TGA through the same
  no-replace protocol as the screenshot, after the composite and before the
  render. It is refused if it exists, names an input, or names the
  screenshot or its `.partial`. "Names" there is `nfsu::same_file_path`
  (`include/nfsu/render.h`), which compares paths that do not exist yet: it
  makes both absolute before normalizing them, removes `.` and `..`, takes
  off the Windows extended-length prefix, leaves a network path absolute,
  and asks the filesystem - read-only - whether it folds case before
  deciding that two spellings differing only in case are one file. What it
  does not see is listed at its declaration. Because the dump is written
  before the render, a spelling that got through would publish the
  generated texture at the screenshot's path and then fail the render.

stdout gains one `skin source I: <label>` line per skin input source
actually searched (a file whose packs the assembly layer would refuse is
left out, reported on stderr, and makes the exit code 3), then `skin:`,
`skin target:`, `skin paint:`, one `skin input` line
per selected source index, one `skin bound:` line per hash, one `skin
displaced:` or `skin displaced claim:` line per record the binding
removed, one `skin preserved claim:` line per hash a removed record
claimed that was not bound, `skin generated:` and, with a dump, `skin
dump:`. A selected input that did not resolve is reported with its skip
reason, is not fatal, and makes the exit code 3.

The `source S` on these lines is not one numbering. `skin target:` and
`skin input ...:` use the skin input source index the `skin source I:`
legend defines, which counts `--skin-vinyls`; `skin displaced:`, `skin
displaced claim:`, `skin preserved claim:` and `skin generated:` use the
`RenderTextureSet` source index, which does not, and which for the
generated texture is the synthetic index past the real sources.
[`RUNTIME_SKIN.md`](RUNTIME_SKIN.md) spells both out.

## Mesh selection and the prepared scene

The renderer does not read `GeometryReport` or `RenderTextureSet`.
`prepare_car_scene` (`include/nfsu/scene.h`, library `nfsu_scene`) turns
them into a `PreparedCarScene` once, and `render_scene` draws only that
scene. The prepared-scene layer has no file, path, `TexturePack`, BUN, JDLZ
or command-line API; source/pack/record numbers are opaque provenance. Its
public header uses only the
in-memory mesh types in `include/nfsu/mesh.h`, not the geometry file API.
The separate `nfsu_texture_decode` library (`include/nfsu/texture_decode.h`)
turns parsed packs into `RenderTextureSet`; a frontend links it while CPU
and GPU backends only consume `PreparedCarScene`. TGA writing is likewise
the independent `nfsu_image` utility, not part of texture-pack decoding.

Selection:

- No selector is the explicit profile `all meshes`: every valid mesh in
  source order, invalid meshes skipped and listed
  (`skipped_invalid_meshes`). Its stdout, stderr and TGA are
  byte-identical to the previous version.
- `--mesh-index N` selects the mesh at position N of the geometry report
  (the index printed by `nfsu_probe meshes`). `--mesh-name NAME` selects
  the one mesh whose stored name equals NAME byte for byte,
  case-sensitively. `--mesh-hash HEX` selects the one mesh with that
  verified 0x00134003/0x00134004 directory hash; the optional prefix is
  `0x` or `0X`. They may be repeated and mixed. Any selector switches to
  an `explicit subset`.
- All selectors are resolved before any texture file is read. The
  selection is rejected, with exit 1 and nothing written, for: more than
  256 selectors or more than 16384 name bytes (both checked while
  parsing, before a selector is stored), an index that is not plain
  decimal, has more than 10 digits or is past the last mesh, an empty
  name, a name longer than 28 bytes or with a byte outside 0x20..0x7E, a
  name no mesh has, a name more than one mesh has (for example
  `240SX_KIT00_LEFT_SIDE_MIRRO`, stored three times), an invalid mesh, and
  a malformed hex hash, an unavailable or incomplete verified directory,
  a hash no mesh has, a hash more than one mesh has, and the same mesh
  selected twice by any combination. Selectors are checked
  in command-line order and the first problem is reported, numbering
  selectors from 0.
- Selected meshes are always drawn in ascending source position, whatever
  the selector order, so triangle ids and depth ties do not depend on it.
- Shared sources decode only hashes used by the selected meshes
  (`wanted_hashes`).
- For an explicit subset stdout gains, before the statistics, `selection:
  explicit subset, K of M meshes, S selectors` and one `selected: mesh I
  name "NAME" by selector J (index|name|hash)` line per mesh in source order.
  The `meshes` statistic then counts the selected meshes only.
- Neither of these two profiles is the game's stock car, and neither
  places anything: they name meshes and each mesh is drawn once in its own
  space. An explicit subset is a diagnostic view chosen by the user.
- The stock car is a third profile, `stock car`, built one layer up by
  `prepare_stock_car_scene` (`include/nfsu/stock_scene.h`, library
  `nfsu_stock_scene`) out of the recovered part set, the per-LOD mesh
  hashes and the recovered placement. See "The stock car profile" below.
  `render_scene` draws it, one copy per instance, with the transform each
  instance carries; "Instances" below states the composition and what the
  frame does and does not claim.

`PreparedCarScene` holds, in deterministic order:

- `profile`, the `selectors` as given, `source_mesh_counts` (one entry per
  geometry source, in source order; the two single-source profiles hold
  exactly one) and `skipped_invalid_meshes`;
- `meshes`: per selected mesh its geometry source and position inside that
  source, stored name, verified directory hash/range when present, the
  selector that chose it, source material count, and copies of its
  vertices and triangles (three u16 indices each). The list ascends in
  (source, position);
- `draws`: one `PreparedDraw` per material record of each selected mesh,
  in mesh then record order, with the mesh handle, material index, first
  index, index count, texture slot, texture hash, `BindingKind`, texture
  handle and its source, pack and record (resolved only), material-key slot,
  flags, record offset, bounds, the untouched 60-byte record, and
  `drawable`. A group with an invalid slot or an index range that is not a
  multiple of 3 inside the mesh index stream is kept with `drawable ==
  false`, counted as invalid and never drawn, as before;
- `textures`: one copy of each resolved texture, in order of first use,
  addressed by the draw's texture handle;
- `unresolved_hashes`: the distinct hashes of unresolved groups of the
  selected meshes, ascending (the `unresolved hashes` statistic);
- `instances`: the drawn copies, empty for every profile but the stock
  car. Each names a prepared mesh, a row-major 4x4 transform in the
  geometry file's raw slot frame, whether that transform is the one the
  retail draw path applies (`placement_exact`), and opaque provenance:
  part slot, LOD index, copy number and mesh hash.

That is everything a GPU backend needs: upload each mesh's vertices and
index stream and each texture, then issue `index_count` indices from
`first_index` per drawable draw, with the texture handle or the fallback
color of `texture_hash`.

Lifetime: the scene owns every byte a backend reads. Nothing in it points
into the `GeometryReport` or `RenderTextureSet`, so those may be changed,
reallocated or destroyed after preparation without any effect; the tests
change the hash list, clear every source buffer and destroy both sources,
and the frame stays byte-identical. The cost is one copy of the selected
geometry (about 4 MiB for all 240SX meshes) and of the resolved textures,
bounded by the scene limits below.

Validation: preparation checks the selection, every count against the
scene limits with overflow-safe additions before copying, every index
range, and the resolved texture index; it catches `std::bad_alloc` and
leaves the scene empty on any failure. It then runs
`validate_prepared_scene` on its result. The scene's fields are public, so
`render_scene` treats it as untrusted and runs `validate_prepared_scene`
again before allocating the frame: profile and selector references,
semantic selector provenance, complete all-mesh source coverage, exactly
one ordered draw per source material, agreement between every decoded
material field and its raw 60-byte record, the derived `drawable` flag and
unresolved-hash set, binding/provenance consistency, texture handles in
first-use order with no unused texture, texture sizes (`width * height * 4`
without overflow), a drawn triangle total below 2^32 - 1 both per group
and over every copy the instances ask for, and instances
that name a prepared mesh with a finite transform and a hash that mesh
has, leave no prepared mesh of a stock car undrawn, and appear on no other
profile. A scene that
fails is refused before a new frame is allocated. Vertex indices inside
triangles are checked per triangle while drawing and counted as `bad_index`,
as for source data.

### The stock car profile

`prepare_stock_car_scene` takes the two `GLOBALB.BUN` tables, the loaded
geometry files in load order, a car type name, a car-wide LOD index and the
pass, and produces one `PreparedCarScene` plus a `StockSceneReport` that
describes all 120 slots. Every rule it applies was traced in `speed.exe`;
the evidence is in `docs/CAR_SCENE.md` ("Installed parts") and
`docs/PART_PLACEMENT.md`, and this section only says what the profile does
with it.

- The installed set is `select_stock_parts`, its enable bytes included. A
  slot outside 0..29 gets no model, and a disabled slot is not drawn.
- The mesh of a slot is `resolve_part_models` at the slot's LOD, so the
  constructor's fallback for a missing mesh applies. A mesh no loaded
  source holds, and a mesh whose source holds it but which failed
  geometry validation, are both reported and drawn by nothing.
- The LOD is the requested one for every generic slot and for the mirrors,
  which draw at LOD A and B only; LOD A for the exhaust and the licence
  plate; LOD A or B for the brake, by the pass; and `min(lod, 2)` for the
  wheel.
- Placement: every body part draws at the identity, that is at the car's
  own matrix. The exhaust and the licence plate draw one copy per
  collected marker, at `car_marker_quarter_turn() * marker matrix`; the
  markers are collected exactly as `0x0056BDB0` collects them, over the
  LOD A models of the enabled slots 0..29 in slot then record order. A
  part with no marker of its role is not drawn.
- The wheel and the brake are **not placed by default**. The drawn wheel
  and brake matrices are not reproduced by this project
  (`docs/PART_PLACEMENT.md`, "The drawn wheel and brake matrices"), so both
  slots report `placement unreproduced` and contribute nothing.
  `place_wheels_from_culling_table` opts in to four copies each at the
  culling placements of `derive_cullable_part_placements`, with
  `placement_exact` false on every instance it produces. Copies 0 and 3,
  the left pair, are first turned the way the retail draw turns them:
  `car_wheel_half_turn() * translation` for the wheel and
  `car_brake_reflection() * translation` for the brake (issue #161,
  `docs/PART_PLACEMENT.md`, "Which way the wheel and brake models face").
- Several slots may name one mesh. It is prepared once and drawn once per
  slot, which is also how the wheel's four copies share one mesh.

`nfsu_probe stock-scene` prints the whole report. For the retail 240SX at
LOD A with `--place-wheels`, all 24 slots that can have a model draw, from
24 meshes over `Cars/240SX`, `Cars/BRAKES` and `Cars/PLATES`, in 30
instances; the counts per LOD are in `docs/CAR_SCENE.md`.

`nfsu_viewer --stock-car NAME` renders that profile. It needs
`--stock-parts <GLOBALB.BUN>`, which holds both tables, and one
`--stock-geometry FILE` per geometry file beyond the car's own;
`--stock-lod`, `--stock-pass` and `--place-wheels` are the three choices
the profile takes. It cannot be combined with `--mesh-index`,
`--mesh-name` or `--mesh-hash`: only one of the two can be the scene.

The report then carries, before the usual statistics and only for this
profile: one `stock geometry N:` line per source with its mesh count and
whether its directory is verified; `stock:` with the car type name, hash,
record, LOD and pass; one `stock slot` line per installed slot with its
outcome, and for a drawn slot its LOD, geometry source, mesh, placement
mechanism and copy count; `stock slots:` with the installed, with-model,
drawn, fallback and marker totals; `stock wheels:` saying whether the
wheel and brake slots were drawn and, when they were, that the culling
placements they used are a stand-in; `stock wheel plane: degenerate` when
the four wheel centres do not span a plane; and `stock absent hash:` /
`stock ambiguous hash:` lines for the hashes no source holds and the
hashes more than one source holds.

### Instances

An instance is one drawn copy of a prepared mesh. A profile that carries
none draws each of its meshes exactly once, in the space the mesh was
authored in, which is what this renderer did before instances existed and
what it still does, by the same arithmetic. That is not spelled as an
identity transform on purpose: running the composition below would be the
identity map on every finite vertex except for the sign of a zero
component, and the two single-source profiles have to stay byte-identical
with every earlier build. The three reference hashes at the end of this
document are the check, and they reproduce.

**The composition.** `PreparedInstance::transform` is applied in the raw
slot frame, as a row vector, and the result is then mapped into the world
frame the camera works in:

```text
slot  = (v.px, v.pz, v.py)
moved = slot * transform            (row vector; row 3 is the translation)
world = (moved[0], moved[2], -moved[1])
```

Each half of that is a fact already established elsewhere in this project,
not a choice made here:

- The transform and the vertex live in one frame. `docs/FORMATS.md`
  ("Attachment markers") records that a marker matrix's translation "is in
  the same raw slot order as a vertex (longitudinal, lateral, vertical)",
  and `docs/PART_PLACEMENT.md` records that the three wheel-record floats
  `0x5693d0` copies are in "the same right-handed frame the geometry file
  uses in raw slot order", with the evidence for each of the three signs.
  The vertex's slot order is `docs/FORMATS.md`'s 36-byte layout: `px` at
  +0, `pz` at +4, `py` at +8. So the multiply happens in that frame, on a
  vector built in that order, and not in the renderer's world frame.
- The multiply is by rows. `nfsu::multiply_placement_matrix` reproduces
  the retail multiply at `0x005AC69C`, which is row-major with the
  row-vector convention, and `car_marker_placement` composes
  `quarter_turn * marker` in that convention. A column-vector reading of
  the same 16 floats transposes every rotation.
- The slot-to-world map is the renderer's own, `world = (px, py, -pz)`,
  under "Frames and camera" below. Applying it after the multiply rather
  than before is what keeps an identity transform equal to the unplaced
  path, and it is the only order in which the matrix acts in the frame it
  was authored in.
- Nothing is composed on the outside. The scene holds one car, and the
  car's own world matrix is the identity here, so a body part's identity
  transform leaves it exactly where the per-mesh path put it. A second car,
  or a car placed on a track, would multiply that matrix in on the outside;
  nothing in this project produces one yet.

One thing the renderer reads less of than the type promises: only the
affine part. `PreparedInstance::transform` is a 4x4, and
`multiply_placement_matrix` multiplies all sixteen elements, but
`placed_vertex` never reads elements 3, 7, 11 and 15 - the fourth column of
the row-vector convention - and so never divides by a `w`. Every transform
this project produces leaves that column at `(0, 0, 0, 1)`, where reading
it would change nothing: `identity_matrix` and `translation_matrix` write
it, and `car_marker_placement` preserves it for every marker record in the
retail 240SX. A marker whose matrix had a nonzero element 11 would survive
the quarter turn as a projective part placement, and this renderer would
place it as though it were affine rather than refuse it. No such record
was found, the retail draw path has not been seen to use one, and
validating the column would refuse a whole car for one odd marker, so the
divergence is recorded here rather than closed.

Confirmation from the data, which is the check and not the proof: the
240SX wheel table gives the four culling placements longitudinal
+1.39/-1.04878, lateral +-0.73 and vertical +0.08, while the car's own
meshes span px -2.09..2.48, pz +-0.935 and py -0.31..1.11
(`docs/FORMATS.md`). Placed by the rule above, the four wheel copies land
under the four arches. Read as `(x, y, z)` in the world frame instead, the
same four numbers would put two copies 0.73 above the car and two 0.73
below it, all four within 0.08 of the centre line.
`tests/scene_tests.cpp` pins each component separately, by rendering an
instance that moves one component against a geometry whose vertices carry
the same move, and requiring the two frames to be equal byte for byte; a
shear case does the same for the row-vector convention.

**Draw order.** The order is a property of the material groups, not of the
instances: the retail queue holds materials, and "Draw order" below
recovers how it is filled and drained. So the group order stays exactly
what it was, and each group submits one copy per instance of its mesh,
in ascending instance order, inside its own place in that order. A group
of a four-instance wheel is drawn four times in a row, and no instance
splits the queue. The alternative - instances outside, groups inside -
would draw a whole part, transparent groups included, before the next
part's opaque ones, which is not what the queue does. A copy is a
different position, not a different material, and the retail queue has no
row for a position.

None of this changes a profile that carries no instances: it submits one
copy of each group, so it reaches `prepared_draw_order`, the id numbering
and the rasterizer exactly as it did before.

**Triangle ids.** Ids stay the running number of submitted triangles over
that order, so a group's copies share one contiguous range and
`prepared_triangle_ranges` reports `count` as the group's triangles times
its mesh's instance count. A pixel's id therefore still names the group
that drew it, which is what the inspection window picks
([`INSPECTOR.md`](INSPECTOR.md)), and the copy is
`(id - first_id) / (count / instances)`. `prepared_draw_triangles` counts
the copies too, so the submitted-triangle limit bounds the work actually
done rather than the work of one copy each.

**Statistics.** `instances` counts the drawn copies next to `meshes`, so
30 instances of 24 meshes cannot be read as 30 meshes. It is counted from
the draw loop and not from the scene: a copy is counted when it actually
submits a triangle, so a backend that skipped a copy reports fewer of them
rather than describing a frame it did not draw, and a copy of a mesh whose
every material group is undrawable is not counted at all while `meshes`
still reports the mesh. It splits into `exact` and `stand_in`: a copy is a
stand-in when the profile could not derive the transform the retail draw
path applies and placed it from a documented substitute instead. A profile
with no instances counts each mesh as one exact copy, because a mesh drawn
in its own authored space is not a placement claim at all. The viewer
prints the line for the stock car only, so every other report is byte for
byte what it was.

**What the frame does not claim.** With `--place-wheels` the wheel and
brake copies are stand-ins, and the render says so three times: the slot
lines report the placement as `wheel table`, `stock wheels:` names the
culling placements as a stand-in and not the matrix the game draws them
with, and `instances:` counts them apart. The picture shows what those
matrices give and nothing more: the lateral component is a term
`docs/PART_PLACEMENT.md` traces but does not reproduce, and the spin and
steering angles are not there at all. The half turn wheels 0 and 3 take
and the `(1, -1, 1, 1)` reflection brakes 0 and 3 take are there since
issue #161; before it one side's copies faced the wrong way, and a camera
on the car's left saw each left wheel's backing quad in front of its rim.
Without the option neither slot contributes a copy, and the frame holds no
wheel at all rather than a wheel in a place this project cannot defend.

## Frames and camera

- World frame: `world = (px, py, -pz)`, Y up, right-handed, source triangle
  order. This is the same mapping as the OBJ export. +X is the car front,
  +Z is the car's right side.
- Orbit camera: `eye = target + distance * (cos p cos y, sin p, cos p sin y)`
  with yaw `y` and pitch `p` in degrees. At yaw 0 the camera looks at the
  front from +X. At yaw 90 it looks at the right side from +Z. Positive
  pitch looks down from above. Pitch is limited to +-89 degrees so the
  up vector (+Y) never lines up with the view direction.
- Look-at: `forward = normalize(target - eye)`,
  `right = normalize(forward x +Y)`, `up = right x forward`.
- Projection (D3D-style depth):
  `clip = (vx * fx, vy * fy, (vz - near) * far / (far - near), vz)`, where
  `(vx, vy, vz)` are view coordinates along right, up and forward,
  `fy = cot(fov_y / 2)` and `fx = fy * height / width`. After division,
  x and y are in [-1, 1] inside the frame, and depth is 0 at the near plane
  and 1 at the far plane.
- Viewport: `sx = (x/w + 1) / 2 * width`, `sy = (1 - y/w) / 2 * height`,
  y down. Pixel `(i, j)` covers `[i, i+1) x [j, j+1)` and is sampled at its
  center `(i + 0.5, j + 0.5)`. Frame bounds are pixels `0..width-1` and
  `0..height-1`, and nothing outside them is ever addressed.
- Sine and cosine come from fixed polynomials on degrees reduced to +-45
  (`det_sin_deg`, `det_cos_deg`). Multiples of 90 degrees are exact, and
  results do not depend on the C library.

## Pipeline

1. The prepared draws are drawn in order: selected meshes in file order,
   and every material record in record order. With `--material-state
   retail` they are reordered into the retail draw queue order described
   under "Draw order" below; every group is still drawn exactly once, so
   the submitted triangles and every counter are unchanged and only the
   triangle ids move with the groups. With no selector, invalid
   meshes are skipped and counted. A draw is drawn only if it is
   `drawable`: its index range is a multiple of 3 inside the mesh index
   stream and its texture slot is inside the mesh hash list. Otherwise the
   group is counted as invalid and skipped. The texture binding is the one
   fixed at preparation. Each triangle gets an id: the running number of
   submitted triangles.
2. Indices are checked against the vertex count again. Triangles with
   repeated indices or zero world-space area are kept and counted
   (`degenerate_source`). They go through the pipeline and produce no
   pixels.
3. Clipping in clip space (Sutherland-Hodgman) against `w >= near` and a
   guard band `|x|, |y| <= 8 w`. This yields a convex polygon of at most 8
   vertices, drawn as a fan. Each intersection is computed from the inside
   vertex toward the outside one, so an edge shared by two triangles is cut
   at bit-identical points. A triangle with 0, 1 or 2 vertices in front of
   the near plane gives 0, 1 or 2 triangles.
4. Snapping: screen positions are clamped to the guard band and rounded to
   8 fractional bits with `floor(s * 256 + 0.5)`. With at most 8192 pixels
   per axis, fixed values stay below 2^24 and edge products below 2^50, so
   the 64-bit edge functions cannot overflow. Every float-to-int conversion
   happens on a value whose range was checked first.
5. Coverage: the edge function
   `E(a, b, p) = (bx - ax)(py - ay) - (by - ay)(px - ax)`. A zero area in
   fixed point is degenerate. A negative area (counter-clockwise on screen,
   i.e. the source front face) is counted as front, a positive area as
   back, and the vertex order is then normalized. The top-left fill rule
   decides samples exactly on an edge: top edges are horizontal with
   `dx > 0`, left edges have `dy < 0`. Adjacent triangles cover each shared
   sample exactly once. The bounding box is clamped to the frame.
6. Faces: both sides are rendered by default (`--cull none`), because 43
   source triangles wind against their normals and thin parts are
   single-sheet. `back` and `front` culling exist for diagnostics.
7. Depth: `z/w` is affine in screen space and is interpolated with the
   exact integer barycentrics. A fragment with `z/w > 1` is discarded
   (far). The test is `z < stored || (z == stored && id > stored_id)`,
   so an exact tie goes to the larger triangle id. This matches a
   less-equal test drawn in source order, but the result does not depend
   on the order the triangles are drawn in. The depth buffer is f64 and is
   cleared to 1.
8. Attributes: u, v and vertex color are interpolated perspective-correctly
   as `sum(l_i a_i / w_i) / sum(l_i / w_i)`.
9. Sampling: nearest texel of the decoded top mip level, repeat wrap on
   both axes: `f = t - floor(t)`, `texel = min(floor(f * size), size - 1)`.
   Negative and large UVs wrap. A non-finite coordinate samples texel 0.
   UVs are raw by default. `--flip-v` samples `(u, 1 - v)` and is printed
   in the report as `flip_v 1`.
10. Alpha policy. `--material-state off`, the default, is the historical
    behaviour: there is no blending, every texel's decoded RGB is drawn
    opaquely and texel alpha reaches nothing but `--alpha-cutoff`. Written
    pixels always get alpha 255, in both modes. `--alpha-cutoff N` (for
    example 128) is a diagnostic in either mode: a textured fragment whose
    texel alpha is below N is
    discarded before the depth test. This covers DXT1 punch-through
    (alpha 0), DXT3 4-bit alpha and A8R8G8B8. With a cutoff, glass and
    other translucent parts come out either opaque or absent.
    `--material-state retail` additionally applies the state recovered in
    `docs/MATERIALS.md`; see "Retail material state" below. In both modes,
    geometry flags are never guessed as alpha state: the alpha test and the
    blend come from the bound texture-info record and from nothing else.
11. Color: texel RGB, optionally times the vertex color bytes 0..2
    (`(c * vc + 127) / 255`, `--vertex-color`), then times the flat shade
    factor `(c * k + 128) >> 8`. With `--shading flat` (default),
    `k = round(256 * (0.35 + 0.65 * max(0, n . L)))`, where `n` is the
    world-space geometric normal of the source triangle turned toward the
    eye, and `L = normalize(0.35, 0.85, 0.40)`. With `--shading none`,
    k = 256.
12. Fallback material: a group whose hash is in no source (`unresolved`),
    or that matches several or only invalid textures of the winning source
    (`unusable`), is never drawn with another texture. It uses a hash-derived color:
    channels are `64 + byte * 160 / 256` of a 32-bit integer mix of the
    hash, in 64..223. An 8-per-UV-unit checker at 3/4 brightness marks it
    visibly as a placeholder. Fallback fragments are opaque.
13. Output: RGBA8, background `40, 44, 52, 255`, encoded in memory with
    `nfsu::write_tga` (32-bit, top-left origin) and written by
    `write_frame_tga`. An output path equal to an input is refused, and so
    is any existing target. The existence check does not follow a final
    symlink, so a dangling link counts as existing. Then:
    - `<out>.partial` is created exclusively, without following a link at
      that name. Windows uses `CreateFileW` with `CREATE_NEW |
      FILE_FLAG_OPEN_REPARSE_POINT`. The handle allows read sharing only,
      with no write or delete sharing. POSIX uses `open(O_CREAT | O_EXCL |
      O_NOFOLLOW)`. If any entry already exists there, including a live or
      dangling symlink, the write fails and that entry is left alone.
    - The data is written and flushed. It is then published under the
      target name with an operation that cannot replace an existing entry.
      Windows renames through the still-open handle
      (`SetFileInformationByHandle(FileRenameInfo)`, `ReplaceIfExists =
      FALSE`). POSIX calls `link(partial, target)`, then
      `unlink(partial)`. A target that appears at any time before
      publication makes the write fail and is left untouched.
    - On failure the partial is removed. Windows deletes it through its own
      handle (`FileDispositionInfo`), so only that file can be removed.
      POSIX uses `rollback_created_files`. The error ends with the same
      "removed all N / removed K of N ...; could not remove: ..." summary as
      the texture export, and `not_removed` lists what is left.
    - Support boundary: `frame_writer_supported()` is true on Windows and
      POSIX. On any other platform the writer refuses to write.
    - POSIX residuals: removing the partial and the `link()` step act on
      names. A process that can modify the output directory could swap the
      partial for its own file in that window. The swapped-in file might
      then be published or unlinked, but no existing target is ever
      replaced. `link()` also needs a filesystem with hard links; without
      them the write fails closed and nothing is published.
    - Tests: the Windows path was exercised here, including live and
      dangling symlinks at the partial and target names. The POSIX branch
      was not compiled or run on this machine. Issue #132 made it compile
      on Linux, where `nfsu_render_tests` now runs it, symlink cases
      included (GCC 13.3, Ubuntu 24.04).

## Limits

All limits are checked before allocation or before the work starts:

- frame 1..8192 per axis and at most 16777216 pixels (17 bytes per pixel:
  RGBA8, f64 depth, u32 id and the u8 blended mark; at most 272 MiB)
- at most 2^22 source triangles, 2^24 submitted triangles (over all
  drawable ranges, which may overlap) and 2^24 raster triangles
- at most 2^32 pixel tests per frame
- prepared scene (`SceneLimits`): at most 256 selectors and 16384 selector
  name bytes, 65536 draws, 2^22 vertices, 2^22 triangles, 2^24 triangles
  over all drawable ranges and 128 MiB of copied texture RGBA8
- camera values as in the option table
- decoded textures: at most 128 MiB of retained RGBA8 top levels in total,
  over all texture sources (`RenderTextureLimits::max_total_bytes`)
- texture input files (`nfsu_viewer`): at most 8 `--shared-textures` files,
  each at most 256 MiB, and at most 512 MiB read for `TEXTURES.BIN` and all
  shared files together, checked before each file is read

Exceeding a limit returns an error. There is no partial screenshot: the
TGA is written only after preparation and rendering succeeded, and a
selection error exits before any texture is read.

Texture decoding (`decode_render_textures`) separates two kinds of problem:

- A texture whose own data is bad (invalid record, JDLZ or level error) is
  kept with `valid == false` and a problem message. Its materials use the
  fallback, the viewer prints the problem, and rendering continues.
- A fatal condition makes the call return false with an error and an
  empty set, and the viewer exits with 2. Fatal means either that retaining
  the next texture would exceed the aggregate limit, or that an allocation
  failed (`std::bad_alloc` is caught). The limit is checked before that
  texture's allocations, using its full top-level size
  (`width * height * 4`). A texture that later fails to decode retains
  nothing and does not count toward the total. Its size is still checked
  first, because the failure cannot be known in advance.

The retail 240SX pack retains 10437632 bytes.

## Retail material state

`--material-state retail` applies the material render state that
`docs/MATERIALS.md` recovers, and only that. Everything below is either a
traced rule or an explicitly bounded reference decision; nothing is
inferred from an untraced material flag.

### Where the state comes from

The retail draw path reads the alpha test, the blend and the draw queue row
from the 124-byte texture-info record of the bound texture, not from the
geometry material record. The reference does the same: `RenderTexture`
carries the four raw bytes info +81, +84, +85 and +86 unchanged,
`prepare_car_scene` copies them with the texture, and the renderer reads
them off the draw's own bound texture. A group whose binding is
`unresolved`, `unusable` or `invalid_slot` has no bound record, therefore no
proven state; it stays opaque and unblended and takes row 0. The fallback
checker is never alpha-tested and never blended.

Raw values are kept raw. Each decoded rule lives in one place
(`render_alpha_test_enabled`, `render_blend_mode_of` and `render_draw_layer`
in the prepared-scene layer, mirroring `texture_alpha_test_enabled`,
`texture_alpha_blend` and `texture_draw_layer` in the pack reader); tests
check the alpha and blend decoders agree on all 256 byte values and the row
decoders on all 65536 pairs.

The only material-record field the retail state reads is `flags`, and only
as the two branch conditions of the coefficient gates. The untouched
60-byte record and every other decoded field are carried and never
interpreted.

### Alpha test

Enabled only when +85 == 1. The comparison is `D3DCMP_GREATER` against
`D3DRS_ALPHAREF` 0xAA, so a fragment survives only when its source alpha is
strictly above 0xAA. It is applied before the depth test and counted in
`alpha_discarded`, the same counter `--alpha-cutoff` uses. A textured
fragment must pass both tests; with the default cutoff 0 only the retail
test can reject anything.

### Blending

+86 selects the blend: 0 none, 2 `SRCALPHA`/`ONE`, every other nonzero
value `SRCALPHA`/`INVSRCALPHA`. In 0..255 integers, with `a` the source
alpha and `d` the destination channel:

```text
source-over: (src * a + d * (255 - a) + 127) / 255
additive:    min(255, (src * a + 127) / 255 + d)
```

The destination alpha stays 255: the frame is an opaque screenshot and no
traced state makes the car write a meaningful destination alpha.

The source alpha is the texel alpha times the effect's `diffuse_scale.w`,
which is an undecoded material coefficient. `--material-alpha MIN,RANGE`
supplies it as `MIN + vdotn * RANGE`; its default 1,0 is the identity, and
the reference never invents a retail value for it. The one case where the
retail value is known is a closed diffuse gate, which zeros the whole
`DiffuseMin` and `DiffuseRange` vectors including their W lane; the source
alpha is then 0 whatever `--material-alpha` says.

### Draw order

Blending is the one part of the state that depends on the order draws reach
the frame, so the order is fixed before blending is enabled. It is not a
policy the reference chose: it is the order the retail draw queue produces,
recovered in `docs/MATERIALS.md`.

The retail queue is a fixed 16 by 7 table of buckets. A material is
appended to the row its texture's info bytes +84 and +81 select and the
column its effect selects, and the table is later walked row by row, each
bucket draining in the order it was filled. Every material of one car uses
the same effect, so the column is constant and the order reduces to three
rules:

1. ascending row, `8 * (info[+84] != 0) + (signed) info[+81]`;
2. inside a row, the textures in the order each was first used, because the
   queue keeps one entry per texture and a later material with the same
   texture joins the entry the first one opened;
3. inside a texture, the material records in record order.

`nfsu_render` reproduces that with a stable sort of the prepared draws by
(row, first use of the texture). Rule 3 then falls out of the sort being
stable, because the prepared order is already mesh source order and then
record order. A group with no resolved texture has no row of its own and
takes row 0, which is the row a record with both bytes zero selects.

The sort itself is `prepared_draw_order` in the scene layer
(`include/nfsu/scene.h`), not in the rasterizer, because the order is a
property of the prepared scene and of the material state and nothing about
it is specific to a CPU. `render_scene` calls it, and so does anything that
has to reproduce the triangle ids this render assigns: ids are the running
number of submitted triangles over that order, which
`prepared_triangle_ranges` turns into one id range per drawable group. That
is what lets the inspection window name the group that drew a pixel
([`INSPECTOR.md`](INSPECTOR.md)), and what a deko3d backend has to follow to
get the same. Moving it changed no frame: a `nfsu_viewer` built at the
previous head produces an identical TGA, stdout, stderr and exit code for
the default options, for `--material-state retail` with coefficients, for a
runtime skin composite and for a mesh subset with the other render options.

Two details of the reproduction are reference decisions, recorded here
rather than claimed as retail:

- First use is established by every group, including one that is not
  drawable. The retail append has no drawable test either, so this is the
  closer match, but no case was constructed where the two differ.
- A group with no resolved texture is placed in row 0 and forms its own
  texture group unless another unresolved group names the same hash. Retail
  always has a texture-info record, so it never reaches this case.

Two consequences are worth stating because they are not what a renderer
would choose on its own:

- The row comes from the texture, not from whether the material blends. An
  opaque texture can sit in a later row than a blended one, and the 240SX
  pack does exactly that: its rows are 0 (67 opaque records), 5 (18 blended),
  8 (8 records, 7 of them opaque) and 13 (26 blended).
- A blended car draw writes depth. `SetRenderState(D3DRS_ZWRITEENABLE,
  TRUE)` is on both the opaque and the blended branch for the car effect;
  the FALSE store is reachable only for a non-car effect. So a blended
  surface does occlude whatever is drawn after it and is farther away, and
  the reference does the same.

Rule 2 and the depth writes both make the frame depend on the order, which
is why the order had to be recovered before blending was enabled rather
than chosen. What makes the frame right is that the order is the retail
one, not that the frame is insensitive to it.

`blend: ... overlap O` reports how much transparency actually stacks: it
counts every blended fragment written onto a pixel a blended fragment had
already written. It is a diagnostic, not a proof of order independence.
With depth writes on, the order also decides which blended fragments reach
the frame at all - a blended draw in front of a later blended one rejects
it outright - so `O == 0` does not by itself mean a different order would
give the same frame.

The default "all meshes" profile draws every kit and style variant at once,
so it stacks transparent surfaces the game would never install together and
`O` is large. That is a property of the profile, not of the ordering.

### The fragment, in order

A backend reproducing a frame needs the exact chain. For a textured
fragment that survives both alpha tests and the depth test:

1. `c = texel.rgb`, and with `--vertex-color`, `c = (c * vc + 127) / 255`.
2. `c = (c * shade + 128) >> 8`, clamped to 255, where `shade` is the flat
   shade factor of step 11 above. With a closed diffuse gate this step
   yields 0 instead, because the effect's whole output is zero.
3. The environment term is added to `c` and the sum is clamped to 255. It
   is added after the shade, so the reference's diagnostic shading does not
   scale it; only the texel alpha and the coefficients do.
4. The blend runs last, with `c` as `src` and the frame as `d`.
5. The frame's alpha byte is set to 255 and the depth and the owning
   triangle id are written.

The source alpha used in step 4, and in the alpha test before it, is the
texel alpha times the alpha lane, never the shaded or environment-modified
colour.

### Coefficient and environment gates

The traced coefficient upload zeros whole coefficient vectors before the
effect sees them, on two branch conditions of the material record flags.
`material_coefficient_gates(flags, environment_enabled)` reproduces exactly
those two branches and reports which of the diffuse, specular and
environment groups survive. It names no flag bit, and nothing else in the
renderer reads `PreparedDraw::flags`.

`--environment-global off` stands for the runtime global the upload also
tests.

The reference models the environment group and the alpha lane of the
diffuse group, because it models neither the effect's harmonic diffuse
lighting nor its specular term. A closed diffuse gate therefore zeros the
source alpha, and with it the environment term, which the identity below
scales by `diffuse_scale.w`. When the environment gate keeps the group and
the diffuse gate is open, a textured fragment gains

```text
environment_term[k] =
    4 * diffuse_scale.w * environment_color[k] *
    (EnvmapMin[k] + pow(vdotn, EnvmapPower) * EnvmapRange[k]) *
    texel_alpha / 255
```

rounded to the nearest integer and clamped, which is the effect's
environment identity written in 0..255 units. `--environment MIN,RANGE,POWER`
and `--environment-color R,G,B` supply `EnvmapMin`, `EnvmapRange`,
`EnvmapPower` and the cube sample. Their defaults make the term zero. The
alpha lane's default 1,0 does not vanish: it is the identity, which is an
assumption about an undecoded coefficient, not a retail value.

Two reference decisions are recorded here rather than claimed as retail:

- The effect evaluates `vdotn` per vertex from the stored vertex normal.
  The reference evaluates it once per triangle, from the geometric face
  normal turned toward the eye and the direction from the triangle's first
  vertex to the eye. That is the same flat model the shade factor uses, and
  the same facing test.
- `EnvmapPower` is a float in the effect. The reference accepts only a
  whole number and evaluates it by repeated multiplication, so the frame
  never depends on a C library `pow()`.

### What the retail state does not change

With `--material-state off` nothing above runs: the frame, the report and
every counter are byte for byte what an earlier build produces. The
material coefficient options are refused in that mode, so the default can
never be changed by them by accident.

## Determinism

- Arithmetic is IEEE double, with `FLT_EVAL_METHOD == 0` enforced at
  compile time. `nfsu_render` is built with `-ffp-contract=off
  -fno-fast-math` (GCC/Clang) or `/fp:precise` (MSVC), so FMA contraction
  cannot change results.
- Only `+ - * /`, `sqrt`, `floor` and `fmod` are used, and all of them are
  correctly rounded or exact. No libm transcendental is used.
- Rendering is single-threaded and in a fixed order. Coverage uses integer
  edge functions. Depth ties are broken by triangle id.
- Byte identity is verified for repeated runs of one build. Identity
  across compilers and CPUs is expected but has not been measured; only
  MinGW GCC 13 on x86-64 was available.

## Statistics

`meshes` (rendered, skipped invalid), `material groups` (resolved,
unresolved, unusable, invalid), `unresolved hashes` (distinct),
`triangles submitted` (textured, fallback), `bad_index`,
`degenerate_source`, `outside` (fully clipped), `clipped` (partly clipped),
`degenerate_screen`, `culled`, `front`, `back`, `rasterized` (source
triangles sent to coverage), `raster_pieces` (fan triangles), `pixel tests`,
`fragments` (covered samples), `alpha_discarded`, `far_discarded`,
`depth_failed`, `written`, `pixels drawn` (pixels owning a triangle),
`fallback` (drawn pixels owned by fallback triangles) and `drawn bounds`
(inclusive).

With `--stock-car` one more line follows the block above: `instances: N
exact E stand_in S`, the copies of those meshes that actually submitted a
triangle and how many of them the profile placed from a stand-in. It is
printed for that profile only, where a mesh can have several copies; every
other profile draws each mesh once, so the line would add nothing there
and is left out to keep those reports unchanged. The `stock ...` lines
that precede it are described under "The stock car profile".

With `--material-state retail` three more lines follow: `draw rows:` with
one count per draw queue row, in row order, over the drawable groups;
`material groups: alpha_tested B blended C`; and `blend: fragments F
overlap O pixels P`. Nothing is printed for them in the default mode, so
the report of a run that does not select the retail state is byte for byte
what it was before the option existed.

## Retail 240SX reference screenshot

Command, with default options (1280x720, yaw 35, pitch 15, distance 7,
target 0.2,0.35,0, fov_y 35, near 0.05, far 100, flip_v 0, alpha_cutoff
0, vertex_color 0, cull none, shading flat):

```text
nfsu_viewer <CARS/240SX/GEOMETRY.BIN> <CARS/240SX/TEXTURES.BIN> build/shots/a.tga
```

### Observed

Sources: GEOMETRY.BIN is 4780816 bytes, SHA-256
`002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`.
TEXTURES.BIN is 697340 bytes, SHA-256
`725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA`.
Both were unchanged after rendering.

- Two runs gave byte-identical TGA files: 3686418 bytes, SHA-256
  `6580372DEF46E928E3D6B4B25ADB49FEBB00EBC50BA5820E6465833FC966EDAD`.
  The reports differed only in the output file name.
- Report: 119 textures decoded, retaining 10437632 bytes. Eye
  `5.7386808066535675,2.1617333157176453,3.878226052556263`. The run drew
  400 meshes (none skipped) and 1242 material groups: 174 resolved, 1068
  unresolved, 0 unusable, 0 invalid. There were 25 unresolved hashes.
- Triangles: 78742 submitted (8000 textured, 70742 fallback). 130 were
  degenerate at the source and 158 degenerate on screen. None were
  outside, clipped, culled or had a bad index. 43760 were front-facing,
  34824 back-facing and 78584 rasterized.
- Coverage: 49100702 pixel tests and 11832902 fragments. Of the fragments,
  0 were alpha-discarded, 0 far-discarded, 11009390 depth-failed and
  823512 written.
- 148173 pixels drawn, 144209 of them owned by fallback triangles. The
  drawn bounds are x 378..991, y 199..589, well inside the frame, so the
  car is not clipped. A count made directly from the TGA bytes (pixels
  that differ from the background) gives the same number and bounds.
- Diagnostic `--alpha-cutoff 128` on the same view: 35584 fragments
  discarded, 823404 written, 148173 pixels drawn, 144285 of them
  fallback. The alpha test therefore removes textured surfaces here, and
  the pixels behind them show fallback or other geometry. In this view
  that affects 76 pixels.
- The image shows the car from the front right, above, with the outside
  surfaces visible. Every kit and style variant is drawn at once because
  every valid mesh is rendered. Alternative spoilers, bumpers and lights
  therefore overlap.
- UV orientation: in a close-up of the front bumper (`--yaw 0 --pitch 3
  --distance 1.0 --target 2.2,0.42,0 --fov 12`), raw UVs show the
  `240SX_BADGING` lettering "NISSAN" upright and reading left to right.
  With `--flip-v` the same area is scrambled. The readable text also shows
  that the image is not mirrored.
- The UV and winding observations below were recorded while the default
  cutoff was still 128. The badge lettering was visible with that cutoff,
  and texture orientation does not depend on the alpha policy. The
  culling counts are triangle counts, which alpha does not change.
- Winding: at 640x360 with the default camera, `--cull back` removes 34820
  triangles and keeps the closed exterior (37039 pixels). `--cull front`
  removes 43764 and shows the underside and interior instead (36281
  pixels). Counter-clockwise on screen is therefore the outward face for
  almost all triangles, and the default two-sided image is not inside-out.
- Vertex colors: among the 98215 vertices there are 241 distinct values.
  45680 are `FF FF FF FF`, 38560 are `00 00 00 FF`, and the most common
  other values are greys with R = G = B.

### With GLOBALB.BUN as a shared source

Command, with the same default options:

```text
nfsu_viewer <CARS/240SX/GEOMETRY.BIN> <CARS/240SX/TEXTURES.BIN> build/shots/g.tga --shared-textures <GLOBAL/GLOBALB.BUN>
```

Observed:

- GLOBALB.BUN is 2361176 bytes, SHA-256
  `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`. All
  three inputs were unchanged after rendering.
- Two runs gave byte-identical TGA files: 3686418 bytes, SHA-256
  `0F7FBEE24178ED35AD46EA05F02C8151D7642AE2E71FDEB6CA235DA7585EBF7B`.
  The reports differed only in the output file name.
- Source 1: 2 packs, 83 records, all valid, complete, with no issues. The
  parser accepts the 16 P8 records and can extract their index chains and
  palettes, but none of their hashes is wanted by this geometry. 61 distinct
  hashes are used by valid meshes. 8 GLOBALB textures were decoded (409600
  bytes, 10847232 in total with the car's pack), 75 records were not wanted,
  and none were shadowed or blocked. The command exits 0.
- 518 material groups resolved (174 from the car's pack, 344 from
  GLOBALB), 724 unresolved, 0 unusable. 17 unresolved hashes remain; the
  most used is 0x77A20245 (`240SX_SKIN1`, the formatted body-skin fallback,
  not in either file).
- Triangles: 17892 textured (8000 + 9892), 60850 fallback. Coverage and
  depth statistics are the same as the local-only run, because the
  geometry and camera are the same. 148173 pixels drawn, 118557 of them
  fallback (144209 without GLOBALB).

After P8 structural support, the command was run again without selectors.
Its TGA hash remains the one listed above; the source report now counts all
83 GLOBALB records as valid.

### Explicit subset

A diagnostic subset, not the stock car, with the same default options and
GLOBALB.BUN, selectors given out of source order:

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/shots/s.tga --shared-textures <GLOBAL/GLOBALB.BUN> --mesh-name 240SX_KITW01_BODY_A --mesh-index 4 --mesh-name 240SX_STYLE01_BRAKELIGHT_A --mesh-index 0
```

- Selected, in source order: mesh 0 `240SX_STYLE15_SPOILER_A` (selector
  3), mesh 4 `240SX_STYLE14_HOOD_A` (selector 1), mesh 83
  `240SX_STYLE01_BRAKELIGHT_A` (selector 2), mesh 91
  `240SX_KITW01_BODY_A` (selector 0).
- Two runs gave byte-identical TGA files and reports: 3686418 bytes,
  SHA-256
  `E82F3F5EE3CD508E582697BBD148FC5C83090982D12CABBE6586619BFDF52004`.
- 14 material groups (8 resolved, 6 unresolved), 1 unresolved hash, 2952
  triangles (539 textured, 2413 fallback), 127804 pixels drawn (96654
  fallback). 9 hashes were wanted from GLOBALB and 4 of its textures
  decoded.

### Retail 240SX stock car

The stock assembly rather than a diagnostic view: every installed part at
LOD A, each where the recovered placement puts it. Same default options,
three geometry sources and two shared texture sources:

```text
nfsu_viewer <CARS/240SX/GEOMETRY.BIN> <CARS/240SX/TEXTURES.BIN> build/shots/stock.tga --shared-textures <GLOBAL/GLOBALB.BUN> --shared-textures <CARS/TEXTURES.BIN> --stock-car 240SX --stock-parts <GLOBAL/GLOBALB.BUN> --stock-geometry <CARS/BRAKES/GEOMETRY.BIN> --stock-geometry <CARS/PLATES/GEOMETRY.BIN>
```

Observed:

- Two runs in separate processes gave byte-identical TGA files, reports
  and empty standard error: 3686418 bytes, SHA-256
  `D68419B0CBB63BE7A209E5CC82BD94F681304C36D187B01C8D8DB240B0DD4E0F`. The
  reports differed only in the output file name. All five inputs were
  unchanged after rendering.
- 25 slots are installed, 24 of them have a model and 22 draw: every body
  part at the car's own matrix, the exhaust and the licence plate at their
  markers, and the wheel and brake slots reported `placement unreproduced`
  and drawing nothing. 16 markers were collected, one exhaust and one
  licence plate among them.
- 22 meshes over the three sources, 74 material groups (15 resolved, 59
  unresolved, 0 unusable, 0 invalid), 12 unresolved hashes, and
  `instances: 22 exact 22 stand_in 0`.
- 4025 triangles submitted (498 textured, 3527 fallback), 110866 pixels
  drawn of which 107050 are fallback, drawn bounds x 390..973, y 232..545.
- Against the all-meshes reference at the top of this section, which draws
  400 meshes and 78742 triangles: one kit, one style and one of each light
  and decal, so nothing overlaps. The image is one car.
- 22 texture hashes were wanted. 4 came from GLOBALB.BUN; `Cars/TEXTURES.BIN`
  holds 353 records and claims none of them. Most visible surfaces are
  still fallback-colored, for the reason the all-meshes run gives: the body
  skin is composed at runtime and needs `--skin-car`. Adding
  `--skin-car 240SX --skin-vinyls <CARS/240SX/VINYLS.BIN> --skin-doorline
  plain --skin-base 30 60 160 128 --material-state retail` takes the same
  74 groups to 47 resolved and the 4025 triangles to 3792 textured, and
  leaves 33795 fallback pixels of 110866.

With `--place-wheels` added, the wheel and brake slots draw four copies
each from the culling placements:

- 24 slots draw, from 24 meshes, in 30 instances: `instances: 30 exact 22
  stand_in 8`. 79 material groups (20 resolved), 6137 triangles submitted,
  116746 pixels drawn, drawn bounds x 390..973, y 232..552. SHA-256
  `E7A042A560E807EE74595449275FFBDC1AE0A0BF321CE06EEF434BC5B1C8274A`.
- The four copies of each land under the four arches, which is the
  confirmation recorded under "Instances" above. They are a stand-in: the
  lateral term, the spin and the steering of the retail matrix are not
  reproduced. The report says so on its `stock wheels:` and `instances:`
  lines, and the 8 stand-in copies are exactly the two slots' four copies
  each. The left pair of each is half-turned or reflected as the retail
  draw does it (issue #161).
- Issue #161 re-pinned this frame. It was
  `FD6727333A94803A97FD26282CD89DF6F3D3295E0231F4A871A1B14089D28CE9`, with
  116748 pixels drawn, while the left wheels were pure translations: the
  front-left wheel's backing quad then sat 0.2 outboard of its rim and
  showed as two black pixels at x 931..932, y 385, above the far fender.
  Those two pixels are background now and nothing else in the frame
  changed. The report's `triangles front` and `back` counts move from 3553
  and 2576 to 3061 and 3068, because the turned wheels now face the other
  way and the reflected brakes are wound the other way; `pixel tests` and
  `fragments` move with them.

### Runtime body skin

Read-only, with an explicit selection, mesh 259 `240SX_KIT00_BODY_A` at
640x360, the car pack plus `Global/GLOBALB.BUN` and `Cars/TEXTURES.BIN`:

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/shots/skin.tga --shared-textures <GLOBAL/GLOBALB.BUN> --shared-textures <Cars/TEXTURES.BIN> --skin-car 240SX --skin-vinyls <Cars/240SX/VINYLS.BIN> --skin-vinyl 0 TRIBAL5 16 no-remap --skin-doorline plain --skin-base 200 30 30 128 --skin-special 0 255 200 0 128 --mesh-name 240SX_KIT00_BODY_A --width 640 --height 360
```

- The target resolved to `DUMMY_SKIN1` `0x530B82B0` in `Cars/TEXTURES.BIN`,
  record 120, format code 0x20 / D3DFORMAT 21, 512x512, one mip level. The
  path taken from that record is type 32, object type byte 0x20, not
  expanded. The generated texture is 512x512, 1048576 bytes.
- Two layers were assembled, owning 4198400 bytes: `240SX_TRIBAL5` with
  `240SX_TRIBAL5_MASK` expanded from `VINYLS.BIN`, and `240SX_DOORLINE`
  with `240SX_DOORLINE_MASK` passed through from the car's pack.
- It was bound under `0x530B82B0` and `0x77A20245`, displacing nothing,
  because no pack record claims either hash.
- The same command at the previous commit reported 3 material groups, all
  unresolved, 804 triangles all fallback and 16432 pixels all fallback. With
  the runtime skin all 3 groups resolve, all 804 triangles are textured, no
  hash is left unresolved and no pixel is fallback.
- Two runs gave byte-identical screenshots and byte-identical generated
  textures. All five retail files were unchanged before and after
  ([`RUNTIME_SKIN.md`](RUNTIME_SKIN.md) lists their hashes) and nothing was
  written into the installation. The derived images were deleted after
  checking.

### Inferred

- Raw UVs with a top-left texture origin, rows as decoded, are correct for
  this car. This matches the Direct3D convention, so `--flip-v` stays off.
- Most visible surfaces are fallback-colored because the body skin
  (0x77A20245) and other shared textures live outside the car's pack.
  With GLOBALB.BUN the underside (0xBD1F0E0A), carbon (0x721AFF7C) and six
  more resolve. The body skin does not, because no record claims that hash
  anywhere; it needs `--skin-car`, which composes the skin and binds it
  under that hash as well (see "Runtime body skin" above).
- Vertex colors look like a grey mask or baked term rather than a color
  tint. Multiplying by them would turn about 39% of vertices black, so
  `--vertex-color` is off by default.

### Unknown

- How the game combines vertex colors and the material/effect objects. The
  material-key slot and 0xFF sentinel are decoded and the flag-controlled
  coefficient gates are reproduced, but the coefficient values themselves,
  the harmonic lighting and the environment cube map are not decoded, so
  the effect's diffuse and specular terms are not reproduced at all.
- Which kit and style meshes the game draws together. The reference view
  draws all of them, which is also why the transparent overlap count is
  large on that view.
- Which of the three `car.efx` techniques a draw uses, and the per-draw
  cull mode the retail path sets from the blend byte, which the reference
  records but does not apply (`docs/MATERIALS.md`).
- Whether the POSIX writer branch behaves as designed. It was not built or
  run here.
- Coplanar decal meshes resolve their depth ties by triangle id. The game
  probably uses draw order or a depth bias.
