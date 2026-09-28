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
    0x00134013  material-key list, 8 to 56 bytes (multiples of 8)
    0x80134100  geometry container, exactly one per mesh
      0x00134900  geometry metadata
      0x00134B01  vertex buffer
      0x00134B02  material records
      0x00134B03  face (index) buffer
    0x00134017, 0x00134018, 0x00134019  unknown, in 115 meshes
    0x0013401A  attachment markers, in 60 meshes
0x80034020  second top-level container (0x00039200, 0x00039201, 0x00039202)
```

Every one of the 400 mesh containers holds exactly one of each of
0x00134011, 0x00134012, 0x00134013, 0x80134100, 0x00134900, 0x00134B01,
0x00134B02 and 0x00134B03.

The two header tables are the runtime mesh index. `speed.exe` keeps the
0x00134003 payload at +0x78 of its solid-list object (0x53ff5c) and
binary-searches it by hash (0x540630, 8-byte entries, key first), so the
hashes must be ascending: they are, strictly, in all 39 retail
`GEOMETRY.BIN` files. The second word of each entry is 0 in the file and
holds the loaded mesh pointer at runtime; it is written when the container
is loaded (0x540201) and cleared when it is unloaded (0x5403d1). The
0x00134004 ranges are what the loader streams. See "Car part database"
below for the records that name these hashes.

Mesh metadata 0x00134011:

- The mesh name is the final 28 bytes of the payload, NUL-terminated ASCII.
  Names are truncated to 27 characters (for example
  `240SX_DECAL_LEFT_QUARTER_RE`).
- `+25` (u8) is the number of 0x00134012 entries in every sample mesh.
- `+26` (u8) is the number of 0x00134013 entries in every sample mesh.

Texture hash list 0x00134012:

- `payload_size / 8` entries of (u32 LE texture hash, u32 LE 0). The second
  word is 0 in every entry of the sample.

Material-key list 0x00134013:

- `payload_size / 8` entries of (u32 LE key hash, u32 LE resolved pointer).
  The on-disk second word is 0 in all 1078 sample entries. The game resolves
  each hash against its material/effect object registry and writes the object
  pointer into the second word at runtime.
- The 400 sample lists contain 1 to 7 entries. All 36 distinct hashes are
  referenced by at least one material. Exact `bin_hash` matches to tokens in
  `speed.exe` include `DECAL`, `BRAKELIGHT`, `INTERIOR`, `WINDSHIELD`,
  `BOTTOM`, `ENGINE` and `MIRROR`.

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
| 32 | u8 + u8[3] | material-key slot into 0x00134013, or 0xFF for none; high bytes are zero in the sample |
| 36 | u8[16] | unknown, kept raw |
| 52 | u32 | first index into the 0x00134B03 index stream |
| 56 | u32 | flags, unknown, kept raw |

In every sample mesh the records are in order, each first index equals the
sum of the previous index counts, the counts add up to exactly
`3 * face_count`, and every texture slot is below the 0x00134012 entry count.
Of 1242 material-key slots, 1241 are valid for their mesh's 0x00134013 list
and one is the 0xFF sentinel. There are no invalid slots.

The retail executable confirms that these are separate bindings. It loads
0x00134012 into the mesh table at +0x2C and 0x00134013 into the table at
+0x3C. The draw path reads material byte +28 to select the former and byte
+32 to select the latter; +32 == 0xFF leaves the material/effect object null.
This was checked against `speed.exe` SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.

Eight flag values occur in the 1242 sample records: 0x00000000 (2),
0x00004080 (4), 0x00004180 (922), 0x0000C180 (6), 0x00014080 (19),
0x00014180 (263), 0x00018000 (3), and 0x0001C180 (23). The executable tests
bits 0x80 and 0x100 in the material/effect path. It also uses bit 0 to bypass
a bounds-related test and, when matrix data exists, to upload constants from
the 16 bytes at material +33..+48. No sample 240SX record has bit 0, so the
skinning interpretation and the remaining flag semantics are not yet proven.

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

- All other fields of 0x00134011 (146 bytes before the name besides +25/+26) and
  of the 64-byte 0x00134900 record apart from the three counts.
- 0x00134B02 bytes +33..+51 and the exact meanings of the flags at +56.
- 0x00134017, 0x00134018 and 0x00134019. 0x0013401A is decoded; see
  "Attachment markers" below.
- 0x00134011 `+16` may be a hash of the mesh name.
- The meanings of 0x00134002 and 0x80134008 in the header container, and
  the second top-level container 0x80034020.
- The color byte order (RGBA vs BGRA) and meaning. Colors are decoded but
  not exported. In the sample most are grey (R = G = B); see
  `docs/RENDERER.md`.
- What the out-of-range UVs mean (tiling, unused channels or other data).
  The reference renderer wraps them. The UV origin is resolved in "Mesh to
  texture binding" below.
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
- In each geometry root, if a 0x80134001 header is present, it must contain exactly one
  0x00134003 table of 8-byte `(hash, 0)` records and one 0x00134004 table
  of 20-byte `(hash, absolute container offset, total container size, 0, 0)`
  records. Counts and hashes must agree, every range must exactly name one
  direct 0x80134010 child of that same root, and every mesh in the root must
  be named once. Directory provenance is attached only after that complete
  pair passes all checks. Hash selection is enabled only when every
  encountered directory validates and every parsed mesh in the file has
  verified provenance.
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
| 84 | u8 | 0 or 1 in the sample, unknown |
| 85 | u8 | alpha-test mode: only 1 enables `GREATER 0xAA`; 0 or 2 disables it |
| 86 | u8 | blend mode: 0 disabled, 1 source-over, 2 additive |
| 87 | u8 | 0 or 1 in the sample, unknown |
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

### Standalone vinyl pack

Reference sample:

- File: `Cars/240SX/VINYLS.BIN`
- Size: 15409440 bytes
- SHA-256: `46099AF77F100C3E09EBDBF4813631FFE3188CDE411F79D570C01B076E4EA1E3`

Physical chunk offsets are:

| Chunk | Header | Payload | Payload bytes |
| --- | ---: | ---: | ---: |
| `0x33310001` | 24 | 32 | 124 |
| `0x33310002` | 156 | 164 | 6976 |
| `0x33310003` | 7140 | 7148 | 17440 |
| `0x33310004` | 24588 | 24596 | 108128 |
| `0x33310005` | 132724 | 132732 | 27904 |
| `0x33320002` | 160896 | 160904 | 15244432 |

The data payload has 120 prefix bytes; the first JDLZ blob starts at 161024.
Representative records at both ends of each code range are:

| Index | Code | Stored name | Info offset | Blob offset + size |
| ---: | ---: | --- | ---: | ---: |
| 0 | 0x08 | `240SX_TRIBAL5_MASK` | 24596 | 161024 + 21351 |
| 871 | 0x08 | `240SX_FLAGS314_MASK` | 132600 | 15383463 + 21872 |
| 2 | 0x80 | `240SX_FLAME10` | 24844 | 200609 + 11025 |
| 867 | 0x80 | `240SX_STRIPE9` | 132104 | 15318967 + 8219 |
| 197 | 0x81 | `240SX_CUSTOM1` | 49024 | 3107930 + 49054 |
| 262 | 0x81 | `240SX_CUSTOM37` | 57084 | 5141640 + 18444 |

The pack contains 872 valid 512x512 P8 textures with 7 levels. Every info
record has +56 = 349504 and +60 = 1024. The P8 format pairs are code 0x08,
0x80 or 0x81 at info +74 with D3DFORMAT 41 at format record +20:

| Code | Count | Observed role | Used palette indices |
| --- | ---: | --- | --- |
| 0x08 | 436 | `<base>_MASK` | 0..255 |
| 0x80 | 402 | ordinary vinyl base | 0..15 |
| 0x81 | 34 | `CUSTOM*` vinyl base | 0..63 |

There are 436 base records and 436 mask records. For every base hash there
is exactly one `bin_hash(base_name + "_MASK")`; there are no missing or
duplicate pairs. Eleven stored mask names are truncated by the 24-byte name
field, but their full hashes still prove the pairing.

The uncompressed JDLZ target is 350592 bytes for every record:

```text
[0, 349504)       P8 index mip chain, 512x512 through 8x8
[349504, 349568)  zero alignment padding
[349568, 350592)  1024-byte palette, 256 four-byte entries
```

Thus the target is `align_up(info +56, 128) + info +60`, not merely info
+56. Info +48 is `index * 350592`; +52 is +48 + 349568 and points at the
palette inside the decompressed target. The next record's +48 equals the
previous +52 + +60. All 872 padding ranges are zero. `texture_payload`
verifies this layout, returns only the 349504 index bytes and can copy the
palette separately. A retail pass extracted all 872 records without error.
The channel meaning of the four palette bytes and the runtime blend formula
remain outside this structural claim.

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
- Whether other packs use other format codes (for example DXT5), other JDLZ
  header bytes or a pack root not at offset 0. None of these is supported;
  unsupported formats are reported, never guessed.
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
  - The format pair is DXT1, DXT3, A8R8G8B8, or one of the observed P8
    codes 0x08, 0x80 and 0x81 with D3DFORMAT 41.
  - The width and height equal `1 << log2` and are within the limits.
  - The mip count is `1..max(log2) + 1`.
  - +56 and +64 equal the computed sizes.
  - The blob lies inside 0x33320002, after the 0x11 prefix.
  - The blob starts with `JDLZ 02 10 00 00`. Its compressed header size
    equals the blob table size. The decompressed size equals +56, except
    that P8 requires `align_up(+56, 128) + 1024` for padding and palette.

  Otherwise the texture is kept with `valid = false` and a message naming
  the texture, the record offset and the field offset.
- Blob overlap is checked for every pair of otherwise valid textures, not
  only for neighbors in offset order, so a blob that contains several others
  is compared with each of them. Blobs `[offset, offset + size)` overlap when
  they share a byte; touching blobs do not. Every texture in any overlap is
  invalidated with one issue listing the number of partners and the 8
  smallest partner indices. Duplicate hashes are reported
  as issues, since hash lookups become ambiguous, but the textures stay
  exportable.
- Decompression happens only on export. The output must be filled exactly,
  and every match must stay inside the output.
- Raw info, format and blob records are kept in `TextureRecord`, so fields
  that are still unknown are preserved.
- Info bytes +85 and +86 are also exposed as `alpha_test_mode` and
  `alpha_blend_mode`. Their executable trace, D3D states and retail counts
  are in `docs/MATERIALS.md`.
- Every hash field (info +36, hash table +0, blob table +0) is read before
  the first check, so a record rejected for its name still reports the
  hashes it claims (`record_claims_hash`). Viewer precedence and material
  lookup rely on this.
- Every parser entry point records the address of the input buffer in
  `TexturePack::parse_source`. It is only an identity for pairing a pack
  with its bytes, valid while the caller keeps that buffer alive and
  unchanged in size and address; it is never dereferenced.

## Embedded texture packs (GLOBAL/GLOBALB.BUN)

Reference sample:

- File: `GLOBAL/GLOBALB.BUN`
- Size: 2361176 bytes
- SHA-256: `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`

### Observed

The generic chunk walk of the file is clean, and the maximum depth is 3.
Two 0xB3300000 roots sit at the top level, between unrelated chunks
(0x00135200 records before them; 0x80134000, 0x00034600 and others after
them) and 0x00000000 padding:

```text
23424   0xB3300000  pack 0, 893624
24        0xB3310000  info container 12456: 0x33310001, 0x33310002,
                      0x33310004, 0x33310005 (no 0x33310003)
          0xB3320000  data container 881080: 0x33320001, padding 80,
                      0x33320002 payload 36104, 880952 bytes
921216  0xB3300000  pack 1, 751800
          0xB3310000  info container 1468 (no 0x33310003)
          0xB3320000  data container 750264: 0x33320002 payload 922888,
                      750136 bytes
```

The byte sequence of the 0x33310003 id occurs nowhere in the file.

| | Pack 0 | Pack 1 |
| --- | --- | --- |
| Header version, name | 4, `GLOBAL` | 4, `GLOBAL` |
| Path | `Global\GlobalTextures.tpk` | `Global\GlobalTexturesPC.tpk` |
| Hash = `bin_hash(path)` = 0x33320001 +12 | 0x383E2E5E | 0x8C37F6B1 |
| Records | 75 | 8 |
| 0x11 prefix to 128-byte alignment | 120 (data start 36224) | 120 (data start 923008) |
| max(+48 + +56) | 880832 = payload - prefix | 750016 = payload - prefix |

- Formats (info +74 / format record +20): pack 0 has DXT1 x35, DXT3 x19,
  A8R8G8B8 x5 and 16 records with code 0x08 and D3DFORMAT 0x29 (P8,
  1 byte per pixel). Pack 1 has DXT3 x6, A8R8G8B8 x1 and DXT1 x1.
- +48 is sorted in table order, is a multiple of 128, and each record
  starts at `align_up(previous +48 + +56, 128)`. The gaps are zero bytes.
  Pack 0 starts at 16384, pack 1 at 0.
- +52 is not an end offset here. In pack 0, P8 records have +52 = 0, 1024,
  ..., 15360 and +60 = 1024. Every other record has +60 = 0 and +52 equal
  to the running sum of the palette sizes before it. In pack 1 +52 is 0.
- The first 16384 bytes after the pack 0 prefix are referenced only by P8
  records, as 1024-byte blocks at `data_start + +52`.
- No hash repeats inside a pack, across the two packs, or between them and
  the 240SX `TEXTURES.BIN`.
- The eight shared 240SX textures are all in pack 0, none of them P8:
  `INTERCOOLER` 0x427B4565, `DRIVER` 0x5799E60B, `DUMMY_TIRE_4LOD`
  0x5C920FB0, `GRILL_01` 0x69C032B9, `GRILL_02` 0x69C032BA, `CARBONFIBRE`
  0x721AFF7C, `CARBOTTOM` 0xBD1F0E0A and `REAR_DEFROSTER` 0xD195BE56.

### Verified

- Each record's chain is stored uncompressed at `data_start + +48`, levels
  largest first. Check: for 63 valid records with at least 2 levels and
  8 pixels per side, level 1 was decoded with `nfsu::decode_level` and
  compared with a 2x2 box filter of level 0. Mean absolute errors per
  channel are 0.00 to 11.21 (22.46 for the checkerboard `DEFAULTTEXTURE`).
  The eight 240SX textures give 0.67 to 8.20, and the same check with
  the base moved by 128 bytes either way gives 5.92 to 87.49. Two
  low-contrast 32 and 64 pixel textures come out slightly lower when
  shifted (7.03 against 7.54, 4.55 against 4.77); the rest are lower or
  equal unshifted.
- The earlier reading of pack 0 as a "16384-byte virtual base" before the
  chain is wrong: offsets are not rebased, the region is the P8 palettes.
- The P8 palettes are 1024-byte blocks of 256 four-byte entries. The loader
  points each texture record at its pack palette at 0x00545A59..0x00545A61.
  At 0x00407E70..0x00407E82 the game copies each palette dword into a pixel;
  0x00407EA8 selects A8R8G8B8 for the created texture. The stored bytes are
  B, G, R, A, and the track decoder reorders them to RGBA.

### Inferred

- A pack without 0x33310003 stores its chains raw. Header version 4 is the
  same in both kinds of file, so there might be another flag.
- The 128-byte alignment could be absolute or relative to the root. Both
  roots are 128-aligned, so the data cannot tell them apart.
- The parser returns P8 palette bytes separately from the index chain.

### Unknown

- Whether other BUN files nest roots deeper. The parser accepts any depth
  up to its limit.
- Info +77 and format record +8, both 1 only for P8 records.

## Texture pack set rules (`nfsu::parse_texture_pack_set`)

- The whole file is read into memory once (limit 256 MiB). Packs are parsed
  in place and no bytes are copied.
- Roots are found by an iterative depth-first walk with an explicit stack.
  It descends into every chunk with bit 31 set, up to 32 levels, but never
  into a 0xB3300000 root. Limits: 1048576 chunk headers for the search,
  256 roots, 16384 records over all packs. Inside a pack the per-pack limits
  above still apply.
- A child that does not fit its parent is reported, the rest of that parent
  is skipped, and the walk continues with the parent's own siblings. Any
  walk problem or pack whose tables were not read makes the set
  incomplete (`TexturePackSet::complete == false`).
- Each root is parsed by `parse_texture_pack_at`. With a 0x33310003 table
  it follows the rules above. Without one it is a raw pack:
  - The hash table entry must equal the info hash, with a zero second
    word.
  - Format, dimension, log2, mip count, +56 and +64 checks are the same.
  - +48 must be a multiple of 128, and `[data_start + +48, ... + +56)` must
    lie inside the 0x33320002 payload. The arithmetic is 64-bit.
  - For P8, +52 addresses a 1024-byte palette inside the data range. Raw
    palettes and index chains must not overlap each other, including across
    records. +52 is not checked for other formats.
  - Ranges of valid records must not share a byte. Every participant is
    invalidated, as for JDLZ blobs.
  - Unsupported formats invalidate only their record. Observed P8 records
    are accepted; `texture_payload` can return their raw palette separately.
  - `raw_extent` = max(+48 + +56) over all records is reported, not
    enforced.
- Duplicate hashes inside a pack are pack issues. A hash that one pack
  repeats from an earlier pack of the same file is a set issue.
- `parse_texture_pack` (standalone `TEXTURES.BIN`) is unchanged. It still
  needs exactly one top-level root and a 0x33310003 table.

## Diagnostic texture export (`nfsu_probe export-textures`)

- `tga` (default) writes the top level only, as an uncompressed 32-bit TGA
  (type 2, top-left origin, 8 alpha bits). Pixels are decoded as follows:
  - DXT1/DXT3 5:6:5 endpoints are expanded by bit replication. Interpolated
    colors use integer `(2a + b) / 3`, `(a + 2b) / 3` and `(a + b) / 2`.
  - DXT1 uses 3-color mode with transparent black when color0 <= color1.
    DXT3 always uses 4-color mode, and its 4-bit alpha is scaled by 17.
  - Blocks at the image edge are cropped.
  - A8R8G8B8 pixels are reordered, not changed.
  - P8 export is refused: the parser exposes its index chain and palette,
    but the diagnostic exporters do not guess palette channels or runtime
    composition.
- `dds` writes a DDS header followed by the decompressed bytes of every
  level, unchanged. Use it to compare against other decoders.
- File names are `t<index:4>_<HASH:8>_<name>.<ext>`, with characters
  outside `[A-Za-z0-9_.-]` replaced by `_`. The index and hash keep names
  unique for duplicate and truncated names.
- All target names are checked before anything is written. If any target
  exists or is the input file, nothing is written. Each file is written as
  `<name>.partial` and renamed when complete. Just before the rename the
  target is checked again, and a target that appeared during the export is
  not replaced. A narrow race between that check and the rename remains.
- After any failure, the export tries to remove every file it created
  (completed targets and its own `.partial` file) and never touches other
  files. It continues after a failed removal. The error message states
  "removed all N" only if every file is gone. Otherwise it says "removed K
  of N" and lists the paths that could not be removed, which are also
  returned in `TextureExportResult::not_removed`.
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
  supplied at runtime. Eight of them are in `GLOBAL/GLOBALB.BUN` pack 0
  (verified, see "Embedded texture packs"), among them `CARBOTTOM`
  (0xBD1F0E0A) and `CARBONFIBRE` (0x721AFF7C). The largest remaining one,
  0x77A20245, matches `bin_hash("240SX_SKIN1")`. It is in neither file.
  Executable evidence identifies it as the formatted static fallback; active
  customization instead composes into the `RideInfo+0x444` override, normally
  `DUMMY_SKIN1`. See `docs/CAR_SCENE.md`.

### Unknown

- How the unresolved texture hashes are resolved at runtime, the exact object
  types selected by 0x00134013, and stable names for the untraced material
  flag bits. Alpha state, effect parameters and the proven 0x80/0x100
  branches are bounded in `docs/MATERIALS.md`.

### UV origin (from `nfsu_viewer`)

- Observed: on the front bumper, `240SX_BADGING` (texture 76, rows decoded
  top to bottom) sampled with raw `(u, v)` shows "NISSAN" upright and
  reading left to right. Sampled with `(u, 1 - v)`, the same area is
  scrambled. The camera and command are in `docs/RENDERER.md`.
- Inferred: v = 0 is the top row of the stored texture, as in Direct3D.
  Raw UVs need no flip.
- Unknown: whether every car and every material follows this. Only this car
  was checked.

## Track geometry (Tracks/STREAM*.BUN)

Track containers reuse the chunk ids above, but a track geometry root is
not the same shape as a car one: it has no `0x00134004` mesh directory,
its `0x00134011` is an aligned 176-byte record, its two hash lists appear
only when their count byte is non-zero, and its vertex buffer holds
24-byte vertices selected by a per-mesh vertex-format word at
`0x00134900 +12`. The byte and executable evidence, the parser rules and
one explicitly bounded decoded retail region are in
[`TRACK.md`](TRACK.md). `nfsu::parse_geometry` is unchanged and still
decodes only the 36-byte car layout.

## Attachment markers (GEOMETRY.BIN chunk 0x0013401A)

Optional, at most one per mesh. In the 37 car files that are
retail-original, 2092 meshes carry one, holding 4216 records in total, and
every chunk holds at least one record. Counting `Cars/LANCER` and
`Cars/PEUGOT`, which carry an `NFS-CarToolkit` header instead of the retail
source path, the totals are 2860 chunks and 4461 records, and the 647 empty
chunks are all theirs. Those are the records in the files; a parser that
skips invalid meshes reports one chunk and two records fewer, because
`Cars/CIVIC` mesh 286 fails a later check. `docs/PART_PLACEMENT.md` has the
detail. The loader is `speed.exe` 0x5400e3, which stores the
aligned table at solid +0x80 and the record count in the byte at solid
+0x1B.

Records start at the first absolute file offset in the payload that is a
multiple of 16, after 0x11 filler, exactly as the other aligned records do.
The count is `(payload_size - filler) / 80`, computed at 0x540104 by a
reciprocal multiply whose divisor is exactly 80. It is stored with
`mov byte [ebx+0x1b], dl` and read back with `movsx`, so only counts up to
127 behave as written; the most any retail chunk holds is 18.

| Offset | Type | Meaning |
| --- | --- | --- |
| +0x00 | u32 | `bin_hash` of the marker name |
| +0x04 | u32[3] | zero in all 4216 retail-original records; uninitialised in the two rewritten files |
| +0x10 | f32[16] | row-major 4x4 matrix; row 3 is the translation |

The translation is in the same raw slot order as a vertex (longitudinal,
lateral, vertical). The exhaust and licence-plate draw paths use the matrix
as `quarter_turn * matrix`; the quarter turn and the full list of 24
recovered marker names are in
[`PART_PLACEMENT.md`](PART_PLACEMENT.md), which also records which parts a
marker places and which are drawn at the car's own matrix instead.

Parser rules (`nfsu::parse_geometry`): at most one chunk per mesh, 0x11
filler up to the 16-byte boundary, the remaining payload a whole number of
80-byte records, at most 127 records because the game truncates the count to
a byte and every reader sign-extends it again, and every matrix element
finite. The three reserved words are kept raw
and never checked. A mesh that fails any of these is skipped with a
diagnostic, like any other geometry failure.

## Car part database (GLOBAL/GLOBALB.BUN)

Reference samples: `GLOBAL/GLOBALB.BUN` as above, and `speed.exe`, 3178496
bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` (PE32,
image base 0x400000, file offset equals RVA). Addresses below are
`speed.exe` virtual addresses; offsets are decimal GLOBALB.BUN offsets.
`nfsu_probe car-parts GLOBALB.BUN [CAR]` lists every record.

### Observed

One top-level 0x80034602 chunk at 1797512 (payload 303992) holds four
children, in this order:

| Chunk | Header offset | Payload size | Content |
| --- | ---: | ---: | --- |
| 0x00034603 | 1797520 | 32 | header |
| 0x00034606 | 1797560 | 21248 | NUL-terminated display names |
| 0x00034605 | 1818816 | 64904 | 8113 x 8-byte attributes (u32 key, u32 value) |
| 0x00034604 | 1883728 | 217776 | 4537 x 48-byte part records |

Header: +0, +4, +12, +20 and +28 are zero; +8 = 5; +16 = 4537 (record
count); +24 = 8113 (attribute count). The loader at 0x57c927 finds the
children by their sizes and never checks their ids. It overwrites +0 and
+4 with list links and +12, +20, +28 with pointers to the record,
attribute and string tables, and appends the pack to the list headed at
0x73ad40.

Part record (0x00034604), little-endian:

| Offset | Type | Meaning | Code evidence |
| --- | --- | --- | --- |
| +0x00 | u32 | display name offset into 0x00034606 | turned into a pointer at 0x57c98a |
| +0x04 | u32 | car type hash, `bin_hash` of the CarTypeInfo +0x20 name | compared at 0x57ce7a; binary-search key (offset 4) at 0x57ce57 |
| +0x08 | u32 | part name hash | compared at 0x57ce98 |
| +0x0C | u32 | not decoded; paint records are grouped by it | switch at 0x57c9ec |
| +0x10 | s8 | slot id (see `docs/CAR_SCENE.md`) | `movsx` compare at 0x57ce88 |
| +0x11 | u8 | upgrade level | `movzx` compare at 0x57cea6 and 0x504670 |
| +0x12 | u8 | not decoded; the wide-body number for slot 6 | `%s_WIDE%d_` argument at 0x57ed37 |
| +0x13 | u8 | not decoded | |
| +0x14 | u32 | first attribute index, pointer at runtime | converted at 0x57c99a when +0x1C != 0 |
| +0x18 | u32 | second attribute index | converted at 0x57c9ad when +0x1E != 0 |
| +0x1C | s16 | first attribute count | 0x57c991, 0x57eaca |
| +0x1E | s16 | second attribute count | 0x57c9a6 |
| +0x20 | 4 x u32 | model hashes for LOD A, B, C, D; 0 = none | read by the render-info constructor at 0x56a95d and the loader at 0x58b08b |

- Records are sorted by +0x04, unsigned ascending, over 43 distinct car
  type hashes. `bin_hash("ANY")` = 0x00009287, `bin_hash("BRAKES")` =
  0x52EB02F7, `bin_hash("PLATES")` = 0x7326CF08 and
  `bin_hash("240SX")` = 0x0150FB80 are among them.
- Every slot id is in 0..119 and every attribute range is inside the
  attribute table.
- Display names are not restricted to printable ASCII: record 4075 (at
  2079336) is `ACE` followed by a TAB.
- 240SX: all 400 0x00134004 directory hashes of `Cars/240SX/GEOMETRY.BIN`
  occur in the +0x20 fields of the 144 records with car type 0x0150FB80.
  417 of their 426 nonzero model hashes equal `bin_hash` of the part name
  (+0x08) extended with `_A` to `_D`. The exceptions: the engine records
  of part names `240SX_STYLE01_ENGINE` to `240SX_STYLE03_ENGINE` carry the
  models of `240SX_KIT00_ENGINE`, `240SX_STYLE01_ENGINE` and
  `240SX_STYLE02_ENGINE`, and the three collision records (slot 119) carry
  their own part name hash as the only model hash.

Worked example, the stock 240SX spoiler (record 1360 at 1949016):

| Field | Bytes | Value |
| --- | --- | --- |
| +0x00 | `3C 14 00 00` | 5180: `STOCK` at 1802748 |
| +0x04 | `80 FB 50 01` | 0x0150FB80 = `bin_hash("240SX")` |
| +0x08 | `C4 0F 5D AA` | 0xAA5D0FC4 = `bin_hash("240SX_KIT00_SPOILER")` |
| +0x10, +0x11 | `0B 00` | slot 11 (SPOILER), level 0 |
| +0x20 | `44 1D E0 B5` | 0xB5E01D44: mesh 256 `240SX_KIT00_SPOILER_A` at 3037696 |
| +0x24 | `45 1D E0 B5` | 0xB5E01D45: mesh 257 `240SX_KIT00_SPOILER_B` |
| +0x28 | `46 1D E0 B5` | 0xB5E01D46: mesh 258 `240SX_KIT00_SPOILER_C` |
| +0x2C | `47 1D E0 B5` | 0xB5E01D47: in no 240SX directory record |

### Lookup (0x57ce10 and 0x53e400)

`find_car_part` reproduces the game's lookup on one pack. Keys are a car
type hash (0 = any), a slot (-1 = any), a part name hash (0 = any), a
previous record and an upgrade level (-1 = any). Without a previous
record and with a car type hash, 0x53e400 binary-searches +0x04 with
unsigned compares and then steps back to the first record of the
equal-key run; the scan then returns the first record in file order that
matches every key, and returns nothing at the first record of another
car type. With a previous record the scan starts right after it. With
car type 0 it starts at the first record. The game keeps a list of packs;
GLOBALB.BUN has one, and the parser accepts exactly one.

### Parser rules (`nfsu::parse_car_part_database`)

- The whole file is read once (limit 256 MiB). Top-level chunks must tile
  the file exactly; exactly one of them is 0x80034602.
- The database must hold exactly 0x00034603, 0x00034606, 0x00034605 and
  0x00034604 in this order, filling its payload.
- Header: payload 32 bytes, version 5, the five runtime words zero.
- Payload sizes must equal count x 8 (attributes) and count x 48
  (records); limits 1048576 attributes and 65536 records.
- Each record: name offset inside the string table and NUL-terminated
  there (content kept byte for byte), slot id 0..119, attribute counts
  not negative, and a nonzero count's range inside the attribute table.
- Records must be sorted by +0x04, unsigned ascending, as the binary
  search requires.
- Any problem makes the database invalid (`CarPartDatabase::valid`), and
  `find_car_part` and `select_stock_parts` then refuse it.

## Car type table (GLOBAL/GLOBALB.BUN)

Reference samples as in "Car part database" above. `nfsu_probe
car-placement GLOBALB.BUN [CAR]` prints the whole table. The evidence, the
placement the game derives from it, and every remaining unknown are in
[`PART_PLACEMENT.md`](PART_PLACEMENT.md); only the layout is repeated here.

### Observed

One top-level 0x00034600 chunk at 1678848 with a 112568-byte payload. The
loader (0x57c8b0) aligns the payload up to 16 bytes and then walks a
hard-coded 35 records of 0xC90 bytes:

| Item | Offset | Size |
| --- | ---: | ---: |
| chunk header | 1678848 | 8 |
| 0x11 alignment filler | 1678856 | 8 |
| 35 x 0xC90 records | 1678864 | 112560 |

The records end exactly at the payload end. The filler is the same 0x11
byte the geometry chunks use, and the alignment is relative to the start of
the file, as it is there.

Fields decoded in a record:

| Offset | Type | Meaning |
| --- | --- | --- |
| +0x000 | char[0x20] | car name, NUL-terminated |
| +0x020 | char[0x20] | lookup name; equal to +0x000 in all 35 records |
| +0x040 | char[0x20] | geometry path, e.g. `CARS\240SX\GEOMETRY.BIN` |
| +0x060 | char[0x20] | the same path with the `.LZC` extension |
| +0x0C0 | char[0x20] | brand name, e.g. `NISSAN` |
| +0x110 | byte[0x2B0] | setup sub-object; 0x575960 copies it per car instance |
| +0x120 | 4 x 0x30 | the four wheel records, below |
| +0xC54 | u32 | class: 0 for the 20 racers, 2 for the 15 traffic vehicles |

One 0x30-byte wheel record:

| Offset | Type | Meaning |
| --- | --- | --- |
| +0x00 | f32 | longitudinal, positive forward |
| +0x04 | f32 | lateral, positive toward the car's left |
| +0x08 | f32 | vertical, positive up |
| +0x0C | u32 | zero in all 140 retail records |
| +0x10 | f32 | wheel radius |
| +0x14 | f32 | equal within an axle; meaning unknown |
| +0x18 | u32 | the record's own index, 0..3 |
| +0x1C | f32 | mirrors the sign of +0x04; meaning unknown |
| +0x20 | f32 | mirrors the sign of +0x04; meaning unknown |
| +0x24 | u32[3] | zero in all 140 retail records |

Index 0 is the front left wheel, 1 the front right, 2 the rear right and 3
the rear left. The frame is the same right-handed frame the geometry file
uses in raw slot order (px, pz, py). Both claims are proven in
[`PART_PLACEMENT.md`](PART_PLACEMENT.md), not assumed.

### Parser rules (`nfsu::parse_car_type_table`)

- The image must be at most `CarTypeLimits::max_file_size`, 256 MiB by
  default. Both entry points apply it: `load_car_type_table` before it reads
  the file, and `parse_car_type_table` before it parses an image the caller
  already has.
- The whole file is read once. Top-level chunks must tile the file exactly;
  exactly one of them is 0x00034600.
- The record array starts at the first 16-byte boundary at or after the
  payload, and every byte before it in the payload must be 0x11.
- The payload must hold all 35 records after the filler. The game would
  read past the chunk instead, because its record count is hard-coded.
- Each of the five string fields must be NUL-terminated inside its 0x20
  bytes, non-empty and printable ASCII, and the two name fields must be
  equal.
- Each wheel record's stored index must equal its position, its four
  reserved words must be zero, and all seven of its float fields must be
  finite: +0x00, +0x04, +0x08, +0x10, +0x14, +0x1C and +0x20, each of which
  has a traced reader.
- Any problem makes the table invalid (`CarTypeTable::valid`), and
  `find_car_type` then refuses it.
