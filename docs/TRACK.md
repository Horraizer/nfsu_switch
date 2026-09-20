# Track geometry: the 24-byte vertex and one bounded STREAM region

Clean-room notes on the track containers of Need for Speed: Underground
(PC). Everything here comes from reading a user-provided retail
installation and its executable read-only. No game data is stored in this
repository.

This note covers exactly two things:

1. the 24-byte track vertex layout and the field in the file that selects
   it, and
2. one explicitly bounded region of one `Tracks/STREAM*.BUN` file, decoded
   end to end into a draw list with cost statistics.

It deliberately stops there. Visibility groups, collision and the
streaming tables that decide which region is resident are not decoded, and
nothing here should be read as a claim about a complete track. The object
directory, the instance transforms and the bounding tree that place this
geometry in the world are decoded in
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md), from the `0x80034100` chunk that
sits beside each geometry root.

Reference samples for this document:

- File: `Tracks/STREAML1RB.BUN`
- Size: 30675616 bytes
- SHA-256: `2C960E19F2EFE9DD260C2E4AFFEF53205B49F666DEF0D0E9BC98DA373065FCC4`

- File: `Tracks/STREAML1RB_1210.BUN`
- Size: 8206776 bytes
- SHA-256: `45CFE82A4DFD0A2131CE86D680D715D8C290A327E934CC8FB321536741AD40F7`

- File: `Tracks/STREAML1RX.BUN`
- Size: 16732096 bytes
- SHA-256: `2F3CFBFA779076C7E0AE3AED825B821F030861F7C81FB83FF46FCF9FC1CF7AE1`

- File: `speed.exe`
- Size: 3178496 bytes
- SHA-256: `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`

Offsets are decimal absolute file offsets unless written as `+n`, which is
relative to the start of the named record. Executable addresses are
`speed.exe` virtual addresses; its image base is 0x00400000 and all three
sections map with `file_offset = VA - 0x400000`.

## Where track geometry lives

`Tracks/` holds five large containers and a set of small per-section
files. Chunk-id census of the five large ones, from bounded walks with
`nfsu_probe chunks`:

| File | Size | `0x80134000` roots | `0x80134010` meshes |
| --- | ---: | ---: | ---: |
| `STREAML1RA.BUN` | 92394688 | 491 | 5066 |
| `STREAML1RB.BUN` | 30675616 | 219 | 1534 |
| `STREAML1RC.BUN` | 28523156 | 181 | 1098 |
| `STREAML1RE.BUN` | 21216288 | 189 | 1003 |
| `STREAML1RX.BUN` | 16732096 | 151 | 365 |

The small per-section files (`STREAML1RB_1201.BUN`,
`STREAML1RC_1305.BUN`, `STREAML1RX_1099.BUN` and the rest) contain **no**
geometry at all: each is one `0xB3300000` texture pack followed by one
`0xB0300100` chunk. Many `STREAML1RA_1xxx.BUN` files are zero bytes. The
`TEX####TRACK.BIN` files are also texture packs, and small ones: three to
six records each.

So a geometry region always comes out of one of the five large files, and
its textures come from somewhere else. `speed.exe` builds those names at
runtime from `TRACKS\STREAM%s.BUN` (0x006C9B88) and
`TRACKS\STREAM%s_%d.BUN` (0x006C9B70); the literal `L1R` is not in the
image.

## How a track root differs from a car root

Track geometry reuses the car geometry chunk ids of `docs/FORMATS.md`, but
the container is not the same shape:

| | Car `GEOMETRY.BIN` | Track `STREAM*.BUN` root |
| --- | --- | --- |
| `0x00134003` mesh hash table | present | present |
| `0x00134004` mesh directory | present | **never present** (0 of 1231 roots) |
| `0x00134011` payload | 176 bytes, no prefix | 176-byte record at `align_up(abs, 16)` after `0x11` filler; payloads 176, 180, 184 or 188 |
| `0x00134012` texture list | always present, never empty | always present; the 272 empty meshes carry it with a zero-byte payload |
| `0x00134013` material-key list | always present | present in only 967 of 9066 meshes; absent when its count byte is zero |
| vertex stride | 36 | 24 (and 60 for the crowd meshes, see below) |

The header also carries `0x00134002`, which begins with four 32-bit words
and then a NUL-terminated source path. For the region decoded below it is
`Location1\RegionB\Pipeline\PC\eLabSceneryC.bin`, and the fourth word is
the mesh count.

`0x00134003` records are `(u32 mesh name hash, u32 0)`. Over all 1231
track roots the second word is always zero, the records never decrease,
and the record count always equals the mesh count of the root. The hashes
are the `0x00134011 +16` name hashes of the root's meshes. They are not
always distinct: 12 roots hold a duplicate, because two meshes in them
have the same name.

## The vertex format field, and where the stride comes from

The stride is **not** derived by dividing. It is a per-mesh field.

`0x00134900 +12` is a 32-bit vertex-format flags word. `speed.exe`
0x0040C890 reads it from the loaded object (`solid->flags`, +0x0C of the
`0x00134900` payload) and walks a four-row table at 0x00701440, twelve
bytes per row, taking the first row whose mask shares a bit with the
flags. The last row has mask 0 and always matches. Raw table bytes:

```text
00701440  01 00 00 00 04 00 00 00 00 00 00 00 40 00 00 00
00701450  02 00 00 00 00 00 00 00 80 00 00 00 05 00 00 00
00701460  00 00 00 00 00 00 00 00 00 00 00 00
```

| Row | Mask | Format index | Stride |
| ---: | --- | ---: | ---: |
| 0 | `0x00000001` | 4 | 60 |
| 1 | `0x00000040` | 2 | 24 |
| 2 | `0x00000080` | 5 | 36 |
| 3 | `0x00000000` (default) | 0 | 24 |

Each format index names a Direct3D 9 vertex declaration built once at
startup. The builder at 0x0040E180 loops seven formats, reads a 128-byte
element list per format from 0x007010C0, maps each element type through
0x00701470 (`D3DDECLTYPE`) and its size through 0x007014C0, accumulates
the running offset in `esi` (0x0040E25F) and stores that total as the
stride at descriptor +8 (0x0040E2C7). The size table at 0x007014C0 reads
`04 00 00 00 08 00 00 00 0C 00 00 00 10 00 00 00 04 00 00 00`, that is 4,
8, 12, 16 and 4 bytes for FLOAT1, FLOAT2, FLOAT3, FLOAT4 and D3DCOLOR.

The resulting seven declarations and their strides:

| Format | Elements | Stride |
| ---: | --- | ---: |
| 0, 1, 2, 3 | POSITION float3 @0, COLOR D3DCOLOR @12, TEXCOORD0 float2 @16 | 24 |
| 4 | POSITION, NORMAL, COLOR, TEXCOORD0, BLENDWEIGHT float3 @36, BLENDINDICES float3 @48 | 60 |
| 5 | POSITION float3 @0, NORMAL float3 @12, COLOR D3DCOLOR @24, TEXCOORD0 float2 @28 | 36 |
| 6 | POSITION, COLOR, TEXCOORD0, TEXCOORD1 @24, TEXCOORD2 @32, TEXCOORD3 @40 | 48 |

Format 5 is byte for byte the 36-byte car vertex already documented in
`docs/FORMATS.md`. Format 0 is the track vertex.

At draw time the stride is read back from memory, never as a constant:

```text
0040EEC4  mov  edi, [0x0073640C]       ; current vertex-format descriptor
0040EECA  mov  edi, [edi + 8]          ; stride
0040EED5  push edi                     ; Stride
0040EEDC  call dword ptr [edx + 0x190] ; IDirect3DDevice9::SetStreamSource
```

Vertex buffers are created with `FVF = 0` (0x0040C7D3) and index buffers
with `D3DFMT_INDEX16` (0x0040C751), so the engine uses vertex
declarations and 16-bit indices. The constant 0x152, which would be the
FVF spelling of the 36-byte car vertex, does not occur as an instruction
immediate anywhere in the image. There is no `div` or `idiv` in the whole
geometry and renderer range 0x0040C000..0x00411000, so the game never
computes a stride from a payload size.

### The rule against the data

Applying the table to every mesh of the five large track files and of five
car `GEOMETRY.BIN` files, and comparing with the stride the payload
implies, gives **10743 agreements and 0 disagreements**. No flags value
outside `{0x00000000, 0x00000040, 0x00000080, 0x00000001, 0x00001000,
0x00004081, 0x00004180, 0x00008000, 0x0000C180}` was observed.

Flags census of `STREAML1RB.BUN`, 1534 meshes:

| Flags | Row taken | Stride | Meshes |
| --- | --- | ---: | ---: |
| `0x00000000` | default | 24 | 1293 |
| `0x00000040` | `0x40` | 24 | 68 |
| `0x00001000` | default | 24 | 24 |
| `0x00008000` | default | 24 | 3 |
| `0x00004081` | `0x01` | 60 | 146 |

Car files use only `0x00004180` and `0x0000C180`; both take the `0x80` row
and give 36. Note that bit `0x01` wins over bit `0x80`, which is why
`0x00004081` is 60 and not 36.

Over all five large track files: 8064 meshes of stride 24, 730 of stride
60 (the crowd characters, which add three blend weights and three bone
indices), 272 empty, and no mesh of stride 36.

The independent derivation `(vertex payload size - alignment prefix) /
vertex count` gives the same answer for all 10743 non-empty meshes with
remainder zero, so the parser asserts it as a free cross-check. It is not
the source of truth, because it is undefined for the 272 empty meshes and
because it is a coincidence of the data, not a rule of the format.

## The 24-byte vertex

| Offset | Type | Field |
| --- | --- | --- |
| 0 | f32 | px |
| 4 | f32 | pz |
| 8 | f32 | py |
| 12 | u8[4] | color |
| 16 | f32 | u |
| 20 | f32 | v |

The slot order is the same as the car vertex: the car record is this one
with a second float triple, the normal, inserted after the position. Field
names follow `MeshVertex`, so file slot 1 is `pz` and file slot 2 is `py`.

Raw sample, the first vertex of `XO_CONSTRUCAHEAD_1B_00`
(`STREAML1RB.BUN`, geometry root 3072, `0x00134B01` payload at 3672,
128-byte aligned data at 3712):

```text
3712  B8 A2 83 BE  DC 48 48 BF  5C 78 CD BC  28 28 28 FF  00 00 01 3F  00 00 00 00
      px=-0.25708   pz=-0.78235   py=-0.02508  28 28 28 FF  u=0.50391     v=0.0
```

Evidence for each field group:

- **Bytes 0..11 are the position.** For every `0x00134B02` material
  record, the minimum and maximum of these three slots over the vertices
  its index range references reproduce the record's `bounds_min` at +0 and
  `bounds_max` at +16, in the same slot order, inflated by exactly 0.01 on
  each side. Over 23458 material records of the five large track files,
  23442 agree to better than 1e-5 and twelve more to better than 1e-3
  (those are `SKYDOME`-class meshes whose coordinates are near 6000, where
  one f32 ULP is 7.4e-4). Four records disagree by 0.01 or more; in each of
  them exactly one of the two vectors is wrong while the other matches to
  1.7e-6, and every sibling record of the same mesh matches, so they look
  like stale exporter output. None of the four is inside the region
  decoded below.
- **Bytes 12..15 are not floats.** Read as an f32 over 142168 vertices of
  `STREAML1RX.BUN`, 100.0000 percent are NaN or have magnitude above 1e30.
  Read as four bytes they are plainly a colour: 938 distinct patterns, 73
  percent of them pure grey (`b12 == b13 == b14`), the most common being
  `72 72 72 FF`, `51 51 51 FF` and `41 41 41 FF`. Byte 15 is a real
  channel rather than padding: it is 0xFF in every vertex of
  `STREAML1RX.BUN`, but `STREAML1RA.BUN` also has 72126 vertices with 128,
  18999 with 110, 8230 with 91 and 6110 with 230. The retail declaration
  calls the four bytes one `D3DCOLOR`, so the channel order is that of a
  little-endian ARGB word; this decoder keeps the bytes as stored and does
  not name them, exactly as it does for the car vertex.
- **Bytes 16..23 are texture coordinates.** 95 to 98 percent of the
  vertices of `STREAML1RA/RB/RC/RE` have both inside [-4, 4] and 69 to 81
  percent inside [0, 1]. The visible outliers are deliberate tiling: in
  `STREAML1RX.BUN` only two meshes exceed 64, `TRN_GUARDRAIL_01` reaching
  869 and `ARC_BUILD01` reaching 2001.

The example in issue #10 is `TRSD_TERRAININSIDE` in `STREAML1RX.BUN`,
geometry root 9728: `0x00134B01` payload at 10664, payload size 5560,
128-byte aligned data at 10752, prefix 88, vertex count 228, and
`(5560 - 88) / 228 = 24` exactly. Three other retail meshes have the same
triple.

## Counts

`0x00134900` +16, +36 and +52 are the material, face and vertex counts, as
for cars. They are 16-bit values stored in 32-bit words: the upper half is
zero in 9065 of the 9066 track meshes.

The exception is `TRN_GUARDRAIL_01` in `STREAML1RX.BUN`, geometry root
1182816, whose `0x00134900` record is at 1183280 and whose vertex count
word at 1183332 reads `04 B0 FF FF`, that is `0xFFFFB004`. The low half is
45060, and 45060 is the count the file agrees with: the vertex payload at
1183352 is 1081448 bytes with an 8-byte prefix, and `1081440 = 45060 * 24`
exactly, while the largest index in its `0x00134B03` is 45059. The word
looks like a signed 16-bit count that the exporter sign-extended.

**This decoder refuses that word** rather than guessing how the game reads
it, and says so:

```text
mesh 0 'TRN_GUARDRAIL_01' (container at offset 1183048): vertex count word
at file offset 1183332 is 0xFFFFB004; its high half must be zero
```

The other six meshes of that root still decode. `speed.exe` compares the
loaded vertex count with 0xFFFF at 0x0040EF27, which is consistent with a
16-bit reading but does not by itself settle whether the field is a `u16`,
an `i16` or a `u32`, so the question is left open here.

## Empty meshes

272 of the 9066 track meshes are placeholders: all three counts are zero,
the `0x00134B01` payload is nothing but `0x11` filler, and `0x00134B02`
and `0x00134B03` are 8 bytes each, which is their alignment prefix and no
data. Each still carries a `0x00134012` chunk, with a zero-byte payload
and a zero count byte, and no `0x00134013`. They are structurally sound,
so the decoder records them with `empty == true` and `valid == false` and
raises no issue; they contribute nothing to the geometry or to the draw
list. Counts that are neither all zero nor all positive are refused.

Their names say what they are: `__DELETEME`, and crowd slots such as
`FEMALE_30`, `FEMALE_32` and `FEMALE_41`.

## Parser rules (`nfsu::parse_track_region`)

The decoder is `include/nfsu/track.h` and `src/track.cpp`. It never
throws for malformed input; every problem is a `TrackIssue` with an
absolute file offset.

**Bounded by construction.** `parse_track_region(data, size, base_offset,
limits)` sees exactly the region's bytes and is told the absolute file
offset they came from. `load_track_region` seeks to that offset and reads
exactly that many bytes; the rest of the file is never read and no
directory is ever scanned. `base_offset` is not cosmetic: every alignment
rule in the format is relative to the start of the *file*, so a decoder
that aligned within the region would put the vertex data in the wrong
place for any root that does not start on a 128-byte boundary.

Structure:

1. The region must be a whole number of top-level chunks. Trailing bytes
   shorter than a header, and any chunk whose payload reaches past the
   region end, are refused.
2. Every `0x80134000` at the top level of the region is parsed. A region
   with none is refused.
3. Inside a root, exactly one `0x80134001` holding exactly one
   `0x00134003` is expected, and any `0x00134004` is refused, because a
   track root has none and this decoder does not read one. A root with no
   header at all is allowed; its meshes decode and the hash table is
   simply not verified.
4. The hash table must have one record per mesh container of the root,
   zero second words, and non-decreasing hashes, and its multiset of
   hashes must equal the multiset of the meshes' `0x00134011 +16` name
   hashes. Equal hashes are matched to meshes in source order, so
   `TrackMesh::hash_record_offset` is deterministic even for the 12 retail
   roots with a duplicate.

Per mesh:

| Chunk | Data offset | Payload size |
| --- | --- | --- |
| `0x00134011` | `align_up(payload_offset, 16)` | `prefix + 176` |
| `0x00134900` | `align_up(payload_offset, 16)` | `prefix + 64` |
| `0x00134B01` | `align_up(payload_offset, 128)` | `prefix + vertex_count * 24` |
| `0x00134B02` | `align_up(payload_offset, 16)` | `prefix + material_count * 60` |
| `0x00134B03` | `align_up(payload_offset, 16)` | `prefix + face_count * 6 + tail` |

Every prefix byte must be `0x11`; positions are never found by scanning
for `0x11`. `tail` is 2 when `face_count * 6` is not a multiple of 4 and 0
otherwise, and the tail bytes must be zero.

5. The mesh name is the last 28 bytes of the 176-byte record, NUL
   terminated, printable ASCII.
6. A `0x00134012` or `0x00134013` chunk, when present, must hold exactly
   `count * 8` bytes, where count is its byte of the 176-byte record
   (+25 and +26), and every second word must be zero. A non-zero count
   requires the chunk. A zero count allows either shape, because retail
   uses both: every track mesh carries a `0x00134012`, an empty one with
   a zero-byte payload, while a mesh with no material keys carries no
   `0x00134013` at all.
7. The vertex-format word selects a stride through the retail table. Any
   stride other than 24 is refused with a message naming the flags, the
   retail format index, the selector row and that stride. Nothing is
   decoded from a layout this repository has not proven.
8. Counts are refused if their high half is set or if they exceed the
   per-mesh limits, and the three must be all zero or all positive.
9. Every vertex float must be finite.
10. Every `0x00134B03` index must be below the mesh vertex count.
11. Material records must start at index 0, each `first_index` must be the
    running total of the previous index counts, each count must be a
    non-zero multiple of 3, the counts must add up to exactly
    `3 * face_count`, every texture slot must be inside the mesh's
    `0x00134012` list, and every material-key slot must be 0xFF or inside
    the `0x00134013` list.
12. Region-wide budgets bound total vertices, total triangles, chunk
    count, mesh count and region bytes. A mesh that would exceed one is
    refused by name; the ones that fit still decode.

Every multiplication and addition that sizes a buffer is overflow-checked;
a mesh that fails any rule keeps no decoded data and contributes nothing
to the cost statistics.

## The prepared draw list (`nfsu::prepare_track_region`)

`include/nfsu/track_scene.h` turns a report and a `RenderTextureSet` into
a `PreparedTrackRegion`: copies of the valid meshes, one `TrackDraw` per
`0x00134B02` record in mesh and record order, one copy of each resolved
texture in order of first use, and the cost statistics below. Material
binding uses the same `nfsu::resolve_material` the car scene uses; the
section after this one states the source precedence, the collision
outcomes and the malformed-source boundary for a region, which differ from
the car viewer in three ways. Invalid meshes and empty meshes are recorded
separately and never drawn.

`validate_prepared_track_region` re-checks every invariant a backend would
rely on: ascending unique meshes inside the source and disjoint from both
skipped lists, one ordered draw per material record, index ranges inside
the mesh index stream, every triangle index inside the mesh vertex count,
agreement between each draw and the untouched `0x00134B02` record it
carries, binding and texture provenance (a draw that is not resolved must
carry none), first-use texture handles, one canonical binding per texture
hash, an unresolved hash that no prepared texture claims, an unresolved
list that is exactly the set of unresolved draws, texture sizes, and the
recomputed cost statistics. `prepare_track_region` runs it before
returning true.

`BindingKind::invalid_slot` cannot come out of `parse_track_region`,
because `parse_materials` already refuses a mesh whose texture slot is
outside its own list. The prepared layer still handles it, because it is
a public API that takes a report it did not build.

Cost statistics, all deterministic:

- region bytes read, chunks walked, geometry roots;
- meshes, valid meshes, empty meshes;
- vertices, triangles, material records;
- source vertex bytes (`vertices * 24`) and source index bytes
  (`triangles * 6`), and what the decoded form holds in memory;
- distinct texture hashes, largest mesh, and the axis-aligned bounds over
  every valid vertex;
- draws, drawable draws, and draws by binding kind;
- drawn triangles and the largest single draw;
- retained textures and their RGBA8 bytes;
- texture binds, the number of times a backend walking the draws in this
  order and binding only on a change would call `SetTexture`, counting
  every unresolved or unusable group as one shared fallback binding.

## Texture sources for a region

The sources a region resolves against are given explicitly, after the region
selector, and **their command-line order is their priority**. No file is
searched for and no directory is scanned. The decoder never compares two
records' contents: when two sources carry one hash, the earlier source wins,
and the later record is dropped whole and listed in
`RenderTextureSet::shadowed`.

The rules are `decode_render_texture_sources`, stated in full in
[`RENDERER.md`](RENDERER.md) under "Texture sources and precedence". Three
things differ for a region, and reading that section alone gets each of them
wrong:

- **Every** track source is filtered by the region's wanted hashes
  (`nfsu::track_region_texture_hashes`), the first one included. The car
  viewer decodes its source 0, the car's own `TEXTURES.BIN`, in full. A
  record of a track source that claims no wanted hash is never decoded,
  which is why the other 362 records of `STREAML1RB_1210.BUN` cost nothing
  below. It still claims its hashes against later sources.
- A source named twice is accepted. The car viewer makes a repeated
  `--shared-textures` file a usage error; `track-region` reads it again as
  the next source, where the first copy shadows every record of the second.
  That is a wasted read, not a wrong answer.
- A texture-source problem does not change the exit code. `track-region`
  exits on the region report alone: `0` when the region parsed cleanly and
  `3` when it did not. Source issues, shadowed records and blocked sources
  are printed and nothing more, so a caller must read the output rather than
  the status. The car viewer exits 3 for a source issue.

What a `TrackDraw` ends up with:

| The hash the material's slot names is | binding | `texture` | provenance |
| --- | --- | --- | --- |
| carried by exactly one usable record, in the earliest source that claims it | `resolved` | the first-use handle | that record's source, pack and record |
| carried by two or more kept records, or only by records that could not be decoded, or by an entry of `unusable_claims` | `unusable` | `kNoHandle` | none, all three fields 0 |
| claimed by no source at all | `unresolved` | `kNoHandle` | none |
| outside the mesh's own hash list | `invalid_slot` | `kNoHandle` | none |

Only `unresolved` hashes reach `PreparedTrackRegion::unresolved_hashes`. An
ambiguous hash is deliberately not one of them: something on disk claims it,
so the region must not report it missing, and it must not be drawn from a
guess either. Both kinds stay `drawable` and share the one fallback binding.

A record's contents never break a tie and never promote a lower-priority
source. An invalid record of an earlier source still shadows a valid record
of a later one, and the hash is `unusable` rather than taken from the file
that could have supplied it.

### Which malformations a source survives

One row below makes a source incomplete, which stops every source behind it
from being read at all. The rest leave the source complete and cost exactly
the records they name. `tests/track_tests.cpp` builds each case as bytes and
parses it with the real reader, because a hand-built `TexturePack` would
skip the reader and prove nothing about a malformed file.

| In a source file | Reported as | Complete | The draw gets |
| --- | --- | --- | --- |
| a record count the hash, info or format tables disagree on | a pack issue; `tables_parsed` stays false | **no** | `unresolved`: no record was ever listed |
| a chain that starts outside the `0x33320002` payload, or a payload shorter than the chain a record declares | a pack issue at info +48; the record is invalid | yes | `unusable` |
| a chain offset that is not a multiple of 128 | a pack issue at info +48; the record is invalid | yes | `unusable` |
| a declared top level (info +64) or total size (info +56) that disagrees with the dimensions and mip count | a pack issue at info +56; the record is invalid | yes | `unusable` |
| a zero width or height | a pack issue at info +68; the record is invalid | yes | `unusable` |
| one hash on two records of a pack, or of two packs of one file | a pack or set issue; **both records stay valid and both are kept** | yes | `unusable` |
| no `0xB3300000` root anywhere in the file | a set issue | yes: it claims nothing | whatever a later source has |

The repeated hash is the row that surprises. The reader reports it and keeps
both records, because nothing tells it which one the game would take, and no
later stage resolves it either: it becomes an `unusable` binding and a
fallback-coloured draw. An incomplete source is the only one that costs
anything beyond its own records.

### What the retail corpus does

Collisions are not hypothetical here, so the synthetic tests pin a rule the
installation exercises. The two-source command below reports `shadowed 54`,
and `STREAML1RA.BUN` on its own holds 220 distinct hashes that appear in
more than one of its 141 packs, one `repeats hash ... ambiguous` line each
on standard error.

Four of the region's eleven wanted hashes are carried by both files, so the
command-line order decides which copy four draws use. Running each source
alone says so twice over. Every row is
`track-region Tracks/STREAML1RX.BUN 9728 784032` with the sources named:

| Sources, in order | decoded | shadowed | skipped | resolved / unresolved |
| --- | ---: | ---: | ---: | ---: |
| `STREAML1RX_1099.BUN` | 7 | 0 | 63 | 108 / 6 |
| `STREAML1RA.BUN` | 6 | 0 | 1630 | 11 / 103 |
| `STREAML1RX_1099.BUN`, `STREAML1RA.BUN` | 9 | 54 | 1643 | 112 / 2 |
| `STREAML1RA.BUN`, `STREAML1RX_1099.BUN` | 9 | 52 | 1645 | 112 / 2 |
| `STREAML1RX_1099.BUN` twice | 7 | 70 | 63 | 108 / 6 |

`decoded` counts wanted records that were kept, and the first source is
never shadowed, so across rows 1 and 3 the second file contributes
`9 - 7 = 2` of the 6 it contributes alone: four of its wanted records were
shadowed. `skipped` confirms it from the other side, because a shadowed
record is not also counted as skipped: `63 + 1630 - 1643 = 50` of the 54
shadowed records would otherwise have been skipped as unwanted, leaving 4
that were wanted. Row 4 gives the same 4 the other way round
(`9 - 6 = 3` of `_1099`'s 7, and `63 + 1630 - 1645 = 48` of 52).

Both orders resolve 112 draws and decode 186368 bytes, so the collision
decides which file four textures come from, not how many there are. Whether
the two copies hold the same pixels was not measured: comparing them means
writing decoded retail pixels somewhere, and nothing here may hold those.

The last row is the repeated-source case: naming one file twice shadows all
70 of its records the second time and changes nothing else. Both commands
exit 0.

## The decoded region

```text
file        Tracks/STREAML1RB.BUN
offset      23015540
size        178268
```

That range is one `0x80134000` chunk: its header is at 23015540 and its
payload size is 178260. The region holds 184 chunks and one geometry root
whose `0x00134002` path is `Location1\RegionB\Pipeline\PC\eLabSceneryC.bin`.

| | |
| --- | ---: |
| Meshes | 22 |
| Valid meshes | 22 |
| Empty meshes | 0 |
| Vertices | 5935 |
| Triangles | 3725 |
| Material records | 70 |
| Distinct texture hashes | 40 |
| Source vertex bytes | 142440 |
| Source index bytes | 22350 |
| Largest mesh | 1420 vertices, 994 triangles |

Twenty meshes have vertex-format flags `0x00000000` and two have
`0x00000040`; both take a 24-byte row. Two meshes carry a one-entry
`0x00134013` list and the other twenty carry none. The hash table verifies
against all 22 mesh name hashes. Bounds over every vertex:

```text
px -31.4546 .. 809.777   py -23.2836 .. 148.82   pz -26.9893 .. 131.309
```

Resolved against one explicit texture source, `Tracks/STREAML1RB_1210.BUN`
(8206776 bytes, SHA-256
`45CFE82A4DFD0A2131CE86D680D715D8C290A327E934CC8FB321536741AD40F7`), which
holds one `0xB3300000` pack of 402 records in the raw storage layout of
issue #7:

| | |
| --- | ---: |
| Wanted hashes | 40 |
| Decoded textures | 40 |
| Decoded texture bytes | 3006464 |
| Records skipped as not wanted | 362 |
| Draws | 70 |
| Drawable draws | 70 |
| Resolved / unresolved / unusable / invalid slot | 70 / 0 / 0 / 0 |
| Drawn triangles | 3725 |
| Largest draw | 368 triangles |
| Texture binds | 69 |
| Vertex buffer bytes | 142440 |
| Index buffer bytes | 22350 |

Only the 40 hashes this region names are decoded, so the other 362 records
of the source cost nothing.

### A second region, for the unresolved and multi-source paths

```text
file        Tracks/STREAML1RX.BUN
offset      9728
size        784032
sources     Tracks/STREAML1RX_1099.BUN, then Tracks/STREAML1RA.BUN
```

463 chunks, 57 meshes, all valid, 27124 vertices, 17266 triangles, 114
material records, 11 distinct texture hashes. Nine resolve and two do not:
`0x49D49894` on `TRSD_GRADIENTS` and `0x4BFF3281` on
`XW_OILLINKFENCEA_1B_00_DEIN`. Neither hash exists in any texture pack of
the installation, so they are left unresolved rather than substituted.
114 draws, 112 resolved and 2 unresolved, 109 texture binds, 650976 vertex
buffer bytes and 103596 index buffer bytes. The second source reports 220
ambiguous hashes of its own on standard error, and 54 of its records are
shadowed by the first source. Neither stops the run. Four of the eleven
wanted hashes are carried by both sources, so the order they are named in
decides which file four of these draws take their pixels from; "Texture
sources for a region" above measures that.

### Whole-corpus check

The decoder is meant for one region at a time, but running it once over
every geometry root of the five large files is the strongest check that
its rules match the data. Over all **1231 roots and 9066 meshes**:

| | |
| --- | ---: |
| Roots whose `0x00134003` table verified | 1231 of 1231 |
| Meshes decoded | 8063 |
| Meshes recorded as empty | 272 |
| Meshes refused | 731 |
| Vertices decoded | 2539149 |
| Triangles decoded | 1656490 |
| Material records decoded | 23435 |

Every one of the 731 refusals falls into exactly two documented
categories, and nothing else was reported:

- **730** meshes whose vertex-format word selects a retail format with a
  stride other than 24. These are the crowd characters, all with flags
  `0x00004081`, which takes the `0x01` row and its 60-byte layout.
- **1** mesh, `TRN_GUARDRAIL_01`, whose vertex count word has a non-zero
  high half.

8063 + 272 + 731 = 9066, and 8063 + 1 = 8064, the number of 24-byte
meshes in the corpus.

A region may hold more than one root. `nfsu_probe track-region
Tracks/STREAML1RC.BUN 11844 279696` covers three `0x80134000` roots and
the unrelated top-level chunks between them: 100 chunks, 9 meshes, all
valid, all three hash tables verified, 8387 vertices, 5814 triangles, 13
material records, and no issue.

Pointed at a car instead, the track decoder refuses everything, which is
the intended behaviour: `nfsu_probe track-region
Cars/240SX/GEOMETRY.BIN 0 4704700` reports 400 meshes, 0 valid, and 401
issues, of which 400 name vertex format word `0x00004180`, retail format
5, selector row `0x00000080` and its 36-byte stride, and one names the
`0x00134004` mesh directory that a car root has and this decoder does not
read. `nfsu::parse_geometry` is untouched and still decodes that file
normally.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and
no game data, screenshot or exported asset may be committed.

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-region X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 \
    X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN

build/nfsu_probe track-region X:/Games/NFSU/Tracks/STREAML1RX.BUN 9728 784032 \
    X:/Games/NFSU/Tracks/STREAML1RX_1099.BUN X:/Games/NFSU/Tracks/STREAML1RA.BUN

build/nfsu_probe track-region X:/Games/NFSU/Tracks/STREAML1RX.BUN 3766272 257312
```

Expected:

1. The first command exits 0 and writes nothing to standard error. Its
   summary lines are exactly the numbers in the table above: `meshes: 22
   valid 22 empty 0 hash table verified`, `cost: region bytes 178268 chunks
   184 vertices 5935 triangles 3725 materials 70`, `textures: wanted 40
   decoded 40 bytes 3006464`, and `draws: 70 drawable 70 resolved 70
   unresolved 0 unusable 0 invalid slot 0`.
2. The second command exits 0 and ends with `draws: 114 drawable 114
   resolved 112 unresolved 2`, followed by `unresolved 0x49D49894` and
   `unresolved 0x4BFF3281`. Its second source reports ambiguous hashes of
   its own on standard error; those records are shadowed and the run still
   succeeds.
3. The third command exits 0, writes nothing to standard error, and prints
   `meshes: 110 valid 58 empty 52 hash table verified` and `cost: region
   bytes 257312 chunks 886 vertices 7311 triangles 5145 materials 74`. It
   is the empty-mesh case: 52 of its 110 meshes are placeholders and none
   of them is an error.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged
   afterwards.

To find the offset and size of another region, list the container first
and pick one `0x80134000` line; the size to pass is its payload size plus
the 8-byte header:

```text
build/nfsu_probe chunks X:/Games/NFSU/Tracks/STREAML1RB.BUN 100000
```

A region that is not a whole number of chunks, or that holds a mesh this
decoder does not read, is refused with an exact diagnostic rather than
decoded partially. Two examples from the same installation:

- `nfsu_probe track-region .../STREAML1RX.BUN 4061056 27700` refuses its
  single mesh `CROWD_FEMALE_02`, naming vertex format `0x00004081`, retail
  format 4, selector row `0x00000001` and its 60-byte stride.
- `nfsu_probe track-region .../STREAML1RX.BUN 1182816 1622276` decodes six
  of seven meshes and refuses `TRN_GUARDRAIL_01` for the sign-extended
  count word described above.

## Observed

- Track geometry lives only in the five large `STREAML1R*.BUN` files; the
  per-section `.BUN` files and the `TEX####TRACK.BIN` files are texture
  packs.
- Track roots never carry `0x00134004`; `0x00134003` alone indexes the
  meshes, one non-decreasing record per mesh, duplicates allowed.
- All 9066 meshes of the five large files carry a `0x00134012`; 967 carry
  a `0x00134013`. The 272 empty meshes carry a zero-byte `0x00134012`.
- `0x00134011` is a 176-byte record at a 16-byte aligned absolute offset,
  with `+16` the mesh name hash, `+25` and `+26` the two list counts and
  the last 28 bytes the name.
- `0x00134900 +12` selects the vertex format; the retail table and the
  payload arithmetic agree on 10743 of 10743 meshes.
- The 24-byte layout is position, colour, one 2-float texture coordinate,
  with the same slot order as the car vertex.
- `0x00134B02` bounds are the vertex bounds of the record's index range,
  inflated by 0.01 per axis.
- All 7110099 track indices are below their mesh's vertex count.
- Every track texture pack lacks `0x33310003`, so all of them use the raw
  storage path recovered in issue #7.
- Texture hashes collide across and inside source files. `Tracks/STREAML1RA.BUN`
  holds 220 distinct hashes that appear in more than one of its 141 packs,
  and 54 of its records repeat a hash of `Tracks/STREAML1RX_1099.BUN`. A
  collision is ordinary in this corpus rather than an edge case.

## Inferred

- `0x00134900 +12` behaves like a vertex-format flag word in which bit
  `0x80` adds a normal (+12 bytes) and bit `0x01` adds three blend weights
  and three blend indices (+24 bytes), giving
  `24 + 12 * ((b >> 7) & 1) + 24 * (b & 1)`. Only four low-byte values
  were ever observed, so other bit combinations are untested and the
  decoder uses the table, not this formula.
- `TRN_GUARDRAIL_01`'s count word is a sign-extended 16-bit value.
- The two unresolved hashes of the second region are generated at runtime
  rather than missing from disk; the same pattern exists for cars.

## Unknown

- Every field of the 176-byte `0x00134011` record except `+16`, `+25`,
  `+26` and the name. `+20` equals the face count in 8984 of 9066 track
  meshes, so it is not reliably that field. `+32..+68` look like a
  bounding box with the same 0.01 inflation.
- Every field of the 64-byte `0x00134900` record except `+12`, `+16`,
  `+36` and `+52`. `+8` is 16 in every mesh seen.
- The meaning of vertex-format bit `0x40`, which changes the declaration
  but not the stride, and of bits `0x1000`, `0x4000` and `0x8000`.
- The colour byte order and meaning, as for the car vertex.
- `0x00134002` beyond its first four words and the source path.
- How the game reads a count word whose high half is set.
- The `0x00E34010`, `0x00034035` and `0xB0300100` chunks that sit beside
  the geometry roots, and the `0x80034020` / `0x00039200` family whose
  handler is at 0x00472B05. The `0x80034100` chunk beside them is decoded
  in [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md).
- Everything about streaming: which regions are resident together and what
  the `_NNNN` section numbers select. Nothing in this note decodes a
  visibility group or collision.
