# Runtime body skin: composing and binding `DUMMY_SKIN1`

This document covers `include/nfsu/runtime_skin.h` and the `--skin-*`
options of `nfsu_viewer`. It is the last step of the body-skin chain:

| Layer | Header | Evidence |
| --- | --- | --- |
| Input selection and assembly | `nfsu/skin_inputs.h` | [`SKIN_INPUTS.md`](SKIN_INPUTS.md) |
| Per-pixel composite | `nfsu/body_skin.h` | [`BODY_SKIN.md`](BODY_SKIN.md) |
| Target choice, composition, binding | `nfsu/runtime_skin.h` | this file |

The reference executable is `speed.exe`, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`,
image base 0x00400000. Every address below is a virtual address in it.

This layer parses nothing. It consumes packs the caller already parsed,
never writes to a pack, and never opens a file of its own.

## What it decides, and on what evidence

Three decisions live here, and each is either a trace or a stated policy.
They are kept apart on purpose.

1. **Which composite routine runs.** A trace. Taken from the real target
   record, never from an option.
2. **What the generated pixels look like as a texture.** A trace for the
   lane order, an inference for the channel names.
3. **Which hashes the generated texture answers to, and that it outranks
   every pack source for them.** A policy, stated below, over a caller's
   explicit list.

## 1. The compositor path comes from the target record

`0x005AA262` reads byte `+0x4A` of the target *texture object*: value 8
calls `0x005A8E90`, anything else calls `0x005A8A50`, which itself returns
at `0x005A8A7E` unless the byte is `0x20`.

Byte `+0x4A` starts as the pack record's format code at info +74, in place.
The upload at `0x00407DB0` is the only thing that changes it: for a record
whose D3DFORMAT is 41 it allocates a 32-bit surface (`0x00407E21`),
expands every level through the record's own 256-entry palette copying each
entry whole (`0x00407E70..0x00407E7B`), sets the format to 0x15
(`0x00407EA8`) and writes `mov byte ptr [esi+0x4A], 0x20` (`0x00407EB1`).
A non-paletted record is left alone. See [`BODY_SKIN.md`](BODY_SKIN.md).

`choose_skin_target_path` is exactly that rule read backwards:

| Record format | Object byte +0x4A | Path | Surface |
| --- | --- | --- | --- |
| A8R8G8B8, code 0x20, D3DFORMAT 21 | 0x20, unchanged | type 32, `0x005A8A50` | stored dwords |
| P8, codes 0x08/0x80/0x81, D3DFORMAT 41 | 0x20, written by `0x00407EB1` | type 32, `0x005A8A50` | expanded through the record's palette |
| anything else | the record's own code | none | refused |

**On this build no record can select the type-8 path.** The only records
whose format code is 8 are paletted, and the upload rewrites their type
byte to 0x20 before the dispatch reads it. So `choose_skin_target_path`
never returns `SkinTargetType::type8`, and `compose_runtime_skin` never
calls `composite_body_skin_8`. That is not a shortcut: a type-8 composite
produces a palette whose lane order nothing proves
([`BODY_SKIN.md`](BODY_SKIN.md), "What the retail data does not decide"),
so there is no way to turn one into an RGBA image without guessing, and
nothing here guesses.

A record the table refuses is refused with a message that names its format,
its code and the instruction that would have turned it away, rather than
being composited on a guess.

## 2. From composite output to an RGBA8 texture

`composite_body_skin_32` writes `width * height` packed colors "in the
target's lane order": the type-32 path exchanges lanes 0 and 2 of every
color it is given, so a surface pixel is
`(GLOSS << 24) | (RED << 16) | (GREEN << 8) | BLUE`.

That is the same dword an A8R8G8B8 record stores, which the retail data
confirms independently: the four marker colors in `DUMMY_SKIN1` match the
type-32 list at `0x005A8AE5..0x005A8AFD` read as little-endian u32, and
match nothing when read byte-reversed ([`BODY_SKIN.md`](BODY_SKIN.md)).

`compose_runtime_skin` therefore converts with the same mapping
`decode_level` uses for an A8R8G8B8 level (`src/texture.cpp`):

```text
red   = byte 2 of the dword
green = byte 1
blue  = byte 0
alpha = byte 3, the GLOSS lane
```

so a generated texture and a decoded pack texture mean the same thing by
the same bytes. **That the GLOSS lane is an alpha channel is not proven**;
it is carried in the fourth byte because a texture in this project is
RGBA8. The viewer draws decoded RGB opaquely unless `--alpha-cutoff` is
given, so the default render does not depend on it.

Only level 0 is read and only level 0 is produced: neither composite
routine downsamples, every lock passes level 0, and the retail target
carries a single mip level.

## 3. The target surface

The marker scan at `0x005A8AC9` runs over the *untouched* target before
`0x005A8C26` overwrites it, so the composite needs the target's own pixels.
`read_skin_target_surface` reads level 0 of the record the assembly layer
resolved, through the same `texture_payload` API the pack decoder uses, and
converts it only where the upload proves the conversion: stored dwords for
an A8R8G8B8 record, palette entries copied whole for a paletted one.

It locates the record through the provenance `plan_body_skin_inputs`
produced and then re-checks that record's raw identifiers against it, so a
provenance from a different source list is refused rather than silently
composited from the wrong pixels.

## 4. Binding: the priority rule

This is a policy, not a trace.

> The generated texture outranks every pack source for every hash it is
> bound to.

`bind_runtime_skin` implements it by removal: every entry of
`RenderTextureSet::textures` that claims a bound hash is removed and
reported, every matching `RenderTextureSet::unusable_claims` entry is
removed and reported, and only then is the generated texture appended. That
is what makes it the single claimant, which is what `resolve_material`
needs: two claimants of one hash are `unusable`, never a winner.
`RenderTextureSet::shadowed` is left alone, because it records what the
decode did and the decode is not changed by this.

Nothing is mutated in any pack. The generated texture carries the
provenance source index the caller gives it, `pack` and `record` zero, and
the report names every record it displaced.

### Which hashes, and why `<car>_SKIN1` is one of them

The material slots of the 240SX body do not name `DUMMY_SKIN1`. They name
`240SX_SKIN1` (`0x77A20245`), on 637 of the car's 1242 material groups, and
no record with that hash exists anywhere in the surveyed installation
([`CAR_SCENE.md`](CAR_SCENE.md)).

The retail render path resolves that by substituting the hash, not the
texture: the render-info setup formats `%s_SKIN1`, `%s_SKIN1B` and
`GLOBAL_SKIN1` at `0x00580B2B`, `0x00580B6E` and `0x00580BA6`, and at
`0x00580C37..0x00580C67` it uses `RideInfo+0x444` whenever that is
non-zero, otherwise the `%s_SKIN1` hash. Initialisation at `0x004C4230`
writes `bin_hash("DUMMY_SKIN1") = 0x530B82B0` into `+0x444`.

Substituting the hash a slot resolves through, and binding one texture
under both hashes, put the same pixels on the same groups. This layer does
the second, because it needs no change to the material resolution a backend
already relies on. Which hashes to bind is the caller's explicit statement:
the target hash is always one, `<car>_SKIN1` is added by the viewer unless
`--skin-no-body-bind` says otherwise, and `--skin-bind`/`--skin-bind-hash`
add more.

At most `kMaxRuntimeSkinHashes` (3) hashes can be bound, which is the cap a
pack record has, because `RenderTexture::claims` holds three.

## The viewer

`--skin-car NAME` turns the whole feature on. Without it nothing in this
chain runs, no extra file is read and not one line of the report changes;
that is checked against a build of the previous commit, byte for byte, in
"Verification" below. A `--skin-*` option without `--skin-car` is a usage
error rather than silently ignored input.

### Skin input sources

Searched in this order, first claim wins, exactly as
`decode_render_texture_sources` and `plan_body_skin_inputs` do:

| Skin source | File | Label in the report |
| ---: | --- | --- |
| 0 | the car's `<TEXTURES.BIN>` | `car-textures` |
| 1 | `--skin-vinyls FILE`, when given | `car-vinyls` |
| 2.. | each `--shared-textures FILE`, in order | `shared-N` |

Every one of these packs is parsed once, by the viewer, for both purposes.
The viewer never parses a pack twice and the skin layer never parses one at
all. `VINYLS.BIN` is a skin input source only: it never becomes a material
texture source, and it is read only when `--skin-vinyls` is given. It is
bounded by the same 256 MiB per-file cap, and the bytes it takes come out
of the same 512 MiB total, so it lowers what is left for the shared files
read after it.

A pack the assembly layer would refuse - one whose record tables were not
parsed, or that was not parsed from the bytes it is paired with - offers no
records instead of failing the run, and a file left with no usable pack is
not searched at all. Either makes the exit code 3 and prints why on stderr,
naming the label. Because a file can drop out that way, the searched list
is printed on stdout as `skin source I: <label>`, so the source index every
other line carries is never ambiguous.

The composite target usually lives in the shared `Cars/TEXTURES.BIN`, so
that file has to be one of the `--shared-textures`.

### Customization options

All explicit. Nothing the game reads from a part is defaulted here: a vinyl
layer, a doorline variant, a hood/spoiler variant or a paint color that is
not named on the command line is simply not selected.

| Option | Meaning |
| --- | --- |
| `--skin-hood V` | source index 0, `hood-spoiler`, `hood` or `spoiler` |
| `--skin-vinyl N TEXTURE NUMCOLOURS remap\|no-remap` | vinyl layer N in 0..3; `NUMCOLOURS` 1..256. A layer whose part carries no `NUMCOLOURS`, or carries zero, abandons the whole composite through the epilogue at `0x005A9F9A`, so neither is a selection this option can make |
| `--skin-vinyl-color N SLOT R G B GLOSS` | remap color SLOT in 0..3 of layer N |
| `--skin-doorline V` | source index 5, `plain`, `kit` or `widebody` |
| `--skin-base R G B GLOSS` | the `BASE_PAINT` color |
| `--skin-special SLOT R G B GLOSS` | `PAINT_ENGINE`, `PAINT_SPOILER`, `PAINT_BRAKE`, `PAINT_EXHAUST`; an unset slot falls back to the base color, as `0x005A9DE0` does |
| `--skin-target NAME` | target name, default `DUMMY_SKIN1` |
| `--skin-target-hash H` | target hash instead of a name |
| `--skin-no-body-bind` | do not bind `<NAME>_SKIN1` |
| `--skin-bind NAME`, `--skin-bind-hash H` | bind one more hash, repeatable |
| `--skin-dump FILE` | write the generated skin as a TGA |

The viewer does not read a part database, so it cannot apply the two rules
that need one: the vinyl-layer de-duplication on installed-part identity
(`0x005A9F30`) and the variant choice the hood/spoiler and doorline helpers
make from installed slots. Both are the caller's to state. The helper rules
themselves are recorded in `nfsu/skin_inputs.h`.

`--skin-dump` writes through the same no-replace protocol as the
screenshot: `<file>.partial` is created exclusively and published with an
operation that cannot replace an existing entry. It is refused up front if
it exists, if it names an input, or if it names the screenshot. It is
written after the composite and before the render, so a render failure
still leaves the generated texture to look at.

### Report

Every line below is printed only when `--skin-car` was given, on stdout,
after the texture-source lines and before `frame:`. The per-source decode
counts printed above them are taken before binding, so they keep describing
the decode.

```text
skin source I: <label>
skin: car "<NAME>" path <type32|type8> <W>x<H> layers N owned_bytes B skipped_inputs S
skin target: hash 0x... source S pack P texture R <FORMAT> code 0x.. d3d D <W>x<H> mips M object_type_byte 0x.. expanded 0|1
skin paint: base 0x... special 0x...,0x...,0x...,0x... defaulted 0,1,1,1
skin input <role>: source "<NAME>" 0x... [<provenance>] mask "<NAME>" 0x... [<provenance>] palette N remap 0|1 <layer L ... | skipped <reason> (detail)>
skin bound: hash 0x... "<NAME>"
skin displaced: hash 0x... source S pack P texture R usable 0|1
skin displaced claim: hash 0x... source S pack P texture R
skin generated: texture I source S bytes B
skin dump: <file name>
```

An input that did not resolve is reported with its machine-readable skip
reason and is not fatal: the composite goes on with the layers that did,
which is what the gatherer's five failure paths do. Any skipped input makes
the viewer exit 3, the same way a pack issue does.

## Verification

### Build and tests

Clean Release build with `-Wall -Wextra -Wpedantic`, no warnings. Full
`ctest`: 9 of 9, including the new `nfsu_runtime_skin_tests`
(`tests/runtime_skin_tests.cpp`, synthetic packs only, 167 checks). It
covers the path table for A8R8G8B8, P8 and DXT1 targets and for an absent
one; the target surface for both proven conversions; the byte mapping
checked against a direct `composite_body_skin_32` call rather than against
written-down numbers; determinism over repeated runs; source priority
deciding which pixels are composited; a missing mask, a mis-sized record
and a record whose stream cannot be decompressed each being skipped or
refused with the right reason rather than being fatal; every binding
refusal leaving the set untouched; displacement of a rival pack record and
of an unusable claim, with `decoded_bytes` checked against an expectation
computed independently of the implementation; a bind of the maximum three
hashes; and a `prepare_car_scene` that resolves the body group only once
the skin is bound.

One case is worth naming, because it is the defect an independent review
found. `read_skin_target_surface` locates its record through the provenance
and then re-checks that record's identity, and the first version compared
only the name hash and the info offset. A re-encoded variant of a pack
keeps both - the name is the same and the info records have a fixed stride
- so a DXT1 record could be read with the level size of the A8R8G8B8
record the path had been chosen from, four bytes per pixel out of a
block-compressed payload. The check now compares everything the read
depends on, and a test builds exactly that pair and asserts the refusal.

### Independent review

Two independent agents reviewed the finished change, one reading it against
the retail evidence and the surrounding code, one exercising the binary.
Every confirmed finding was fixed and all validation re-run.

From the code review: the identity check above; `--skin-vinyl` accepting a
`NUMCOLOURS` of zero or `default`, which the assembly layer always refuses
because `0x005A9F9A` abandons the whole composite, so the option now takes
1..256; a `static_assert` tying `kMaxRuntimeSkinHashes` to the size of
`RenderTexture::claims`; `std::length_error` added to the refusals that
already caught `std::bad_alloc`; three test gaps closed, one of which was a
dead branch that could never run; and two documentation statements that
claimed more than the code does.

From the empirical review, over 118 adversarial invocations: the report's
ASCII escaping helper was named `quoted`, and `<iomanip>` declares a
non-const `std::quoted` overload that wins by argument-dependent lookup for
a non-const `std::string` lvalue. Two of the new lines passed one, so they
printed raw bytes while every other line escaped them. The helper is now
`ascii_quoted`, which removes the trap for every call site, and a run with
a quote, a backslash and a high byte in the car name is checked to produce
no byte outside 0x20..0x7E on stdout. Also: an empty `--skin-vinyls` or
`--skin-dump` value is now a usage error instead of reaching the writer,
and `--skin-vinyls` naming the not-yet-existing screenshot or dump is
caught up front by name rather than by the writer afterwards.

The reviews confirmed, by tracing every mutation, that `bind_runtime_skin`
really does leave the texture set untouched when it fails, and found no
defect in the bounds arithmetic, the P8 expansion, the RGBA mapping, the
option loop's index advancement, the lifetimes of the borrowed source
buffers, or the guarantee that nothing printed without `--skin-car`
changes. Twenty of the twenty-four no-option configurations they compared
against the previous commit matched on all four channels, and the only
difference in any of them was the usage text, which gained the new option
block.

One pre-existing defect was observed and deliberately not fixed here: on
this MinGW build `std::filesystem::path(argv[i])` aborts for a path whose
bytes are not valid UTF-8, and the previous commit aborts identically for
`GEOMETRY.BIN`, the output and `--shared-textures`. The new file options
reach the same constructor, so they inherit it. Fixing it means changing
how every existing path argument is handled, which is outside this change.

### Unchanged behaviour without the options

`nfsu_viewer` built at the previous commit and at this one were run on the
same read-only 240SX inputs with no `--skin-*` option, writing to the same
file name in separate directories. Three configurations were compared:
no shared textures; two shared sources with an explicit mesh subset; and
one shared source with a changed camera, `--flip-v` and `--cull back`. The
TGA, stdout and stderr of all three are byte-identical.

### Read-only 240SX smoke render

Files read, all unchanged before and after (SHA-256):

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `Cars/240SX/GEOMETRY.BIN` | 4780816 | `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619` |
| `Cars/240SX/TEXTURES.BIN` | 697340 | `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA` |
| `Cars/240SX/VINYLS.BIN` | 15409440 | `46099AF77F100C3E09EBDBF4813631FFE3188CDE411F79D570C01B076E4EA1E3` |
| `Cars/TEXTURES.BIN` | 1230808 | `999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |

Nothing was written into the installation. The selection was explicit:
`VINYL_LAYER0` taking `TRIBAL5` at `NUMCOLOURS` 16 without remap, the plain
doorline, base paint 200/30/30/128 and `PAINT_ENGINE` 255/200/0/128. The
records it selected:

| Role | Name | Hash | Skin source | Record | Format | Size | Mips |
| --- | --- | --- | ---: | ---: | --- | --- | ---: |
| target | `DUMMY_SKIN1` | `0x530B82B0` | 3 (`Cars/TEXTURES.BIN`) | 120 | code 0x20, D3D 21 | 512x512 | 1 |
| vinyl_layer0 source | `240SX_TRIBAL5` | `0x4573BA32` | 1 (`VINYLS.BIN`) | 275 | code 0x80, D3D 41 | 512x512 | 7 |
| vinyl_layer0 mask | `240SX_TRIBAL5_MASK` | `0x005C59FD` | 1 (`VINYLS.BIN`) | 0 | code 0x08, D3D 41 | 512x512 | 7 |
| doorline source | `240SX_DOORLINE` | `0x580576DB` | 0 (`TEXTURES.BIN`) | 41 | code 0x20, D3D 21 | 512x512 | 7 |
| doorline mask | `240SX_DOORLINE_MASK` | `0x699B6846` | 0 (`TEXTURES.BIN`) | 49 | code 0x20, D3D 21 | 512x512 | 7 |

The path selected from the target record was **type 32**, object type byte
`0x20`, not expanded. Both paletted vinyl surfaces were expanded; neither
doorline surface was. Two layers were assembled, owning 4198400 bytes, and
the generated texture is **512x512**, 1048576 bytes. It was bound under
`0x530B82B0` (`DUMMY_SKIN1`) and `0x77A20245` (`240SX_SKIN1`), displacing
nothing, because no pack record claims either hash.

Rendering mesh 259 `240SX_KIT00_BODY_A` at 640x360 with the car pack,
`Global/GLOBALB.BUN` and `Cars/TEXTURES.BIN`:

| | Previous commit | With the runtime skin |
| --- | ---: | ---: |
| material groups resolved | 0 | 3 |
| material groups unresolved | 3 | 0 |
| unresolved hashes | 1 | 0 |
| triangles textured | 0 | 804 |
| triangles fallback | 804 | 0 |
| pixels drawn / fallback | 16432 / 16432 | 16432 / 0 |

Two runs of the same command produced byte-identical screenshots and
byte-identical generated textures. Re-running with the dump file already
present was refused (`refusing to overwrite the --skin-dump file`) and
wrote nothing at all, including no screenshot. Naming the dump and the
screenshot the same file was refused before anything was read.

The derived images were inspected and deleted. No retail data, derived
texture, dump or screenshot is in this repository.

## Observed, inferred, unknown

### Observed in the executable

The dispatch at `0x005AA262` and the two guards at `0x005A8A7E` and
`0x005A8EB9`; the upload at `0x00407DB0` with the palette expansion at
`0x00407E70..0x00407E7B`, the format write at `0x00407EA8` and the type
byte write at `0x00407EB1`; the marker scan at `0x005A8AC9` preceding the
overwrite at `0x005A8C26`; the render-info hash choice at
`0x00580C37..0x00580C67` and the initialisation at `0x004C4230`.

### Observed in retail data

The `DUMMY_SKIN1` record in `Cars/TEXTURES.BIN` at index 120, 512x512, one
mip level, format code 0x20 and D3DFORMAT 21; the four marker colors in its
level 0; the 240SX records in the smoke-render table; that no record in the
surveyed installation claims `0x77A20245`.

### Inferred

- That format code 0x20 with D3DFORMAT 21 is A8R8G8B8 stored as
  little-endian ARGB. This comes from the public D3DFORMAT numbering, not
  from the pixels ([`FORMATS.md`](FORMATS.md)). The conversion this layer
  performs does not depend on the name: the lane order it relies on is the
  one the marker comparison proves.
- That binding one texture under both the target hash and the `%s_SKIN1`
  hash is equivalent, for what ends up on screen, to the hash substitution
  the retail render path performs. The substitution is observed; the
  equivalence is the inference.

### Unknown

- Whether the GLOSS lane is an alpha channel, and what the alpha value
  `0xBF` in the retail target means ([`BODY_SKIN.md`](BODY_SKIN.md)). The
  fourth RGBA byte carries it unchanged and nothing here interprets it.
- What the type-8 path is for. Nothing on this build reaches it, and this
  layer cannot produce an RGBA image from it without guessing a lane order,
  so it refuses instead.
- Which of `DUMMY_SKIN1..4` a car uses, and what `%s_SKIN1B` and
  `GLOBAL_SKIN1` are for. Only `0x004C4230` writing the `DUMMY_SKIN1` hash
  is observed. The viewer takes the target name as an option and defaults
  it to the one that is observed.
- Whether the retail renderer would draw the composited surface with the
  alpha-test and blend selectors of the *target* record or of the material
  ([`MATERIALS.md`](MATERIALS.md)). The CPU renderer applies neither yet.
- Which pack wins a hash held by both car packs, inherited unchanged from
  [`SKIN_INPUTS.md`](SKIN_INPUTS.md). The order here is the caller's
  explicit source list, not a claim about retail lookup.
