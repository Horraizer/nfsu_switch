# Reference renderer (`nfsu_render`, `nfsu_viewer`)

`nfsu_render` is a dependency-free CPU rasterizer written in portable C++17.
It is the deterministic reference for later GPU and deko3d backends: the
same inputs and options give a byte-identical frame. `nfsu_viewer` loads
`GEOMETRY.BIN`, `TEXTURES.BIN` and any explicitly named shared texture
files through `nfsu_core`, renders one frame
and writes it as a TGA screenshot. The screenshot is the acceptance
artifact. There is no window and no platform graphics API.

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> <out.tga> [--shared-textures FILE]... [--mesh-index N | --mesh-name NAME | --mesh-hash HEX]... [options]
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

Each option may be given once, except `--shared-textures` (at most 8
times) and the mesh selectors (see below). `--material-alpha`,
`--environment`, `--environment-color` and `--environment-global` are
refused without `--material-state retail`, because they would otherwise be
silent no-ops. Numbers are parsed with `std::from_chars`
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
H source S pack P texture R`, in source, pack, record and claim order). The `textures:` line still describes the car's
pack alone. Problems in shared sources are printed on stderr as `texture
source N [pack P] issue at ...`. The exit code is 3 if any source reported
an issue.

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
- Neither profile is the game's stock car. The stock part set, the mesh
  each part uses at each LOD and the fallback for a missing mesh are
  decoded (`nfsu::select_stock_parts`, `nfsu::resolve_part_models`, see
  `docs/CAR_SCENE.md`), but this renderer does not consume them yet, and
  part placement is still unknown; an explicit subset is a diagnostic view
  chosen by the user.

`PreparedCarScene` holds, in deterministic order:

- `profile`, the `selectors` as given, `source_mesh_count` and
  `skipped_invalid_meshes`;
- `meshes`: per selected mesh its source position, stored name, verified
  directory hash/range when present, the selector that chose it, source
  material count, and copies of its vertices and triangles (three u16
  indices each);
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
  selected meshes, ascending (the `unresolved hashes` statistic).

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
without overflow), and a drawn triangle total below 2^32 - 1. A scene that
fails is refused before a new frame is allocated. Vertex indices inside
triangles are checked per triangle while drawing and counted as `bad_index`,
as for source data.

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
      was not compiled or run on this machine.

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

### Inferred

- Raw UVs with a top-left texture origin, rows as decoded, are correct for
  this car. This matches the Direct3D convention, so `--flip-v` stays off.
- Most visible surfaces are fallback-colored because the body skin
  (0x77A20245) and other shared textures live outside the car's pack.
  With GLOBALB.BUN the underside (0xBD1F0E0A), carbon (0x721AFF7C) and six
  more resolve. The body skin still does not.
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
