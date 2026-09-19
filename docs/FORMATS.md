# File formats

Clean-room notes on Need for Speed: Underground (PC) data formats. Everything
here comes from inspecting a user-provided retail installation with
`nfsu_probe`. No game data is stored in this repository.

Reference sample for this document:

- File: `Cars/240SX/GEOMETRY.BIN`
- Size: 4780816 bytes
- SHA-256: `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`

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
    0x00134012  unknown, 8 to 88 bytes (multiples of 8)
    0x00134013  unknown, 8 to 56 bytes (multiples of 8)
    0x80134100  geometry container, exactly one per mesh
      0x00134900  geometry metadata
      0x00134B01  vertex buffer
      0x00134B02  unknown
      0x00134B03  face (index) buffer
    0x00134017, 0x00134018, 0x00134019  unknown, in 115 meshes
    0x0013401A  unknown, in 60 meshes
0x80034020  second top-level container (0x00039200, 0x00039201, 0x00039202)
```

Every one of the 400 mesh containers holds exactly one of each of
0x00134011, 0x80134100, 0x00134900, 0x00134B01, 0x00134B02 and 0x00134B03.

Mesh metadata 0x00134011:

- The mesh name is the final 28 bytes of the payload, NUL-terminated ASCII.
  Names are truncated to 27 characters (for example
  `240SX_DECAL_LEFT_QUARTER_RE`).

Alignment prefix: in 0x00134900, 0x00134B01 and 0x00134B03 the record data
starts at the first absolute file offset inside the payload that is a
multiple of a fixed alignment. Every payload byte before that offset is 0x11.
With `align_up(x, a)` = the smallest multiple of `a` that is `>= x`:

| Chunk | Data offset | Payload size |
| --- | --- | --- |
| 0x00134900 | `align_up(payload_offset, 16)` | `prefix + 64` |
| 0x00134B01 | `align_up(payload_offset, 128)` | `prefix + vertex_count * 36` |
| 0x00134B03 | `align_up(payload_offset, 16)` | `prefix + face_count * 6 + tail` |

`tail` is 2 when `face_count * 6` is not a multiple of 4 (odd face counts)
and 0 otherwise. All 400 sample meshes match these rules exactly.

Geometry metadata 0x00134900:

- The record is exactly 64 bytes. Payload is 64 bytes (201 meshes, already
  aligned) or 72 bytes (199 meshes, 8-byte prefix).
- Offsets below are relative to the aligned record start:

| Offset | Type | Meaning |
| --- | --- | --- |
| 36 | i32 LE | Face (triangle) count |
| 52 | i32 LE | Vertex count |

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
  of its three vertex normals (same slot order): 78570 triangles are
  positive, 42 negative and 130 have zero area.

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

- All other fields of 0x00134011 (148 bytes before the name) and of the
  64-byte 0x00134900 record apart from the two counts. Candidates include
  bounds, flags and material or texture references.
- 0x00134B02 (60 to 912 bytes observed). Skipped by the parser.
- 0x00134012, 0x00134013, 0x00134017, 0x00134018, 0x00134019, 0x0013401A.
- The header container 0x80134001 and its children, and the second
  top-level container 0x80034020.
- The color byte order (RGBA vs BGRA) and meaning. Colors are decoded but
  not exported.
- The UV origin (top-left or bottom-left), which texture each mesh uses, and
  what the out-of-range UVs mean (tiling, unused channels or other data).
- Whether other cars or other games in the series use multiple geometry
  containers per mesh, other strides, or other padding rules.
- The meaning of the 42 triangles whose winding disagrees with their
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
- A mesh needs exactly one 0x00134011, 0x80134100, 0x00134900, 0x00134B01
  and 0x00134B03. 0x00134B02 and all other unknown chunks are skipped.
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
