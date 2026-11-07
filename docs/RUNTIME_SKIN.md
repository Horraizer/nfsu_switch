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

Locating it is not enough on its own, because the payload is read from the
*source's* bytes while the offsets come from the *pack's* records. The pack
that holds the target must therefore still be paired with those bytes -
`parse_source` the same buffer, `file_size` the same size, `tables_parsed`
true. The first two are the rule `plan_body_skin_inputs` and
`decode_render_texture_sources` both enforce over their own sources; the
third is required by `plan_body_skin_inputs`, while
`decode_render_texture_sources` treats it as an incomplete source instead.
A *different* buffer put in the source's place, even one of exactly the
same size, stays inside every bound that `texture_payload` checks, so
nothing else would catch it: it would simply be decoded through offsets
that describe a different file, under the provenance of a record that file
does not hold. Nothing is read out of bounds; what is read is the wrong
file.

What the pairing cannot see is a caller overwriting the contents of the
same live buffer. It compares address and size, exactly as
`TexturePack::parse_source` does, and that field's own contract already
scopes itself to a buffer the caller keeps unmodified. Seeing through that
would mean hashing the file, which nothing in the traced behaviour asks
for; a caller that rewrites a pack's bytes under it has already broken
every layer below this one.

What the read returns therefore carries where it came from. A
`SkinTargetSurface` holds a `SkinTargetIdentity`: the source, pack and
record it was read from, every identifier that decides which bytes are read
and how, and the buffer they were read from. `compose_runtime_skin` takes
the source list too, re-locates the record the assembled inputs resolved in
it, and refuses unless the two identities are equal. Without that, two
valid records of the same format and the same size are interchangeable once
their level 0 is in a vector, and one target's pixels could be composited,
labelled and bound as another's.

The source list `compose_runtime_skin` is given is not checked to be the
one the inputs were assembled from, and does not need to be. Only the
target record is re-located in it, and every pixel the composite reads from
the inputs lives in a buffer the `AssembledSkinInputs` owns, so a list that
differs anywhere but the target changes nothing the composite produces.
What has to agree is the surface and the target record, and that is what is
compared.

The buffer is the part that makes this decide a file rather than a
position. Every other field is an index into the caller's source list or a
shape, and a second list holding a pack of the same layout at the same
index has all of them - which a re-encoded copy of a file, or a synthetic
pack built to the same shape, is. Only `SkinInputSource::bytes`, compared
and never dereferenced exactly as `TexturePack::parse_source` is,
distinguishes them. That is why an identity is complete only when it was
built against a source list, and why `skin_target_identity` takes one.

## 4. Binding: the priority rule

This is a policy, not a trace.

> The generated texture outranks every pack source for every hash it is
> bound to.

`bind_runtime_skin` implements it by removal: when it returns true, every
entry of `RenderTextureSet::textures` that claimed a bound hash has been
removed and reported, every matching `RenderTextureSet::unusable_claims`
entry has been removed and reported, and the generated texture is the last
entry of the set. That is what makes it the single claimant, which is what
`resolve_material` needs: two claimants of one hash are `unusable`, never a
winner.

Inside the call that happens in the opposite order - the generated texture
is appended first and the displaced entries are erased after it - because
the append is the one mutation that allocates, so doing it first means a
failed allocation has changed nothing yet. The end state is the same, and
it is the end state this priority rule is about.
`RenderTextureSet::shadowed` is left alone, because it records what the
decode did and the decode is not changed by this.

Removal is per record, not per hash, and a record can claim up to three
(`RenderTexture::claims`). One bound hash among them takes the whole record
away, so the hashes this binding never named would stop matching anything
at all. Each of them is therefore appended to
`RenderTextureSet::unusable_claims` and reported in
`binding.preserved_claims`, which is what the decode already does for a
record it leaves out whose claims still block. One entry per hash, as the
decode leaves: a hash the retained list already covers is not added again,
and neither is one an earlier removal in the same binding already
contributed.

For a set the decode produced, that leaves resolution for those hashes
exactly as it was. The decode gives a record distinct claims only when its
three hash fields disagree, which is also what makes it invalid, so every
hash it claims already resolves as `unusable` and goes on doing so. The one
case where this is not an identity is a hand-built texture that is usable
and claims several hashes: its unbound hashes move from `resolved` to
`unusable`. That is the honest report once the pixels they resolved to have
left the set, and it is nearer than the `unresolved` dropping them would
give, but it is not the same answer, so it is written down here rather than
claimed away.

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
it exists, if it names an input, or if it names the screenshot or its
`.partial`. That last refusal matters more than it looks: the dump is
written after the composite and before the render, so a spelling that got
through would publish the generated texture at the screenshot's path and
only then fail the render, leaving the caller the wrong file at the name
they asked for and a re-run that is refused until they delete it. Nothing
would be lost - the exclusive-create and no-replace protocol holds
throughout - but nothing they asked for would be there either.

Comparing those paths is `nfsu::same_file_path` (`include/nfsu/render.h`),
which every such refusal in the viewer and in the writers now goes through.
The paths do not exist yet, so `std::filesystem::equivalent` answers
nothing about them and the names have to be compared instead:

- both sides are made absolute before they are normalized, because
  `weakly_canonical` on its own leaves a bare `out.tga` relative - nothing
  in it exists - while `./out.tga` comes back absolute, since `.` does, and
  the two then compare unequal;
- `.` and `..` are removed;
- on Windows the extended-length prefix `\\?\` is removed and a
  `\\server\share` path is left absolute, because this standard library
  parses neither as a root and `fs::absolute` would otherwise rewrite both
  onto the current drive - which hides the alias between two spellings of
  one path and invents one between a network path and an unrelated local
  path;
- two spellings that differ only in case are one file wherever the
  filesystem folds case, and the filesystem is asked rather than assumed:
  read-only, by spelling a name the containing directory already holds with
  the case flipped and seeing whether it still answers. Nothing is created,
  opened for writing or changed. An entry the filesystem will not resolve -
  a dangling symbolic link, a junction whose target is gone - is skipped,
  because `fs::equivalent` fails for it for a reason that has nothing to do
  with case and one such entry sorting first would otherwise report a
  case-folding volume as case-sensitive. Where nothing can be asked at all,
  the answer is that the two are one file, because this decides a refusal.
  The fold is the OS ordinal fold on Windows, which matters because this
  repository is checked out under a Cyrillic path, and A-Z elsewhere.

What it does not see, and therefore lets through: a junction or a directory
symbolic link in the middle of the path, because `fs::weakly_canonical`
dereferences neither on this Windows toolchain; an 8.3 short name against
its long name; a second drive letter for one directory (`subst`, a mapped
share); a network path against the local path for the same file; and case
folding outside ASCII where the standard library is not asked. Those are
listed at the declaration rather than claimed away.

`tests/render_tests.cpp` covers it against a case-folding answer it
measures itself rather than asks the code for - including a probe directory
whose only entry dangles, the extended-length prefix, a network path, and a
non-ASCII case alias - and the CTest `nfsu_viewer_dump_alias` runs the
viewer with the two spellings and needs no input files, because the refusal
comes before the first read. What those case assertions cannot prove on a
case-folding filesystem is the other half: with the measured answer `true`
everywhere, a comparison that always said "one file" for a case alias would
pass them too, and only a case-sensitive filesystem separates the two.

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
skin preserved claim: hash 0x... source S pack P texture R
skin generated: texture I source S bytes B
skin dump: <file name>
```

`skin preserved claim:` is one line per hash a removed pack texture claimed
that this binding did not name; it is the line the claim-preservation rule
above exists to produce, and the same hashes also appear in the
`unusable claim:` block, because that block describes the set the scene is
prepared from.

**`source S` is two different numberings in this block**, which is worth
knowing before reading a report:

- `skin target:` and `skin input ...:` print the *skin input source* index,
  the one the `skin source I: <label>` legend above them defines. Those
  sources are the car's pack, then `--skin-vinyls` if it was given, then
  each `--shared-textures` file.
- `skin displaced:`, `skin displaced claim:`, `skin preserved claim:` and
  `skin generated:` print the *`RenderTextureSet`* source index, which is
  the car's pack and the `--shared-textures` files only - `VINYLS.BIN` is
  never a material texture source - plus, for `skin generated:`, the
  synthetic index one past the real ones that the caller gives the
  generated texture.

With `--skin-vinyls` the two are offset by one for every shared file, so
the same number means different files on different lines. On the 240SX
smoke render below they collide: `skin target: ... source 3` is `shared-2`
in the skin legend, and `skin generated: ... source 3` is the synthetic
index.

An input that did not resolve is reported with its machine-readable skip
reason and is not fatal: the composite goes on with the layers that did,
which is what the gatherer's five failure paths do. Any skipped input makes
the viewer exit 3, the same way a pack issue does.

## Verification

### Build and tests

Clean Release build with `-Wall -Wextra -Wpedantic`, no warnings. Full
`ctest`: 10 of 10, including the new `nfsu_runtime_skin_tests`
(`tests/runtime_skin_tests.cpp`, synthetic packs only, 413 checks) and the
`nfsu_viewer_dump_alias` case described under `--skin-dump` above. It
covers the path table for A8R8G8B8, P8 and DXT1 targets and for an absent
one; the target surface for both proven conversions; the byte mapping
checked against a direct `composite_body_skin_32` call rather than against
written-down numbers; determinism over repeated runs; source priority
deciding which pixels are composited; a missing mask, a mis-sized record
and a record whose stream cannot be decompressed each being skipped or
refused with the right reason rather than being fatal; every binding
refusal leaving the set untouched and saying why, by its message and not
only by its answer; displacement of a rival pack record and of an unusable
claim, with `decoded_bytes` checked against an expectation computed
independently of the implementation; a bind of the maximum three hashes;
and a `prepare_car_scene` that resolves the body group only once the skin
is bound.

Two of those need saying separately, because neither is reachable from the
arguments of the function it tests.

`test_identity_fields` changes each of the 23 fields of
`SkinTargetIdentity` in turn and asserts `same_skin_target` says the two
are different targets, in both directions. Before it existed,
`pack_root_offset`, `pack_hash`, `storage`, `mip_count` and
`palette_bytes` could each be deleted from the comparison with the whole
suite still passing, because every negative case differed in several
fields at once. The field list is bound from the struct, so a field added
to `SkinTargetIdentity` does not compile until it is named there - the
same pin `same_record` carries in `src/runtime_skin.cpp`.

`test_binding_allocation_failure` makes an allocation throw, at each
allocation `bind_runtime_skin` makes in turn, through a global
`operator new` replaced in that test program. Every refusal the arguments
can reach returns before the function touches anything, so nothing else in
the suite exercises what "leaves the set untouched" is actually about: the
claim list rebuilt into a local, the single `push_back` the set is mutated
through, and the `noexcept` swap that commits the list. The set it uses
has a texture that stays, two that are removed, two claims to preserve and
one to carry over, and the rebuilt list is longer than the one it
replaces, so committing it by assignment rather than by swap would have to
allocate after the textures had already changed - which the test catches.

Three of those cases are the defects the Codex review found, and each is
asserted the way it would actually go wrong:

- A source whose bytes were replaced by another image of *exactly* the same
  size, plus a pack whose `file_size` is not its source's, one whose record
  tables were never read, and one no parser ever ran on paired with an empty
  buffer, where both sides of the pointer comparison would be null. The test
  also reads the substituted image through its own properly parsed sources
  and checks that its level 0 is different, so the refusal is not a vacuous
  one.
- A malformed record claiming one bound hash and two unbound ones, with
  `resolve_material` asked what each hash resolves to before and after the
  binding. Both unbound hashes stay `unusable`; the bound one becomes
  `resolved`; a hash nothing ever claimed stays `unresolved`. The record's
  own hash field repeats one of the claims, so the count also proves one
  hash is preserved once. Then the two ways a hash could be preserved
  twice and is not: one the retained claim list already covers, and one two
  removed records share.
- Two targets in one pack, both valid A8R8G8B8, both 4 x 4, differing only
  in their background pixels. Composing the assembled inputs of one against
  the surface of the other is refused, while the matching pair still
  composes and a surface whose identity was never filled is refused as
  well. Then the swap that needs no second record: a second source list
  whose pack holds the same two textures under the same names at the same
  sizes, so every field the provenance carries matches and only the pixels
  and the buffer differ. Composing a surface read from that list against
  inputs assembled from the first is refused, and so is composing the first
  list's surface while naming the second list.

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

### Independent review, second round

A third review, by Codex on the finished branch, confirmed three more
defects. All three are fixed above and are covered by the tests just
described.

1. `read_skin_target_surface` accepted bytes the pack was never parsed
   from. `record_at` compared record metadata but not `parse_source`,
   `file_size` or `tables_parsed`, so a same-sized substituted buffer was
   decoded under the original provenance. The pairing rule the assembly and
   decode layers already apply is now applied here too.
2. Binding one hash could remove unrelated aliases. A malformed record
   retains up to three distinct claims, and removing it for one bound hash
   dropped the others, changing what they resolve to. Every unbound claim
   is now kept as an `UnusableClaim` and reported.
3. Composition did not prove the target surface belonged to the assembled
   target. Two valid same-sized type-32 surfaces could be swapped,
   producing one target's pixels labelled and bound as the other's. The
   surface now carries a `SkinTargetIdentity` and `compose_runtime_skin`
   compares it with `plan().target_texture`.

Nothing outside those three was changed. On the read-only 240SX smoke
render below, run at the previous commit and at this one on the same
inputs, writing the same file name in separate directories, the screenshot
and the generated texture are byte-identical, as are stdout, stderr and the
exit code:

| Artifact | SHA-256 |
| --- | --- |
| `render.tga`, 640x360 | `10D51A935C31759F5C61E331A911213710BEEBBD34D550C07C5A27C37FF7DFE1` |
| generated texture, 512x512 | `D70C4756B1D174082C9F50E2381A76300273408B40E2AE8EF95A20385589FFCF` |

A run that actually displaces a record, `--skin-bind 240SX_DOORLINE`
(displacing `source 0 pack 0 texture 41 usable 1`), is byte-identical
across the two commits as well, and prints no `skin preserved claim:` line:
a valid retail record's three hash fields agree, so it claims one hash and
there is nothing to preserve.

### Independent review, third round

A third review, of the finished fixes, found no defect in any of them and
one defect outside them, in code this branch introduced. It is fixed here.

**`--skin-dump` could publish the generated texture at the screenshot's
path.** The viewer's own path comparison used
`std::filesystem::weakly_canonical` without making the two paths absolute
first. For a path with no component that exists, that returns the relative
name; for the same name written `./name`, the `.` does exist, so it returns
an absolute path. The two compared unequal, the guard did not fire, and
because the dump is written after the composite and before the render, the
512x512 generated texture was created at the screenshot's path and the
render then failed on it. Reproduced on retail inputs at the previous head:

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> out.tga --width 128 --height 96 \
  --shared-textures <GLOBALB.BUN> --shared-textures <Cars/TEXTURES.BIN> \
  --skin-car 240SX --skin-vinyls <VINYLS.BIN> --skin-dump ./out.tga
error: refusing to overwrite out.tga
exit 2, out.tga present, 1048594 bytes
```

`--skin-dump OUT.TGA` and `--skin-dump Out.Tga` did the same on this
case-insensitive filesystem. Nothing was ever lost - the exclusive-create
and no-replace protocol held throughout, and all five retail hashes were
unchanged - but the caller got the wrong file at the name they asked for,
an exit code of 2, and a re-run that was refused until they deleted it.

The viewer no longer has a path comparison of its own. Every one of its
path refusals, and the writers', now goes through
`nfsu::same_file_path`, described under `--skin-dump` above. At this head
each of the five spellings exits 1 and creates nothing.

One behaviour changed beyond that defect, because the checks that used
`std::filesystem::equivalent` alone now use the same comparison: two
`--shared-textures` arguments that are different spellings of one file that
*does not exist* are now refused up front as duplicates, where before they
were both read, both failed, and the run continued to exit 3. The same
holds for an output path equal to a texture or geometry path that does not
exist. Every case where the files do exist behaves exactly as before.

Two adversarial reviews of the fix itself came back with more, all
addressed. One dangling junction in the output directory put the case half
of the guard back exactly as it was, because the probe took a failure to
resolve that entry for an answer. The Windows extended-length prefix and a
network path were both rewritten onto the current drive by `fs::absolute`,
which let `--skin-dump \\?\C:\dir\out.tga` through beside an `out.tga`
screenshot and made a network path compare equal to an unrelated local one.
Case folding stopped at A-Z although the filesystem folds more and this
repository lives under a Cyrillic path. The comparison could throw out of a
call site with no handler. And in the tests: the CTest passed a viewer that
printed the refusal and carried on, "a refused binding reports nothing"
looked at one field of seven, and "the set is untouched" compared
`shadowed` by length alone and against a fixture where it was empty.

The same review raised two gaps in the tests rather than in the code, both
closed: no test pinned an individual field of `SkinTargetIdentity`, and no
`bind_runtime_skin` test looked at `error` at all. Both are described under
"Build and tests" above. It also listed documentation that claimed more
than the code delivered; those sentences are corrected in place here, in
this file, in `docs/RENDERER.md`, in `README.md`, in `docs/ROADMAP.md` and
in the two headers.

Two items from that review are deliberately not addressed here, because
neither is a defect in what this branch set out to do and both change
behaviour rather than describe it: `--skin-car NAME --skin-bind NAME_SKIN1`
is diagnosed by `bind_runtime_skin` after the whole composite and exits 2,
where both hashes are known at the end of `parse_options` and it could be
an exit-1 usage error; and `error: --skin-car needs 1 values` is a grammar
slip shared by every single-value skin option.

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
screenshot the same file was refused before anything was read - for the
identical spelling, for `./<name>` against `<name>`, and, on this
case-insensitive filesystem, for `<NAME>` against `<name>`. Each exits 1
and creates nothing.

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
