# Body-skin input assembly

Clean-room notes on how Need for Speed: Underground (PC) gathers the inputs
its body-skin compositor consumes, and on what `nfsu_skin_inputs`
(`include/nfsu/skin_inputs.h`) does with them. Everything here was read out
of the retail executable and out of a user-provided retail installation. No
game code and no game data are stored in this repository.

Reference executable:

- File: `speed.exe`
- Size: 3178496 bytes
- SHA-256: `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`
- PE32, image base `0x00400000`. All addresses below are virtual addresses
  of that image.

This note covers the layer between the texture packs and the compositor.
The per-pixel work is in [`BODY_SKIN.md`](BODY_SKIN.md); the pack format is
in [`FORMATS.md`](FORMATS.md); the earlier survey of which textures take
part is in [`CAR_SCENE.md`](CAR_SCENE.md).

## The gatherer

`0x005A9CE0` fills an array of 8 records of 0x34 bytes at parameter block
`+0x18` and stores the used count at `+0x1B8`. It walks eight source
indexes in a fixed order and appends a record for each one that resolves,
so the list it hands the compositor is compact:

| Index | Source | Name helper |
| ---: | --- | --- |
| 0 | hood/spoiler vinyl | 0x005A9930 |
| 1..4 | `VINYL_LAYER0`..`VINYL_LAYER3` | 0x005A9B50 |
| 5 | doorline | 0x005A96B0 |
| 6, 7 | never filled | none |

`0x005A9ED3` clears all 0x1A0 bytes first, and every failure path jumps
past the record cursor and the counter at `0x005AA1D4`, so an unfilled
record stays zeroed.

### How each name is built

All three helpers start from the same car name, `CarTypeInfo+0x20` of
`[0x734588] + 0xC90 * RideInfo[0]` (0x005A96C8, 0x005A9BA8, 0x005A9951).
None of them folds case, anywhere.

**Index 0, 0x005A9930.** The car name plus exactly one literal, appended by
the single `strcat` at `0x005A999C`:

| Slot 35 `VINYL_HOOD` (`RI+0x4D8`) | Slot 36 `VINYL_SPOILER` (`RI+0x4DC`) | Name |
| --- | --- | --- |
| filled | filled | `<car>_HOOD_SPOILER` (0x006B7E80) |
| filled | empty | `<car>_HOOD` (0x006B7E78) |
| empty | filled | `<car>_SPOILER` (0x006B7E6C) |
| empty | empty | returns 0 (0x005A99C9) |

The branches are `0x005A996D`, `0x005A9971` and `0x005A998B`, all plain
`test reg, reg` on the raw slot pointer: presence only, with no
upgrade-level condition. The function contains exactly one `call`, the
`strcat`, so **no part attribute reaches this name**. Which hood or spoiler
vinyl is installed does not change it.

**Indexes 1..4, 0x005A9B50.** The car name, `_` (0x006B7E1C) and the
installed part's `TEXTURE` attribute value, fetched by the string getter
`0x0057CC30` and appended at `0x005A9BDB`, giving
`<car>_<TEXTURE-value>`. A null part or a missing attribute returns 0.

**Index 5, 0x005A96B0.** The car name plus one of three literals:

| Condition | Name |
| --- | --- |
| slot 6 `WIDE_BODY` (`RI+0x464`) filled, presence only (0x005A96E4) | `<car>_DOORLINE_WIDEBODY` (0x006B7EE8) |
| otherwise, any of slots 1 `FRONT_BUMPER`, 2 `REAR_BUMPER`, 5 `BODY` filled with upgrade level above 0 (0x005A974F, 0x005A9753) | `<car>_DOORLINE_KIT` (0x006B7ED8) |
| otherwise | `<car>_DOORLINE` (0x006B7ECC) |

The precedence short-circuits, so a stock level-0 bumper does not select
the kit variant. A suffix is appended on every path, so the helper always
returns a name: the doorline can only disappear through a failed lookup.

### How each mask name is built

The gatherer never builds the mask string. `0x005AA009` takes the base hash
already stored in record `+0x00`, and continues it:

| Address | Instruction | Meaning |
| --- | --- | --- |
| 0x005AA004 | `mov edx, 0x6b7e14` | the literal `_MASK` |
| 0x005AA009 | `mov ecx, esi` | the base hash |
| 0x005AA00B | `mov al, 0x5f` | `_` |
| 0x005AA010 | `imul ecx, ecx, 0x21` | `h = h * 33` |
| 0x005AA016 | `add ecx, eax` | `h += byte` |
| 0x005AA018 | `mov al, [edx + 1]` | the next literal byte |
| 0x005AA01E | `jne 0x5aa010` | until the terminator |
| 0x005AA024 | `push ecx` | look the mask up |

No string buffer is touched in between. That gives the same value as
hashing the concatenation, because the hash is `h = h * 33 + byte` with no
length term, and the retail build depends on the two agreeing: `0x005A9C10`
and `0x005A97F0` build the full `_MASK` string and re-hash it from the seed
to reach the same records. `tests/skin_inputs_tests.cpp` pins the
equivalence.

### NUMCOLOURS and REMAP

There are exactly two writes to record `+0x1C` in the whole gatherer:

- `0x005A9F95`, the part's `NUMCOLOURS`, reached only by the vinyl-layer
  block. A missing attribute arrives as `xor eax, eax` at `0x005A9F91`, so
  the `test eax, eax` at `0x005A9F93` cannot tell absent from zero. Either
  way `0x005A9F98` falls through to the epilogue at
  `0x005A9F9A..0x005A9FA4`, which pops through `add esp, 0x3A0` and returns
  with `eax` still zero: **the whole gatherer gives up**. It is not a
  per-record skip.
- `0x005A9FD4`, the constant `0x10`, for indexes 0 and 5. For those two
  sources the `NUMCOLOURS` key is not even hashed, so they have no palette
  count to select.

`REMAP` is looked up for indexes 1..4 only (`0x005AA0AC`, `0x005AA0B5`) and
needs both attribute presence (`0x005AA0E8`) and a non-zero value. The
value is stored into record `+0x20` at `0x005AA10C` and tested at
`0x005AA10F`, which on zero jumps to `0x005AA1CC` - past the colour loop,
but into the record cursor at `0x005AA1D4`, so the layer is still counted.

Only past that test does the loop at `0x005AA134` read the four colours
from slot `42 + 4 * layer` (`0x005AA126`), packing `GLOSS`, `BLUE`, `GREEN`
and `RED` into one dword at `0x005AA13C..0x005AA19F`; a missing slot `k`
contributes `0xFF << (8 * k)` (`0x005AA1A7`), which makes the remap an
identity on lanes 0..2. So a layer that does **not** remap carries four
zeroed colours, not four substituted ones, and indexes 0 and 5 never remap
at all.

### Why a source is dropped

Five paths jump to `0x005AA1E0`, bypassing the `inc`/`add` at
`0x005AA1D4`:

| Address | Cause |
| --- | --- |
| 0x005A9FE0 | record `+0x00` is zero: index 6/7, index 0 with neither slot filled, or a vinyl layer with a missing part, a duplicate part, or no `TEXTURE` |
| 0x005A9FFF | the base name hash resolves to no texture |
| 0x005AA039 | the `_MASK` hash resolves to no texture |
| 0x005AA070 | the source `LockRect` returned a null `pBits` |
| 0x005AA0A7 | the mask `LockRect` returned a null `pBits` |

Vinyl layers are also de-duplicated before any name is built:
`0x005A9F1C` loads the slot dword, `0x005A9F30` compares it with each
earlier layer's slot dword, and `0x005A9F34` drops the layer on a match.
That is **part identity**, not the `TEXTURE` value, so two different parts
carrying the same `TEXTURE` value are both kept.

### What the gatherer does not do

- It reads no source or mask width or height. Every `+0x44`/`+0x46` read in
  the gatherer and in both composite routines is on the target
  (0x005A8AA4, 0x005A8AA8, 0x005A8EF8, 0x005A8EFC, 0x005A9488). The loops
  run `target_w * target_h` elements over every source, so a smaller source
  is read out of bounds. There is no retail behaviour to reproduce here; a
  port has to check this itself.
- It selects no mip level. The source and mask locks at `0x005AA045` and
  `0x005AA07C`, and the target locks at `0x005A8A95` and `0x005A8EDC`, all
  pass `Level = 0`, and neither composite contains a downsample loop.
- It converts nothing. `+0x0C` and `+0x14` are the `D3DLOCKED_RECT.pBits`
  of `IDirect3DTexture9::LockRect` (vtable `+0x4C`, unlocked through
  `+0x50` at `0x005AA283`), read at struct `+4`; `Pitch` at `+0` is never
  read. `+0x10` and `+0x18` are the texture objects' own `+0x74` palette
  pointers, copied verbatim, and both stores are skipped when the target's
  type byte is `0x20` (`0x005AA061`, `0x005AA098`).

## What reaches the compositor as what

The texture upload at `0x00407DB0` runs before any of this. For a record
whose format value is `0x29` it allocates `[ti+0x38] * 4` bytes
(`0x00407E21`), expands every level through the 256-entry palette at
`[ti+0x74]` one pixel at a time (`0x00407E70`, `0x00407E7B`), sets the
format to `0x15` (`0x00407EA8`) and writes
`mov byte ptr [esi+0x4A], 0x20` (`0x00407EB1`).

So on this build every vinyl and every vinyl mask arrives at the gatherer
as a 32-bit surface with no palette, which is why one composite can mix a
paletted vinyl with a 32-bit doorline, and why the type-8 path never runs
on retail PC data. The expansion copies a palette entry into a pixel
**whole**, so an expanded surface keeps exactly the byte-lane order the
palette had on disk.

`nfsu_skin_inputs` therefore converts in exactly three cases and refuses
everything else:

| Target | Record | What happens | Evidence |
| --- | --- | --- | --- |
| type 32 | A8R8G8B8 (code 0x20, D3DFORMAT 21) | stored little-endian dwords used unchanged | the upload leaves non-paletted records alone; 0x005A8A50 reads 4 bytes per element |
| type 32 | P8 (codes 0x08/0x80/0x81, D3DFORMAT 41) | each index expanded through the record's own palette, entry copied whole | 0x00407E70..0x00407EB1 |
| type 8 | P8 | index bytes passed through, palette loaded little-endian | layer record fields +0x0C, +0x10, +0x14, +0x18 |

A 32-bit surface is never reduced to indices for a type-8 target: no traced
instruction does that. The type-8 assembly path is unreachable on the
reference build and is covered by synthetic fixtures only.

## Source priority

Retail resolves a name hash through `0x005461C0`, which walks a global
bucket list from `[0x00740418]`, binary-searches each pack, and on a hit
unlinks the pack and relinks it at the head (`0x00546240`..`0x0054625A`).
New packs are appended at the tail, so pre-lookup order is registration
order; the car `TEXTURES.BIN` is requested at `0x0058BBDF` and `VINYLS.BIN`
at `0x0058BC27`, in the same function that later calls the gatherer
(`0x0058BE57`).

Which of the two wins for a hash they both hold is **not decidable from the
executable**: it depends on the whole prior lookup history, and every hit
reorders the list. It is also not decidable from the retail 240SX data,
because the two packs share no hash at all (see below).

`nfsu_skin_inputs` therefore does not reproduce retail lookup. It imposes a
deterministic rule over an explicit source list, the same one
`decode_render_texture_sources` uses: sources in the order given, packs
within a source in the order given, records in index order, and the first
record that *claims* a hash wins even when that record is unusable, so a
broken car texture is never silently replaced. Every other claimant is
reported in `also_claimed_by` with its source, pack and record index.

## Ownership

`AssembledSkinInputs` owns every buffer its `BodySkinRequest` points at.
The storage is private, one heap block per layer behind `unique_ptr`, and
the request is handed out by const reference, so:

- copying is deleted, because a copy could not own the buffers;
- moving is safe, because the pointees never move;
- a moved-from object is emptied rather than left with a live size and no
  layers, which would otherwise composite into a plain base-paint image;
- the request stays valid after the texture packs and their bytes are
  destroyed.

`validate_assembled_skin_inputs` re-checks that invariant from outside: one
storage per layer, every view pointing into its own storage, every view at
least `width * height` long, and only the fields the target type uses set.

## Retail cross-check, 240SX

Read-only. The files were hashed before and after and are unchanged:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `Cars/240SX/TEXTURES.BIN` | 697340 | `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA` |
| `Cars/240SX/VINYLS.BIN` | 15409440 | `46099AF77F100C3E09EBDBF4813631FFE3188CDE411F79D570C01B076E4EA1E3` |
| `Cars/TEXTURES.BIN` | 1230808 | `999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58` |

`nfsu_probe body-skin-inputs` assembled a type-32 request for car `240SX`
from those three sources, in that order, with `VINYL_LAYER0` selecting
`TRIBAL5` at `NUMCOLOURS` 16 and the plain doorline. It wrote nothing. The
records it selected:

| Role | Name | Hash | Source | Record | Format | Size | Mips | Info | Blob |
| --- | --- | --- | --- | ---: | --- | --- | ---: | ---: | --- |
| target | `DUMMY_SKIN1` | `0x530B82B0` | `Cars/TEXTURES.BIN` | 120 | code 0x20, D3D 21 | 512x512 | 1 | 24944 | 484456+1180 |
| vinyl_layer0 source | `240SX_TRIBAL5` | `0x4573BA32` | `VINYLS.BIN` | 275 | code 0x80, D3D 41 | 512x512 | 7 | 58696 | 5321701+13249 |
| vinyl_layer0 mask | `240SX_TRIBAL5_MASK` | `0x005C59FD` | `VINYLS.BIN` | 0 | code 0x08, D3D 41 | 512x512 | 7 | 24596 | 161024+21351 |
| doorline source | `240SX_DOORLINE` | `0x580576DB` | `TEXTURES.BIN` | 41 | code 0x20, D3D 21 | 512x512 | 7 | 8596 | 206612+68192 |
| doorline mask | `240SX_DOORLINE_MASK` | `0x699B6846` | `TEXTURES.BIN` | 49 | code 0x20, D3D 21 | 512x512 | 7 | 9588 | 285115+28988 |

Both paletted vinyl surfaces were expanded; neither doorline surface was.
The source uses 16 palette entries and the mask 256, matching the retail
distribution in [`FORMATS.md`](FORMATS.md). Two layers were assembled, in
gatherer order, owning 4194304 bytes, and the ownership invariant
validated.

A second run selected the wide-body doorline. Its source is record 65
(`0x9D21E7D1`) and its mask record 80 (`0xBFD2E2FC`). Both records store
the **same 23 characters**, `240SX_DOORLINE_WIDEBODY`, because the name
field truncated the mask's name; the probe reports `name_hash matches` for
the source and `name_hash differs` for the mask, and the pair still
resolves, because resolution is by hash and never by stored text.

The whole installation carries no hash that is in both
`Cars/240SX/TEXTURES.BIN` and `Cars/240SX/VINYLS.BIN`, and no hash appears
twice inside either pack, so no retail 240SX selection can observe the
source-priority rule.

## Observed, inferred, unknown

### Observed in the executable

Everything above that carries an address.

### Observed in retail data

The 240SX record tables, dimensions, format codes, blob offsets and hashes
in the cross-check above, and the absence of cross-pack hash collisions.

### Inferred

- That format code 0x20 with D3DFORMAT 21 is A8R8G8B8 stored as
  little-endian ARGB. This comes from the public D3DFORMAT numbering, not
  from the pixels (see [`FORMATS.md`](FORMATS.md)). The pass-through this
  layer performs does not depend on it: the bytes are copied as stored.
- That the eleven truncated vinyl mask names and the truncated doorline
  mask name are the full `<base>_MASK` strings. The hashes prove the
  pairing; the authoring strings themselves are not proven.

### Unknown

- Which palette lane order the type-8 target path expects. The type-32
  upload path copies entries whole into A8R8G8B8 pixels, whose stored
  B, G, R, A lanes are traced at 0x00407E70..0x00407EA8. For a type-8
  target, which takes the palette itself, nothing proves that path's lane
  expectation; `skin_palette_from_p8` loads the stored dwords unchanged.
- Which pack wins for a hash held by both the car `TEXTURES.BIN` and
  `VINYLS.BIN`. Neither the executable nor the retail data decides it.
- What the type-8 path is for. Nothing on this build reaches it.
- Whether the gatherer leaks a lock when a mask lock fails. `0x005AA0A7`
  abandons the record after the source was already locked at `0x005AA051`,
  and the record's `+0x04`/`+0x08` are left set while `+0x00` is re-zeroed.
  Traced, not observed. This layer takes no locks, so it cannot reproduce
  or contradict it.
- Whether a part database can produce a vinyl-layer selection this layer
  would accept but the gatherer would drop as a duplicate. The
  de-duplication is on part identity, which this layer never sees.
