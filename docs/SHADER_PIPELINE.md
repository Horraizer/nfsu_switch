# Offline shader pipeline

The Switch renderer's shaders are compiled when the project is built, never
while it runs. The GLSL lives in `shaders/`; the Switch build
([`SWITCH_BUILD.md`](SWITCH_BUILD.md)) compiles every stage with `uam`, the
deko3d shader compiler, into a `.dksh` blob, which is what deko3d loads.
deko3d has no shader compiler of its own, and nothing the application links
contains one.

Each shader program has a manifest beside its GLSL, `shaders/<program>.manifest`,
that states its whole interface: the vertex buffer layout, what the vertex
stage hands the fragment stage, every uniform block with its member offsets,
every texture binding, the colour outputs, and the blend and depth state of
every variant the program is drawn with. Three checks hold the manifest to
the things it describes, and each of them has been made to fail (see "How the
drift checks work").

No deko3d renderer exists yet. These shaders have been compiled and checked,
not drawn with; see "What the checks cannot catch" for what that leaves open.

## The variants

The variants are the ones the CPU reference renderer draws, read out of
`src/render.cpp` (`render_scene`) and `src/track_render.cpp`
(`render_track_draw_list`). Both paths decide per material group between the
group's resolved texture and the fallback checker, and the car path alone can
take the retail material state of [`MATERIALS.md`](MATERIALS.md). That gives
four programs:

| Program | The reference's draw | Vertex buffer | Stages | Variants |
| --- | --- | --- | --- | --- |
| `car_textured` | a car group whose binding is `resolved` | `nfsu::MeshVertex`, 36 bytes | `placed.vert.glsl`, `textured.frag.glsl` | six, below |
| `car_fallback` | a car group whose binding is `unresolved`, `unusable` or `invalid_slot` | `nfsu::MeshVertex` | `placed.vert.glsl`, `fallback.frag.glsl` | `opaque` |
| `track_textured` | a track draw-list entry whose group is `resolved` | `nfsu::TrackVertex`, 24 bytes | `placed.vert.glsl`, `textured.frag.glsl` | `opaque` |
| `track_fallback` | a track draw-list entry whose group is not | `nfsu::TrackVertex` | `placed.vert.glsl`, `fallback.frag.glsl` | `opaque` |

One vertex stage serves all four: `placed_vertex` and `track_placed_vertex`
compose the placement the same way, a row-vector transform over the raw slot
order `(px, pz, py)` mapped back to `(x, y, -z)`, and only the buffer layout
differs, which is pipeline state and lives in the manifest.

A textured car group's state comes from its texture-info record, and only
under `MaterialStateMode::retail`: byte +85 turns the alpha test on
(`render_alpha_test_enabled`, `GREATER` against `kRenderAlphaRef` 0xAA), and
byte +86 selects the blend (`render_blend_mode_of`). The two bytes are decoded
independently, so `car_textured` has one variant per pair:

| Variant | Alpha test | Blend | Selected by |
| --- | --- | --- | --- |
| `opaque` | off | off | the off state, and retail with +85 != 1 and +86 == 0 |
| `alpha_tested` | `GREATER` 170 | off | retail, +85 == 1, +86 == 0 |
| `source_over` | off | `SRCALPHA`, `INVSRCALPHA` | retail, +85 != 1, +86 not 0 or 2 |
| `additive` | off | `SRCALPHA`, `ONE` | retail, +85 != 1, +86 == 2 |
| `alpha_tested_source_over` | `GREATER` 170 | `SRCALPHA`, `INVSRCALPHA` | retail, +85 == 1, +86 not 0 or 2 |
| `alpha_tested_additive` | `GREATER` 170 | `SRCALPHA`, `ONE` | retail, +85 == 1, +86 == 2 |

The last two are pairs the reference applies and no surveyed retail record
reaches: every record [`MATERIALS.md`](MATERIALS.md) counted with +85 == 1 has
+86 == 0. They are covered because the reference would draw them, not because
the data does; `tests/shader_manifest_tests.cpp` derives the six pairs from
the two decoders over every byte value and requires exactly those.

Every variant of every program shares the rest of its state, because the
reference does: the depth test keeps a fragment whose depth is below the
stored one, or equal to it with a later triangle id, which in submission
order is `LESS_EQUAL`; every fragment written writes depth, blended ones
included; the frame's alpha is 255 in every pixel, so no draw writes the alpha
channel (`color_write=rgb`); and `RenderOptions` culls nothing by default.

### What each variant computes

`placed.vert.glsl` places the vertex with the drawn copy's transform and
projects it with the reference camera (`project_point`: a look-at basis and a
D3D-style projection, depth 0 at the near plane and 1 at the far one, and
`+y` at the top row of the frame). `textured.frag.glsl` and
`fallback.frag.glsl` are `Rasterizer::fill`, keeping the reference's integer
rounding where it has one: the nearest texel of the wrapped coordinate, the
alpha cutoff diagnostic, the vertex colour multiply, the flat shade factor,
the closed diffuse gate, the `car.efx` environment term with its constant
colour, and the source alpha the alpha test and the blend then use. The alpha
test and the blend themselves are the variant's fixed-function state, not
code in the stage.

What a backend puts in the uniform blocks, per the reference:

| Block, member | Value |
| --- | --- |
| `Frame.eye`, `.right`, `.up`, `.forward` | the `CameraFrame` vectors |
| `Frame.projection` | `focal_x`, `focal_y`, `near_plane`, `far_plane` |
| `Frame.light` | `(kLightX, kLightY, kLightZ)`, normalized |
| `Frame.options` | `flip_v`, `vertex_color`, 1 for `ShadingMode::flat`, `alpha_cutoff` |
| `Instance.rows` | `PreparedInstance::transform` as four rows, the identity for a car profile with no instances; for a track instance, `TrackTransform::linear` as rows 0 to 2 and `translation` as row 3 |
| `Material.fallback` | `fallback_color` of the group's hash, as bytes 0..255, and 1 in w for the checker pattern |
| `Material.alpha` | `alpha_min`, `alpha_range` when the reference evaluates the view term, else 1 and 0; 0 and 0 with 1 in z when the diffuse gate is closed |
| `Material.environment_min`, `.environment_range` | `EnvmapMin`, `EnvmapRange` when the environment gate keeps the term, else zero |
| `Material.environment_color` | the constant colour as bytes 0..255, and `EnvmapPower` in w |

The alpha reference 170 means the source alpha byte must exceed 0xAA. This
stage writes the source alpha as a byte over 255, so a backend that sets
deko3d's alpha reference to `170.5 / 255` rather than `170 / 255` keeps that
boundary where the reference has it even if the GPU's division is an ulp off.
That is guidance for the backend, not something checked here.

### Left out, deliberately

- The retail material state on the track path. `render_track_draw_list`
  refuses it: [`MATERIALS.md`](MATERIALS.md) proved that state for the car
  draw path, and [`TRACK_RENDER.md`](TRACK_RENDER.md) records that the track
  path is not known to share it. `tests/shader_manifest_tests.cpp` calls the
  function and fails if that ever changes.
- The retail per-draw cull mode (`D3DCULL_NONE` with +86 == 0, `D3DCULL_CW`
  otherwise). [`MATERIALS.md`](MATERIALS.md) records it as observed and not
  applied, and so does the reference. `RenderOptions::cull` is a diagnostic
  over every draw, which a backend sets as rasterizer state; it is not a
  material variant.
- What the reference does not decode: the `DEFAULTALPHA` skip, the effect's
  harmonic diffuse lighting and specular term, the `EnvMapTex` cube map (a
  constant colour stands in, as in the reference) and the choice among the
  three `car.efx` techniques.
- The runtime body skin. It is bound into the texture set as a
  `RenderTexture` and drawn as `car_textured`, so it needs no program of its
  own.
- The inspection window's diagnostic views ([`INSPECTOR.md`](INSPECTOR.md)).
  They recolour a CPU frame for a person at a desktop, and are not draws the
  renderer makes.
- Texture filtering beyond the nearest texel of the top level, which is all
  the reference decodes and samples.

## The manifest

A manifest is ASCII text, one record per line. `#` starts a comment; blank
lines are ignored; fields are separated by spaces. The first record is
`format 1`, the second `program <name>`, and the name must be the file's own
name without `.manifest`. Every other record is a kind followed by
`key=value` fields, in any order, each key once:

| Kind | Keys | Notes |
| --- | --- | --- |
| `stage` | `kind` `source` | `kind` is `vertex` or `fragment`, once each; `source` is a file beside the manifest. |
| `buffer` | `index` `stride` `struct` `header` | The C++ struct the buffer holds, and the header that declares it. |
| `attribute` | `location` `name` `type` `format` `buffer` `offset` `members` | `location` 0..15. `type` is what the stage declares; `format` what the buffer holds (`float32x1`..`float32x4`, `unorm8x4`, `snorm8x4`, `uint8x4`, `unorm16x2`, `snorm16x2`); `members` the C++ members, comma-separated, whose bytes it reads. |
| `varying` | `location` `name` `type` `interpolation` | `location` 0..31; `smooth`, `flat` or `noperspective`. |
| `block` | `name` `binding` `stages` `size` | A std140 uniform block, bound at `binding` in each stage listed (`vertex`, `fragment` or both, comma-separated). |
| `member` | `block` `name` `type` `offset` | In declaration order. `type` may be an array, as in `vec4[4]`. |
| `texture` | `name` `binding` `stage` `type` `filter` `wrap` | `sampler2D`; `nearest` or `linear`; `repeat`, `clamp_to_edge` or `mirrored_repeat`. |
| `output` | `location` `name` `type` `format` | A render target, 0..7; `rgba8`. |
| `variant` | `name` `alpha_test` `alpha_ref` `blend` `blend_src` `blend_dst` `blend_op` `depth_test` `depth_write` `color_write` `cull` | `alpha_ref` (0..255) exactly when `alpha_test` is neither `always` nor `never`; `blend_src`, `blend_dst` and `blend_op` exactly when `blend=on`. |

Types are `float`, `int`, `uint` and their `vec2`..`vec4` forms, and `mat4`
for a block member. Compare operations are `never`, `less`, `equal`,
`less_equal`, `greater`, `not_equal`, `greater_equal` and `always`; blend
factors `zero`, `one`, `src_color`, `one_minus_src_color`, `src_alpha`,
`one_minus_src_alpha`, `dst_alpha`, `one_minus_dst_alpha`, `dst_color` and
`one_minus_dst_color`; blend operations `add`, `subtract`, `reverse_subtract`,
`min` and `max`; `depth_write` is `on` or `off`, `color_write` `rgb` or `rgba`,
`cull` `none`, `back` or `front`.

A manifest that breaks a rule is refused with its file and line, and no check
runs on it. Beyond the grammar, the rules are: no two records claim one
location, binding, name or byte; an attribute lies inside its buffer's stride
and reads no more components than its format fetches; and each block's
members sit exactly where std140 puts them in the order given, with the block
size their end rounded up to 16 bytes, so a manifest cannot state an offset
the compiler would not produce.

A manifest name stands for the GLSL identifier `in` or `out` followed by the
name with each underscore-separated word capitalised: attribute `texcoord` is
`inTexcoord` in the vertex stage, varying `world` is `outWorld` there and
`inWorld` in the fragment stage, output `color` is `outColor`. Block and
texture names are the GLSL names themselves. The names are checked, which is
what catches two attributes of one type trading places: their locations and
types still agree.

`cmake/shader_manifest.cmake` holds the only parser.

## How the drift checks work

| Check | Where it runs | Reads | Catches |
| --- | --- | --- | --- |
| Source | desktop CTest, `nfsu_shader_source_guards` | the GLSL of both stages | a location, binding, type, name, interpolation, block member or block size that differs from the manifest |
| Compiled | Switch build step and `nfsu_switch_shader_<program>` | the `.dksh` each stage compiled to, and the TGSI `uam` wrote beside it | the same, as the compiler produced it |
| C++ | desktop CTest, `nfsu_shader_manifest_tests` | `nfsu/shader_manifest.h`, generated from the manifests | a vertex struct that no longer matches a buffer, and variants that are not the reference's |

### Source

`MODE=source` reads the constrained GLSL the stages are written in and
refuses what it does not read rather than skipping it: every `in`, `out` and
`uniform` at global scope needs an explicit layout qualifier with a literal
location or binding, a uniform block must be `std140`, one declaration per
statement, and no preprocessor directive but `#version` and `#extension`,
because a conditional could hide a declaration from a reader that does not
preprocess. It then compares every declaration with the manifest in both
directions and computes each block's std140 layout from its members.

### Compiled

The Switch build compiles each distinct stage source once and runs
`MODE=blob` for every manifest over the blobs of its two stages; a finding
fails the build, and the stamp the step writes on success is removed first,
so a failed check runs again. It reads two things.

The shader program header inside each `.dksh`. uam's container is a 24-byte
header (magic `DKSH`, header size, control size, code size, program table
offset, program count), one program per stage, and the program's entry point
in the code section, where the 0x50-byte shader program header begins; the
GPU reads that header to know which attributes a program fetches and which
outputs it writes. The layout is NVIDIA's documented Maxwell one, confirmed
here against probe shaders before the check relied on it:

| Stage | Words | Meaning |
| --- | --- | --- |
| both | 0 | bits 0..4 the header type (1 vertex, 2 fragment), bits 10..13 the stage (1 vertex, 5 fragment) |
| vertex | 6..9 | four bits per generic attribute 0..31, one per component fetched |
| fragment | 6..13 | two bits per component of each generic input 0..31: 1 flat, 2 perspective (`smooth`), 3 screen-linear (`noperspective`) |
| fragment | 18 | four bits per render target 0..7, one per component written |

So the attribute locations and the components read, the varyings with their
interpolation, and the render targets are read out of the blob itself.

The TGSI dump (`uam -t`) the same invocation writes. It declares each uniform
block the stage reads as `CONST[binding + 1]` with the block's whole size as
its range, each texture as `SAMP[binding]` with a `2D FLOAT` view, and each
varying the vertex stage writes as `GENERIC[location]`, so the bindings, the
block sizes and the vertex stage's outputs come from here. `CONST[0]` is the
driver's constant buffer and is skipped: uam refuses a uniform outside a
block, so only compiler state lands there (the y flip `dFdy` needs, for one).
The two files are also compared with each other where they overlap, so a
TGSI that is not from the same compile as its blob is reported.

### C++

`MODE=header` turns the manifests into `nfsu/shader_manifest.h` in the build
tree. Each attribute there carries `offsetof` and `sizeof` of the members it
names, taken from the struct its buffer names, so the header does not compile
against a struct that lost one of them. `tests/shader_manifest_tests.cpp`
then requires: each buffer's stride to be its struct's size, and the struct
the path uploads (`MeshVertex` for the car, `TrackVertex` for the track); each
attribute's members to start at its offset, run without a gap and hold exactly
the bytes its format fetches; exactly the four programs above; the textured
programs to sample one nearest, wrapped `sampler2D` and the fallback programs
none; `car_textured`'s variants to be exactly the alpha test and blend pairs
the two decoders produce; the other three programs to have only `opaque`;
`render_track_draw_list` to refuse the retail state; and every variant to
share the state above.

### Made to fail

A check that has only been seen to pass is not evidence, so each is run
against changes it must catch:

- `nfsu_shader_source_guards` changes one thing in a copy of `shaders/` for
  each of 15 cases: in a stage source, an attribute moved, widened or swapped
  with another of its type, a texture or block binding moved, a block member
  added or reordered, a varying's interpolation changed, an output moved, a
  declaration without a layout qualifier, a `#define`; in a manifest, an
  attribute, a texture binding or an interpolation moved, and a member offset
  std140 would not produce.
- `nfsu_switch_shader_blob_guards` compiles changed stages with `uam` and
  checks the unchanged manifest against them, for 9 cases: an attribute moved,
  an attribute widened and read, a texture binding moved, a fragment and a
  vertex block binding moved, a block member added, a varying's
  interpolation changed, a varying moved and an output moved.

Each case must fail with the finding that names its change. A run is
classified by what it printed: one that fails without the checker's closing
line is an unknown result and fails the test, as does a case whose change is
no longer in the file, or whose changed stage `uam` refuses.

## What the checks cannot catch

- Fixed-function state. The blend, the alpha test, the depth state, the
  colour write mask, the cull mode and the sampler's filter and wrap are in
  no shader, so no reflection sees them. The C++ check holds the manifest's
  state to the reference's decoders; nothing yet holds a backend to the
  manifest, because there is no backend.
- Vertex formats. A stage sees floats whatever the buffer holds, so
  `unorm8x4` against `float32x4` is only checked against the C++ struct's
  bytes, not against the compiled shader.
- A block's inside, in the compiled check. The blob and its TGSI show a
  block's binding and total size, not its members, so reordering two members
  of one size or changing a type without changing the size passes the
  compiled check. The source check catches both.
- GLSL outside the subset the source check reads. It refuses what it does not
  recognise, but it is not a GLSL parser.
- Bindings from the machine code. They are read from the TGSI, which is the
  compiler's own account of the program and not the Maxwell instructions. A
  uam that numbered `CONST` differently would make every block a finding
  rather than pass silently, and the overlap check catches a mismatched TGSI,
  but the instructions themselves are not decoded.
- Anything a stage declares and does not use. The compiler drops it, so the
  compiled check reports it missing; a manifest describes what the compiled
  program uses, and every declared component must be read.
- Whether the shaders draw the right pixels. They have not been compared with
  a reference frame: there is no deko3d backend and no GPU here. Known
  differences from the reference are the flat normal (screen derivatives
  instead of the three placed vertices), the view term (per fragment instead
  of per triangle from the first vertex), float instead of double
  arithmetic, the GPU's rasterisation, clipping and interpolation rules
  instead of the reference's fixed-point fill, the precision of unorm
  blending instead of the reference's integer formula, and the alpha test
  boundary noted above.
- Anything on a machine without `uam`. A desktop-only machine runs the source
  and C++ checks, not the compiled one.

## Reproducibility

The shaders are compiled with `uam 1.1.0` (devkitPro package `uam 1.1.0-1`).
Two compiles of the same source produce byte-identical files, and this
branch's sources compile to:

| File | SHA-256 |
| --- | --- |
| `placed.vert.dksh` | `0a130be59dd99b0db1a4db0a5cf390117347738b3ac2cfd3ec47df1134546ee3` |
| `textured.frag.dksh` | `f0e8a6c2acec1cc0d36e59ebadc39fec81704db0e094bdbebfdc63b3338d699b` |
| `fallback.frag.dksh` | `5d2908e4240171a3730b9602658ab0ac0d4829cf164ba1f66d10260cf3495a69` |

The blobs are written to `build-switch/switch/shaders/`. They are not yet
packaged into the application; the renderer will decide whether they travel
in the NRO's RomFS or are linked in.
