# File formats

Clean-room notes on Need for Speed: Underground (PC) data formats. Everything
here comes from inspecting a user-provided retail installation with
`nfsu_probe`. No game data is stored in this repository.

Reference samples for this document:

- File: `Cars/240SX/GEOMETRY.BIN`
- Size: 4780816 bytes
- SHA-256: `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`

- File: `Cars/240SX/TEXTURES.BIN`
- Size: 697340 bytes
- SHA-256: `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA`

All offsets below are decimal absolute file offsets unless stated as
`+n`, which is relative to the start of the named record or payload.

## Chunk framing

Every chunk is an 8-byte header followed by its payload:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 LE | Chunk ID. Bit 31 set means the payload is a list of child chunks. |
| 4 | u32 LE | Payload size in bytes, excluding the header. |

The generic walker (`nfsu_probe chunks`) parses the sample into 4402 chunks
with no bound violations.

## GEOMETRY.BIN (car geometry)

### Observed

Chunk tree of the sample (depth shown by indentation, unrelated chunks
omitted):

```text
0x80134000  geometry root (file offset 0)
  0x80134001  header container (0x00134002, 0x00134003, 0x00134004, 0x80134008)
  0x00000000  padding chunk, 0 to 124 bytes, between most children (384)
  0x80134010  mesh container, 400 in the sample
    0x00134011  mesh metadata, 176 bytes in every sample mesh
    0x00134012  texture hash list, 8 to 88 bytes (multiples of 8)
    0x00134013  unknown hash list, 8 to 56 bytes (multiples of 8)
    0x80134100  geometry container, exactly one per mesh
      0x00134900  geometry metadata
      0x00134B01  vertex buffer
      0x00134B02  material records
      0x00134B03  face (index) buffer
    0x00134017, 0x00134018, 0x00134019  unknown, in 115 meshes
    0x0013401A  unknown, in 60 meshes
0x80034020  second top-level container (0x00039200, 0x00039201, 0x00039202)
```

Every one of the 400 mesh containers holds exactly one of each of
0x00134011, 0x00134012, 0x80134100, 0x00134900, 0x00134B01, 0x00134B02 and
0x00134B03.

Mesh metadata 0x00134011:

- The mesh name is the final 28 bytes of the payload, NUL-terminated ASCII.
  Names are truncated to 27 characters (for example
  `240SX_DECAL_LEFT_QUARTER_RE`).
- `+25` (u8) is the number of 0x00134012 entries in every sample mesh.

Texture hash list 0x00134012:

- `payload_size / 8` entries of (u32 LE texture hash, u32 LE 0). The second
  word is 0 in every entry of the sample.

Alignment prefix: in 0x00134900, 0x00134B01, 0x00134B02 and 0x00134B03 the
record data
starts at the first absolute file offset inside the payload that is a
multiple of a fixed alignment. Every payload byte before that offset is 0x11.
With `align_up(x, a)` = the smallest multiple of `a` that is `>= x`:

| Chunk | Data offset | Payload size |
| --- | --- | --- |
| 0x00134900 | `align_up(payload_offset, 16)` | `prefix + 64` |
| 0x00134B01 | `align_up(payload_offset, 128)` | `prefix + vertex_count * 36` |
| 0x00134B02 | `align_up(payload_offset, 16)` | `prefix + material_count * 60` |
| 0x00134B03 | `align_up(payload_offset, 16)` | `prefix + face_count * 6 + tail` |

`tail` is 2 when `face_count * 6` is not a multiple of 4 (odd face counts)
and 0 otherwise. All 400 sample meshes match these rules exactly.

Geometry metadata 0x00134900:

- The record is exactly 64 bytes. Payload is 64 bytes (201 meshes, already
  aligned) or 72 bytes (199 meshes, 8-byte prefix).
- Offsets below are relative to the aligned record start:

| Offset | Type | Meaning |
| --- | --- | --- |
| 16 | i32 LE | Material record count (0x00134B02) |
| 36 | i32 LE | Face (triangle) count |
| 52 | i32 LE | Vertex count |

Material records 0x00134B02 (60 bytes each, 1242 in the sample):

| Offset | Type | Field |
| --- | --- | --- |
| 0 | f32[3] | bounds minimum (source slot order, not interpreted) |
| 12 | u32 | index count, a multiple of 3 |
| 16 | f32[3] | bounds maximum (not interpreted) |
| 28 | u32 | texture slot: index into this mesh's 0x00134012 list |
| 32 | u32 | unknown slot (see Unknown) |
| 36 | u8[16] | unknown, kept raw |
| 52 | u32 | first index into the 0x00134B03 index stream |
| 56 | u32 | flags, unknown, kept raw |

In every sample mesh the records are in order, each first index equals the
sum of the previous index counts, the counts add up to exactly
`3 * face_count`, and every texture slot is below the 0x00134012 entry count.

Vertex buffer 0x00134B01:

- Observed prefixes: 8 to 120 bytes. The prefix is never empty in this
  chunk order: the metadata record ends on a 16-byte boundary and the vertex
  chunk header is 8 bytes.
- In 5 sample meshes the first vertex byte is itself 0x11, so scanning for
  leading 0x11 bytes would misplace the data. Offsets must come from the
  alignment rule.
- Vertex layout, 36 bytes, all little-endian:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | f32 | px |
| 4 | f32 | pz |
| 8 | f32 | py |
| 12 | f32 | nx |
| 16 | f32 | nz |
| 20 | f32 | ny |
| 24 | u8[4] | color, documented as RGBA |
| 28 | f32 | u |
| 32 | f32 | v |

Face buffer 0x00134B03:

- Each triangle is three u16 LE indices, 6 bytes.
- Observed prefixes: 0, 4, 8 or 12 bytes.
- The 2-byte tail after odd face counts is 0x00 0x00 (126 sample meshes).
- All indices in the sample are below the owning mesh vertex count.

Texture coordinates (raw source values, 98215 vertices):

- u ranges from -1.9511719 to 209.1836, v from -39.68164 to 3.5449219.
- 597 vertices have u or v outside [0, 1]; the rest are inside.

Totals in the sample: 400 meshes, 98215 vertices, 78742 triangles. The first
mesh is `240SX_STYLE15_SPOILER_A`: 72-byte geometry metadata (8-byte prefix),
368 faces, 420 vertices, 15176-byte vertex payload with a 56-byte prefix,
2208-byte face payload with no prefix.

Orientation in the sample, using the named source fields:

- Bounds: px from -2.09 to 2.48 (length), py from -0.31 to 1.11 (height),
  pz from -0.935 to 0.935 (width, symmetric).
- Meshes named `*_FRONT_*` average px = +2.1, `*_REAR_*` average px = -1.8.
- Meshes named `*_LEFT_*` average pz = +0.80, `*_RIGHT_*` average pz = -0.80.
- Taking the cross product of each triangle's edges in source index order
  (a, b, c), in raw file slot order (px, pz, py), and dotting it with the sum
  of its three vertex normals (same slot order): 78569 triangles are
  positive, 43 negative and 130 have zero area.

### Inferred

- The raw file slot order (px, pz, py) is a right-handed, Z-up frame:
  +X forward, +Y left, +Z up. Named as the problem statement does
  (px, py, pz with py up), the frame is left-handed with +pz to the left.
- Source front faces wind counter-clockwise around their normals in raw slot
  space (clockwise when viewed in the left-handed px, py, pz frame, which
  matches the Direct3D convention).
- The alignment is relative to the start of the file, which suggests the
  game loads the file to a suitably aligned address and uses the buffers in
  place (for example as vertex and index buffers).

### Unknown

- All other fields of 0x00134011 (147 bytes before the name besides +25) and
  of the 64-byte 0x00134900 record apart from the three counts.
- 0x00134B02 bytes +32..+59 apart from the first index. The +32 slot looks
  like an index into 0x00134013, but that is not verified.
- 0x00134013, 0x00134017, 0x00134018, 0x00134019, 0x0013401A.
- Unverified leads from read-only research, not used by the parser:
  0x00134011 `+16` may be a hash of the mesh name; 0x00134003 and 0x00134004
  may be tables of mesh name hashes and mesh container offsets.
- The header container 0x80134001 and its children, and the second
  top-level container 0x80034020.
- The color byte order (RGBA vs BGRA) and meaning. Colors are decoded but
  not exported.
- The UV origin (top-left or bottom-left) and what the out-of-range UVs
  mean (tiling, unused channels or other data).
- Whether other cars or other games in the series use multiple geometry
  containers per mesh, other strides, or other padding rules.
- The meaning of the 43 triangles whose winding disagrees with their
  normals (possibly thin or double-sided parts).

## Parser rules (`nfsu::parse_geometry`)

- The whole file is read into memory (limit 256 MiB) and never written.
- Top level: every chunk must fit in the file. Each 0x80134000 root is
  processed; a missing root is an error. If a chunk sequence is malformed,
  chunks read before the error are still processed and the error is
  reported.
- Mesh: each 0x80134010 is parsed independently. An error inside a mesh marks
  only that mesh invalid, with a message naming the mesh index, name, chunk
  ID and file offset; parsing continues with the next mesh because the
  parent bounds are still intact.
- A mesh needs exactly one 0x00134011, 0x00134012, 0x80134100, 0x00134900,
  0x00134B01, 0x00134B02 and 0x00134B03. All other chunks are skipped.
- 0x00134012 must hold exactly `(0x00134011 +25) * 8` bytes, and every
  second word must be 0.
- 0x00134B02 follows the alignment table. Its record count (1..4096) comes
  from 0x00134900 +16. The index runs must be contiguous from 0, non-empty,
  multiples of 3 and cover all faces exactly, and every texture slot must be
  inside the 0x00134012 list.
- Name: payload of at least 28 bytes, NUL within the final 28 bytes,
  printable ASCII only.
- Record positions come only from the alignment table above, computed from
  absolute file offsets with overflow-checked arithmetic. The parser never
  scans for 0x11 bytes and never infers a prefix from spare payload bytes.
- Each payload size must equal the expected size exactly, every prefix byte
  must be 0x11, and every face tail byte must be 0x00. A mismatch names the
  chunk, the expected breakdown (prefix, aligned file offset, data bytes,
  tail) and the first bad byte with its file offset.
- Counts must be in 1..65536 (vertices, the u16 index range) and
  1..1048576 (faces). These checks run before any allocation.
- Every float must be finite and every index must be below the mesh vertex
  count.

## Diagnostic OBJ export (`nfsu_probe obj`)

The export is a right-handed, Y-up OBJ intended for inspection only:

- Position: `x = px`, `y = py`, `z = -pz`. Normals use the same mapping.
- Winding: source index order (a, b, c) is kept. Negating z is a reflection,
  which by itself turns the source front faces counter-clockwise around
  their normals, as OBJ expects. Reversing the indices as well would turn
  them clockwise.
- Result for the sample: +X is the front of the car, +Y is up and +Z is
  the car's right side; `*_LEFT_*` parts are at negative z. The alternative
  mapping `z = pz` with reversed winding also keeps the faces
  counter-clockwise, but it mirrors the car (left parts end up on the right
  side), so it is not used.
- UV: `vt u v` with the raw source values. No origin flip is applied
  because the UV origin has not been confirmed against textures.
- Each valid mesh becomes `o` and `g` records named `m<index>_<name>`, with
  the index zero-padded to four digits and characters outside
  `[A-Za-z0-9_.-]` replaced by `_`. The index keeps names unique.
- Every vertex writes one `v`, one `vn` and one `vt` line, so face records
  are `f i/i/i` with `i = previously exported vertices + source index + 1`.
  Invalid meshes are skipped and do not advance the offset.
- Floats use the shortest round-trip form from `std::to_chars`, and lines end
  with `\n` on every platform, so output is byte-identical between runs.
- Before writing, the exporter checks every index against the owning mesh
  vertex count again and writes nothing if one fails.

## TEXTURES.BIN (car texture pack)

### Observed

Chunk tree of the sample (header offset, payload size):

```text
0      0xB3300000  pack root, 693228
8        0x00000000  padding, 0 bytes
16       0xB3310000  info container, 22060
24         0x33310001  pack header, 124
156        0x33310002  hash table, 952
1116       0x33310003  blob table, 2380
3504       0x33310004  texture info table, 14756
18268      0x33310005  format table, 3808
22084    0x00000000  padding, 52 zero bytes
22144    0xB3320000  data container, 671084
22152      0x33320001  data header, 24
22184      0x00000000  padding, 80 zero bytes
22272      0x33320002  compressed texture data, 670956
693236 0x00000000  padding, 4096 zero bytes
```

String hash used below, `bin_hash`: `h = 0xFFFFFFFF`, then for every byte
`h = h * 33 + byte` modulo 2^32. `bin_hash("None") = 0x001A93CF`.

Pack header 0x33310001 (124 bytes):

| Offset | Type | Value in the sample |
| --- | --- | --- |
| 0 | u32 | 4 (meaning unknown, reported as version) |
| 4 | char[28] | `CARTEXTURES`, NUL padded |
| 32 | char[64] | `Global\Art\Cars\240SX\240SX_Textures.tpk`, NUL padded |
| 96 | u32 | 0xA5034852 = `bin_hash` of the +32 string |
| 100 | u8[24] | zero |

Data header 0x33320001 (24 bytes): u32 values 0, 0, 1, 0xA5034852 (the pack
hash), 0, 0.

Texture count: 119. 0x33310002, 0x33310003, 0x33310004 and 0x33310005 all
hold 119 records, and record `i` of each table describes the same texture:

- 0x33310002, 0x33310003 and 0x33310004 carry the same hash at index `i`.
- 0x33310005 has no hash. Its format agrees with 0x33310004 at the same
  index for all 119.

Hashes are unique and strictly ascending in table order.

Hash table 0x33310002: 8-byte records, (u32 texture hash, u32 0).

Blob table 0x33310003 (20-byte records):

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | texture hash |
| 4 | u32 | JDLZ blob offset, absolute in the file |
| 8 | u32 | JDLZ blob size including its 16-byte header |
| 12 | u32 | 0x100 in all 119, unknown |
| 16 | u32 | 0 in all 119, unknown |

Texture info table 0x33310004 (124-byte records):

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8[12] | zero |
| 12 | char[24] | name, NUL-terminated |
| 36 | u32 | texture hash |
| 40 | u32 | 0x001A93CF (`bin_hash("None")`) x118, 0x23BEB8AA x1 (`240SX_TIRE`), unknown |
| 44 | u32 | zero |
| 48 | u32 | offset in a decompressed address space |
| 52 | u32 | `align_up(+48 + +56, 128)` in all 119 |
| 56 | u32 | decompressed size of all mip levels |
| 60 | u32 | zero |
| 64 | u32 | size of the top mip level |
| 68 | u16 | width |
| 70 | u16 | height |
| 72 | u8 | log2(width) |
| 73 | u8 | log2(height) |
| 74 | u8 | format code: 0x20, 0x22 or 0x24 |
| 75 | u8[3] | zero |
| 78 | u8 | mip level count |
| 79 | u8[2] | zero |
| 81 | u8 | 0 x75, 5 x44, unknown |
| 82 | u8[2] | zero |
| 84 | u32 | 0x01000000 x66, 0x00010200 x18, 0x00010201 x16, 0x00020201 x10, 0x01000001 x7, 0x01000100 x1, 0x01020001 x1, unknown |
| 88 | u32 | 0x100 |
| 92 | u32 | zero |
| 96 | u32 | 0x01000000 |
| 100 | u32 | 0x100 |
| 104 | u8[20] | zero |

- The first +48 value is 0. Each later one equals the previous record's +52,
  so the textures sit back to back at 128-byte alignment in the
  decompressed address space. File data is never located through +48.
- Name lengths: 108 names use all 23 characters of the field, and 11 are
  shorter. `bin_hash(name) == hash` holds for 13 textures: all 11 short
  names, plus `240SX_DOORLINE_WIDEBODY` and `240SX_DOORLINE_KIT_MASK`,
  which are exactly 23 characters long.
- There are only 27 distinct names among the 119 records (for example 11
  records are named `240SX_STYLE01_BRAKELIGH`), so names are not keys.

Format table 0x33310005 (32-byte records):

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32[2] | zero |
| 8 | u32 | 0 x74, 1 x45, unknown |
| 12 | u32 | 5 |
| 16 | u32 | 6 x108, 2 x11, unknown |
| 20 | u32 | D3DFORMAT: FourCC `DXT1` x99, `DXT3` x14, 21 x6 |
| 24 | u32[2] | zero |

The info table format code pairs with this field in every record:
0x22 = `DXT1`, 0x24 = `DXT3`, 0x20 = 21.

Compressed data 0x33320002:

- The payload starts at 22280. Its 120 bytes up to the first 128-byte
  aligned absolute offset (22400) are 0x11, the same padding convention as
  GEOMETRY.BIN.
- 119 JDLZ blobs follow back to back: the first at 22400, 118 gaps of 0
  bytes, the last ending at 693233. The 3 bytes from there to the payload
  end are zero.
- JDLZ header (16 bytes): `JDLZ`, bytes 02 10 00 00, u32 decompressed size,
  u32 compressed size. The decompressed size equals info +56, and the
  compressed size equals blob table +8, in all 119.
- Decoding (implemented in `nfsu::jdlz_decompress`): two flag registers
  are refilled from the input with `byte | 0x100` whenever they reach 1,
  flags1 first. A clear flags1 bit copies one literal byte. A set bit reads
  two bytes b0 b1:
  - If the next flags2 bit is set: `length = (b1 | (b0 & 0xF0) << 4) + 3`,
    `distance = (b0 & 0x0F) + 1`.
  - Otherwise: `distance = (b1 | (b0 & 0xE0) << 3) + 17`,
    `length = (b0 & 0x1F) + 3`.

  flags2 shifts only after a match, and flags1 shifts after every token.
- All 119 blobs fill their stated size exactly, with no back-reference
  before the output start and no overrun. Each blob leaves 1 (77 blobs) or
  2 (42 blobs) input bytes unused.
- Decompressed sizes match the formats exactly, for +56 (all levels) and
  +64 (top level) in all 119: DXT1 is 8 bytes per 4x4 block, DXT3 is 16,
  and A8R8G8B8 is 4 bytes per pixel. Each level halves the previous
  dimensions. Chains may stop early: 512x512 with 7 levels ends at 8x8.

Inventory (`nfsu_probe textures`), 119 textures, all valid:

| Count | Format | Size | Levels |
| --- | --- | --- | --- |
| 35 | DXT1 | 128x64 | 1 |
| 26 | DXT1 | 128x128 | 1 |
| 22 | DXT1 | 64x32 | 1 |
| 8 | DXT1 | 32x32 | 1 |
| 4 | DXT1 | 64x64 | 1 |
| 1 | DXT1 | 16x16 | 1 |
| 1 | DXT1 | 256x256 | 1 |
| 2 | DXT1 | 256x256 | 6 |
| 6 | DXT3 | 64x64 | 1 |
| 3 | DXT3 | 128x64 | 1 |
| 3 | DXT3 | 64x32 | 1 |
| 2 | DXT3 | 32x32 | 1 |
| 6 | A8R8G8B8 | 512x512 | 7 |

Decoder check: in the two A8R8G8B8 textures 22 and 65, level 1 matches a
2x2 box filter of level 0. Over 29584 sampled channel values the mean
absolute difference is 0.015. Comparing against deliberately misaligned
level-0 pixels gives 14.5. A wrong JDLZ decoder, a wrong level size or a
wrong level order would not produce this match.

### Inferred

- The JDLZ decoder is correct. This rests on the exact sizes, clean stream
  consumption and the mip check above.
- Format code 0x20 with D3DFORMAT 21 is D3DFMT_A8R8G8B8, stored as
  little-endian u32 ARGB (bytes B, G, R, A). This comes from the public
  D3DFORMAT numbering, not from the pixels.
- Blob table +4 is relative to the file start. In this sample the pack root
  starts at offset 0, so a root-relative base cannot be ruled out.
- Hashes of the other 106 names probably cover the full, longer original
  names that the 23-character field truncates.

### Unknown

- The 1 to 2 unused bytes at the end of every JDLZ blob.
- The fields marked unknown above, including the second hash at info +40.
- Whether other packs use other format codes (for example DXT5 or
  paletted formats), other JDLZ header bytes or a pack root not at offset 0.
  None of these is supported; unsupported formats are reported, never
  guessed.
- The DXT decoding rules below are the common reference rules. The game's
  hardware may round interpolated colors differently.

## Texture pack parser rules (`nfsu::parse_texture_pack`)

- The whole file is read into memory (limit 256 MiB) and never written.
  Limits: 4096 chunks, 4096 textures, dimensions 1..4096, 64 MiB
  decompressed per texture. All arithmetic is 64-bit and overflow-checked.
- The file must hold exactly one 0xB3300000 root. It must contain exactly
  one 0xB3310000 and one 0xB3320000, and inside them exactly one of each of
  0x33310001..0x33310005 and 0x33320001..0x33320002. Every chunk must fit
  its parent exactly.
- The table sizes must be exactly `count * record size`, with count =
  0x33310002 size / 8. The hash of record `i` must agree across the three
  hashed tables.
- A texture is valid only if every check passes:
  - The name is NUL-terminated printable ASCII.
  - The format pair is one of the three observed pairs.
  - The width and height equal `1 << log2` and are within the limits.
  - The mip count is `1..max(log2) + 1`.
  - +56 and +64 equal the computed sizes.
  - The blob lies inside 0x33320002, after the 0x11 prefix.
  - The blob starts with `JDLZ 02 10 00 00`, and its header sizes equal +56
    and the blob table size.

  Otherwise the texture is kept with `valid = false` and a message naming
  the texture, the record offset and the field offset.
- Overlapping blobs invalidate both textures. Duplicate hashes are reported
  as issues, since hash lookups become ambiguous, but the textures stay
  exportable.
- Decompression happens only on export. The output must be filled exactly,
  and every match must stay inside the output.
- Raw info, format and blob records are kept in `TextureRecord`, so fields
  that are still unknown are preserved.

## Diagnostic texture export (`nfsu_probe export-textures`)

- `tga` (default) writes the top level only, as an uncompressed 32-bit TGA
  (type 2, top-left origin, 8 alpha bits). Pixels are decoded as follows:
  - DXT1/DXT3 5:6:5 endpoints are expanded by bit replication. Interpolated
    colors use integer `(2a + b) / 3`, `(a + 2b) / 3` and `(a + b) / 2`.
  - DXT1 uses 3-color mode with transparent black when color0 <= color1.
    DXT3 always uses 4-color mode, and its 4-bit alpha is scaled by 17.
  - Blocks at the image edge are cropped.
  - A8R8G8B8 pixels are reordered, not changed.
- `dds` writes a DDS header followed by the decompressed bytes of every
  level, unchanged. Use it to compare against other decoders.
- File names are `t<index:4>_<HASH:8>_<name>.<ext>`, with characters
  outside `[A-Za-z0-9_.-]` replaced by `_`. The index and hash keep names
  unique for duplicate and truncated names.
- All target names are checked before anything is written. If any target
  exists or is the input file, nothing is written. Each file is written as
  `<name>.partial` and renamed when complete. After any failure, all files
  written by the run are removed.
- Sample results (119 files each, byte-identical across runs): the SHA-256
  of the sorted `name hash` manifest is
  `40DF8D3293818129603216E618C84B184E6502450803A16052A88175B726D593`
  (TGA) and
  `707F405F094780DA429A61680648B498E3A82A9FBB5E11A1EC9AED3A7087E614`
  (DDS). Do not commit exported images; they are derived from copyrighted
  game data.

## Mesh to texture binding (`nfsu_probe texture-bindings`)

### Observed

A triangle's texture is resolved by this chain:

1. Find the 0x00134B02 material record whose run of indices
   `[first index, first index + index count)` contains the triangle's
   indices.
2. Read its texture slot (+28).
3. Take the 0x00134012 entry at that slot.
4. Match the entry's hash to a texture pack hash.

In the sample, all 1242 material records resolve to a 0x00134012 entry.
174 records resolve to a texture in the 240SX pack. The other 1068
reference 25 hashes that are not in this pack. The most used are
0x77A20245 (637 records), 0xBD1F0E0A (156) and 0x721AFF7C (87).

Worked example, reproducible with `texture-bindings` and a hex viewer:

| Step | Offset | Value |
| --- | --- | --- |
| Mesh container 0x80134010 of mesh 118 `240SX_KIT00_ENGINE_B` | 1628544 | |
| 0x00134011 +25 texture count (payload 1628560) | 1628585 | 1 |
| 0x00134012 header | 1628736 | size 8 |
| 0x00134012 entry 0 | 1628744 | 0x4B6F3195, 0 |
| 0x00134900 record: +16, +36, +52 | 1628784 | 1 material, 199 faces, 294 vertices |
| 0x00134B02 header | 1639512 | size 60, no prefix |
| Material 0: +12 index count | 1639532 | 597 (= 199 * 3) |
| Material 0: +28 texture slot | 1639548 | 0 |
| Material 0: +52 first index | 1639572 | 0 |
| TEXTURES.BIN 0x33310002 entry 31 | 412 | 0x4B6F3195 |
| TEXTURES.BIN 0x33310003 entry 31 | 1744 | 0x4B6F3195, blob 160486, 31402 bytes |
| TEXTURES.BIN 0x33310004 record 31 | 7356 | `240SX_ENGINE`, hash at 7392 = 0x4B6F3195 |

The whole mesh (all 199 triangles) uses texture 31, `240SX_ENGINE`, DXT1
256x256, 1 level. `bin_hash("240SX_ENGINE") = 0x4B6F3195`, which confirms
the name independently of both files. A second single-texture example is
mesh 183 `240SX_KIT00_BRAKELIGHT_C`: its entry at 2372808 (0x3090660D)
matches texture 18 at 5744.

### Inferred

- The 25 unresolved hashes are textures from other packs or textures
  supplied at runtime. Read-only research found some of them in
  `GLOBAL/GLOBALB.BUN`, and others match hashes of names such as
  `240SX_SKIN1` (0x77A20245), `CARBOTTOM` (0xBD1F0E0A) and `CARBONFIBRE`
  (0x721AFF7C). This was not verified by this tool and is out of scope for
  this pack.

### Unknown

- How the unresolved hashes are resolved at runtime, and what 0x00134013
  and the material flags select (shader, blending or damage state).
- The UV origin. Textures were not overlaid on the meshes to confirm it.