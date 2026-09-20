# Track collision: the polygon array, the leaf cells and the quadtree

Clean-room notes on the static collision of Need for Speed: Underground
(PC). Everything here comes from reading a user-provided retail
installation and its executable read-only. No game data is stored in this
repository.

[`TRACK.md`](TRACK.md) decodes the geometry a track region draws and
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry is
placed. Both say collision is not decoded. This note decodes it:

1. where it is, and why it is in none of the containers those two notes
   read,
2. the five chunks of a collision image and the retail function that sizes
   every record in them,
3. the 32-byte collision polygon, its three quantised coordinate groups,
   their units and the retail routine that turns one into world space,
4. the leaf cells and the index list they slice, and
5. the quadtree over those cells, whose shape is stored nowhere and is
   recovered from the cell centres it has to reproduce.

It deliberately stops there. Five bytes of the polygon record are not
explained here, the other twenty chunks of the same container are not
decoded,
and nothing here claims anything about dynamic collision, the car bodies
the runtime builds, or how the game responds to a hit.

Reference samples for this document:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Tracks/TRACKB1210.lzc` | 244396 | `3EF7DDACDA744DD1F5C610AEDD791A5D4FA5E95C950C340BBE9B6F31D7370B07` |
| `Tracks/TRACKB1001.lzc` | 556152 | `E23BC0A7AFE09355A8DCF6E5845029958CE137821CF9EEB74A329C9EEEC5D078` |
| `Tracks/TRACKB1104.lzc` | 873077 | `7D689551AF173BD4D7C04C0F7A224414D9A2FD2D80A23B7BEDEDB60526CC9F00` |
| `Tracks/STREAML1RB.BUN` | 30675616 | `2C960E19F2EFE9DD260C2E4AFFEF53205B49F666DEF0D0E9BC98DA373065FCC4` |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |

The corpus is the 31 `Tracks/TRACKB*.lzc` files of the installation:
`TRACKB1001` to `TRACKB1008`, `TRACKB1099`, `TRACKB1102` to `TRACKB1109`,
`TRACKB1201`, `TRACKB1202`, `TRACKB1206`, `TRACKB1207`, `TRACKB1210`,
`TRACKB1214` and `TRACKB1301` to `TRACKB1308`.

Offsets are decimal absolute offsets inside the decompressed image unless
written as `+n`, which is relative to the start of the named record.
Executable addresses are `speed.exe` virtual addresses; its image base is
0x00400000 and all three sections map with `file_offset = VA - 0x400000`.

## Collision is in none of the containers already decoded

The five large `STREAML1R*.BUN` files hold seven top-level chunk ids and
nothing else. Walking each as (u32 id, u32 payload size) pairs from offset
0, and recursing into every id with bit 31 set, the whole census of all
five files is:

| Depth | Chunk | Count | Bytes |
| ---: | --- | ---: | ---: |
| 0 | `0x00000000` | 4298 | 1667272 |
| 0 | `0x00034035` | 175 | 28340 |
| 0 | `0x00E34010` | 700 | 17333280 |
| 0 | `0x80034100` | 593 | 2373768 |
| 0 | `0x80134000` | 1231 | 115186280 |
| 0 | `0xB0300100` | 18 | 4776 |
| 0 | `0xB3300000` | 207 | 52948128 |
| 1 and below | the geometry, scenery and texture children of `TRACK.md`, `TRACK_OBJECTS.md` and issue #7 | | |

Nothing in that census is collision, and two candidates that look like it
from their size are ruled out by their content rather than by their id:

- **`0x00E34010` is animation.** The 140 chunks of it in each file are
  byte for byte the same 140 chunks in all five files, total 3466656 bytes
  each. Each payload begins `7F 45 4C 46` after its `0x11` filler, an ELF
  header, and carries plain names: `crowd_female_idle_02_s`,
  `IntroNis28_Camera_cut000_s`, `ZPM_Car1_q`, `ZPM_Car1_t`,
  `XO_SpotlightA_Lamp_00_q`. `speed.exe` handles the id at 0x0047CE55, in
  the same range as the `0x00034035` handler at 0x0047B896, and the two
  ids interleave identically in all five files: one lone `0x00E34010`,
  then 34 of the 35 `0x00034035` chunks each followed by exactly four
  `0x00E34010` chunks and the last by three, which is 140.
- **`0x00135200` is a material table.** It is not in these files at all;
  it is the 139 records of 160 bytes at the head of `Global/GLOBALB.BUN`,
  and they are named `ALUMINUM`, `BOTTOM`, `BRAKEDISC`. `speed.exe`
  handles it at 0x00540F16, beside the `0x00134002`, `0x00134003` and
  `0x00134011` cases of the geometry loader.

Across the whole installation, 207 of the 476 files walk as top-level
chunks to end of file, and the complete set of top-level ids in those 207
is `0x00000000`, `0x00030201`, `0x00030203`, `0x00030210`, `0x00030230`,
`0x00034026`, `0x00034035`, `0x00034036`, `0x00034201`, `0x00034202`,
`0x00034492`, `0x00034600`, `0x00034601`, `0x00034A07`, `0x00034A08`,
`0x00034B00`, `0x00035020`, `0x00035021`, `0x00039000`, `0x0003A000`,
`0x00135200`, `0x00E34009`, `0x00E34010`, `0x80034020`, `0x80034100`,
`0x80034602`, `0x80134000`, `0xB0300100` and `0xB3300000`. None of them is
the collision chunk family below, because the files that carry it are
compressed and so are not chunk trees from offset 0.

That is a statement about the census and not about this project's reach:
the compression is JDLZ, which `nfsu::jdlz_decompress` has decoded since
issue #7. It is declared at `include/nfsu/texture.h:285` and documented at
`docs/FORMATS.md:435`, so these 31 files were already readable with code on
`main`; what was missing was a census that decompressed before walking.

## Where it is, and how to get at it

`Tracks/TRACKB<NNNN>.lzc` is one compressed image per track. The `<NNNN>`
are the same section numbers the per-section files use: every one of the 31
has a `Tracks/TEX<NNNN>TRACK.BIN` and a `Tracks/STREAML1R?_<NNNN>.BUN`
beside it, so `TRACKB1210.lzc` pairs with `Tracks/STREAML1RB_1210.BUN` and
`Tracks/TEX1210TRACK.BIN`.

The compression is JDLZ, the same one the texture blobs of issue #7 use.
`speed.exe` 0x0058D190 is the whole decompressor: it accepts the magic at
0x0058D195 and a version byte of 2 at 0x0058D1A2, reads the decompressed
size at +8 and the compressed size at +12, loads two independent flag
bytes from +0x10 and +0x11 and starts at +0x12. A clear flags1 bit copies
one literal. A set flags1 bit reads two bytes `b0 b1` and consumes one
flags2 bit with them: set gives `length = ((b0 & 0xF0) << 4 | b1) + 3` and
`distance = (b0 & 0x0F) + 1` (0x0058D205), clear gives
`distance = ((b0 & 0xE0) << 3 | b1) + 0x11` and `length = (b0 & 0x1F) + 3`
(0x0058D22D). That is the algorithm `nfsu::jdlz_decompress` already
implements for texture blobs, and this decoder reuses it rather than
adding a second one.

The decompressed image is a flat chunk tree that ends exactly at its
declared size in all 31 files. `TRACKB1210.lzc` unpacks 244396 bytes into
671504 and walks as 42 top-level chunks with no remainder.

`nfsu::jdlz_decompress` stops as soon as the output is full, while retail's
own loop is driven by the packed size, so one or two trailing flag bytes
are left unread. Every one of the 31 tracks leaves exactly one or two, and
`load_track_collision` refuses more than two rather than passing over
whatever is there.

## The loader, and where every record size comes from

`speed.exe` 0x00441380 is the collision loader: one function, five
compares, each case writing a pointer and a count into the single global
structure at 0x0077B130. None of the sizes below is inferred from the
data.

```text
0044138B  lea  edx, [ecx + 0x17]       ; 0x00034130: align the payload
0044139B  sub  esi, edx                ;   up to 16 bytes, then
004413A1  shr  ecx, 3                  ;   count = (end - aligned) / 8
004413B6  lea  edx, [ecx + 0x17]       ; 0x00034131: align up to 16
004413CD  shr  edx, 4                  ;   count = (end - aligned) / 16
004413E2  lea  edx, [ecx + 8]          ; 0x00034132: the payload itself
004413EB  shr  ecx, 5                  ;   count = size / 32
00441423  lea  edx, [ecx + 8]          ; 0x00034133: the payload itself
0044142C  shr  ecx, 1                  ;   count = size / 2
00441440  add  ecx, 0x17               ; 0x00034134: align up to 16
00441446  fld  dword ptr [ecx + 4]     ;   three floats at +0, and
0044145A  fld  dword ptr [ecx + 0x14]  ;   three more at +16
```

| Chunk | Data at | Record | Stored at |
| --- | --- | ---: | --- |
| `0x00034130` | `align_up(payload, 16)` | 8 | +0 pointer, +4 count |
| `0x00034131` | `align_up(payload, 16)` | 16 | +8, +0x0C |
| `0x00034132` | payload | 32 | +0x10, +0x14 |
| `0x00034133` | payload | 2 | +0x18, +0x1C |
| `0x00034134` | `align_up(payload, 16)` | 32, once | +0x30 and +0x40 |

The bytes an alignment skips are `0x11` in every retail image, the same
filler the geometry and scenery chunks use, and the decoder checks them.

Immediately after loading `0x00034132` the loader walks every record it
just read and stores 0xFFFF into the halfword at +6:

```text
004413FC  push edi
00441400  mov  edi, [eax + 0x10]           ; the polygon array
00441403  mov  [edi + ecx + 6], si         ; si = 0xFFFF
0044140C  add  ecx, 0x20                   ; 32 bytes per record
0044140F  cmp  edx, edi
```

so +6 is scratch the runtime overwrites before anything reads it, and the
loop's `add ecx, 0x20` is a second proof of the 32-byte stride, independent
of the `shr ecx, 5` above it. The `sar eax, 5` in the next section is a
third.

## These records are the collision

The array at +0x10 of that structure is the global at **0x0077B140**, and
the physics code names its records `TrackPolygon<n>`:

```text
0046A4D9  mov  esi, [0x77b140]        ; the polygon array
0046A4DF  mov  eax, [ebx + 0x10]      ; one record
0046A4E2  sub  eax, esi
0046A4E4  sar  eax, 5                 ; index = (record - base) / 32
0046A4E8  push 0x6c8850               ; "TrackPolygon%d"
...
0046A75E  mov  ecx, [0x77b140]
0046A76B  shl  eax, 5                 ; base + index * 32
0046A76E  add  eax, ecx
```

The surrounding strings are `TrackPolygonCollisionBody` (0x006C8834),
`AllTrackPolygonCollisionBodies` (0x006C8814),
`TrackPolygonCollisionBodySlotPool` (0x006C8928) and, beside them,
`CarCollisionBody`, `WorldObjectCollisionBody` and `DynamicCollisionBody`.
0x00441B90 reads the +6 halfword of a record to decide whether that record
already has a body.

## The 32-byte polygon (`0x00034132`)

Polygon 0 of `TRACKB1210.lzc`, at offset 263208 of the decompressed image:

```text
263208  00 DB 01 18  F2 FF  FF FF
263216  FC 18 06 19 DB 18 D1 18
263224  BB FF 7C FF 7C FF BB FF
263232  BA FE BA FE 01 03 01 03
```

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | runtime scratch; zero in every retail record |
| 1 | u8 | not decoded here; read by 0x0043FF4D |
| 2 | u8 | not decoded here |
| 3 | u8 | not decoded here; read by 0x0043FFDD |
| 4 | i16 | the whole-unit base of this polygon's vertical |
| 6 | u16 | scratch; the loader stamps 0xFFFF over it |
| 8 | i16[4] | corner slot 0, in eighths of a world unit |
| 16 | i16[4] | corner slot 1, in eighths of a world unit |
| 24 | i16[4] | corner slot 2, in 256ths of a world unit, above +4 |

Its +4 is -14, and decoded that record reads

```text
(799.5, -8.625, -15.2734) (800.75, -16.5, -15.2734)
(795.375, -16.5, -10.9961) (794.125, -8.625, -10.9961)
```

An earlier revision of this note printed the same corners 14 units higher,
with the vertical read as `slot2 / 256` alone. That was wrong, and the
next section is why.

**The three groups are the four corners of one polygon, stored as three
arrays rather than four triples.** Each group holds one value per corner,
and taking corner `i` as `(slot0[i], slot1[i], slot2[i])` is what makes
every check below hold; taking them any other way makes none of them hold.

**The units come from the routine that reads a record into world space.**
`speed.exe` 0x0043FD20 takes a polygon in `eax` and an output vector in
`ecx` and writes four corners of three floats each:

```text
0043FD23  movsx edx, word ptr [eax + 0x10]   ; slot1[0]
0043FD27  shl   edx, 0xd                     ;   << 13
0043FD2E  movsx edx, word ptr [eax + 4]      ; the +4 base
0043FD32  shl   edx, 8                       ;   << 8
0043FD36  movsx esi, word ptr [eax + 0x18]   ; slot2[0]
0043FD3A  add   edx, esi                     ;   + slot2
0043FD3C  shl   edx, 8                       ;   << 8
0043FD43  movsx edx, word ptr [eax + 8]      ; slot0[0]
0043FD47  shl   edx, 0xd                     ;   << 13
0043FD4E  fild  dword ptr [esp + 0xc]
0043FD52  fmul  dword ptr [0x6cc83c]         ; * 1/65536
0043FD58  fstp  dword ptr [ecx]              ; world slot 0
0043FD64  fstp  dword ptr [ecx + 4]          ; world slot 1
0043FD71  fstp  dword ptr [ecx + 8]          ; world slot 2
```

0x006CC83C holds `0x37800000`, which is 1/65536, and the sequence repeats
for corners 1, 2 and 3 at +0x12/+0x1A, +0x14/+0x1C and +0x16/+0x1E into
`[ecx+0x10]`, `[ecx+0x20]` and `[ecx+0x30]`. So, with `base` the int16 at
+4:

```text
world slot 0 = (slot0[i] << 13) / 65536            = slot0[i] / 8
world slot 1 = (slot1[i] << 13) / 65536            = slot1[i] / 8
world slot 2 = (((base << 8) + slot2[i]) << 8) / 65536
             = base + slot2[i] / 256
```

The routine is reached by the chain this note already cites: 0x0046A520
calls 0x00441B90 with the record, which keeps it in `edi` and calls
0x0043FF30, which calls 0x0043FD20.

**The stored bounds agree with those divisors but cannot prove them.**
`0x00034134` is a minimum float triple at +0 and a maximum at +16, and over
all 31 tracks the minimum and maximum of groups 0 and 1 divided by 8, and
of group 2 divided by 256, reproduce those six floats exactly. So do the
minimum and maximum of `base + slot2 / 256`, on all 31. The reason is that
every one of the 42466 saturated wall records carries `base == 0`, so the
stored vertical bound sits at the clamp under either reading and the
measurement cannot tell them apart. That is worth stating plainly rather
than quietly correcting: the bounds evidence in an earlier revision of this
note was not false, it was not decisive, and the note claimed more for it
than it could carry. The code settles it.

**Group 2 is the vertical.** Three separate observations say so:

- Per-polygon extent. Over `TRACKB1210` the median extent of group 0 is
  7.000 world units and of group 1 is 3.500, which is the size of a road
  tile, while the median extent of group 2 is 0.074, and 6103 of its 7841
  polygons vary by less than half a unit in it. `TRACKB1104`, the largest
  track, gives 5.125, 5.250 and 0.270 with 12843 of 21591 flat. If group 1
  were the vertical instead, group 2 would be a horizontal axis on which
  most polygons are a sliver 0.07 units wide, which no collision surface
  can be.
- The saturated records. The group-2 field saturates at 32512, that is
  exactly 127.0, and 42466 of the 290995 retail polygons carry it. Those
  42466 hold exactly one distinct pattern between them,
  `-32512, +32512, +32512, -32512`: a wall that spans the whole vertical
  range while the other two groups trace a line.
  A bounded box query over a road column of `TRACKB1210` returns eighteen
  surfaces between -22.902 and -22.500 and two such walls, which is what a
  street with barriers on both sides looks like.
- The stored vertical bound is exactly -127.0 to +127.0 in **all 31**
  tracks, which is the saturation and not a measurement, while the two
  horizontal bounds differ from track to track. This bullet distinguishes
  the vertical axis from the other two; it does not distinguish the two
  readings of the vertical, for the reason given above.

The transform at 0x0043FD20 settles the axis outright: it writes group 0,
group 1 and the based vertical into components 0, 1 and 2 of one world
vector, and it is the only one of the three that carries a base.

**Groups 0 and 1 are the two horizontal world slots, in the raw slot order
of `TrackVertex`.** Over the 31 tracks group 0 spans -1384.125 to 2446.125
and group 1 spans -1930.250 to 897.750. The scenery instance translations
of `TRACK_OBJECTS.md`, over all 29715 of them, span

```text
slot 0 (px)  -1543.30 ..  3204.68
slot 1 (pz)  -3069.55 ..  2132.55
slot 2 (py)   -401.59 ..   332.17
```

Group 0 reaches 2446.125, past the 2132.55 of slot 1, so group 0 is not
slot 1; group 1 reaches -1930.250, past the -1543.30 of slot 0, so group 1
is not slot 0. With group 2 already fixed as the vertical, that leaves
group 0 = slot 0 = `px` and group 1 = slot 1 = `pz`.

A second check, independent of those ranges, agrees. Rasterise the 29715
scenery instance translations into 64-unit cells of `(px, pz)` and dilate
that set by one cell, giving 2317 cells of world that hold scenery. Then
ask where each collision polygon's centroid falls. Under the reading above,
**290995 of 290995** centroids land in an occupied cell, on every one of
the 31 tracks, with not one exception. Under the swapped reading the same
count is 150767, 51.81 percent, and it varies from 0.0 percent on
`TRACKB1001` to 100 percent on the two tracks that happen to sit in a
near-symmetric part of the map. A track's collision lies where that track's
scenery does, and only one of the two readings puts it there.

What remains an inference is only the naming. 0x0043FD20 fixes which
stored group reaches which component of the world vector, so the ordering
is settled; that component 0 is the one `docs/FORMATS.md` calls `px` is
still an identification made across documents rather than read out of one
instruction.

`docs/FORMATS.md:286` gives the renderer's world map for this slot order as
`x = px`, `y = py`, `z = -pz`. The decoder and every query here work in the
raw slot order and never apply that map; a caller that wants renderer space
applies it once, at the boundary, exactly as the geometry path does.

**The corners are in perimeter order, with exceptions under both of the
two natural tests.** "Convex" has to name a test here, because the records
are three-dimensional and the index is two-dimensional, and the two tests
fail on different records:

| Test, exact integer arithmetic, zero turns allowed | Records failing |
| --- | ---: |
| turn signs of the (slot0, slot1) projection | 12 |
| pairwise dot of the four 3-D turn normals | 23 |
| either | 35 |

The two sets are **disjoint**. An earlier revision of this note said all
290995 were convex with not one exception; that is false under both tests,
and the count depends on which one is meant.

The twelve that fail in projection are listed below. The 23 that fail in
three dimensions are near-collinear quads whose vertical wobbles by one or
two 1/256 steps, which is quantisation rather than shape.

What the triangulation actually needs is weaker than convexity and does
hold: no record is self-intersecting, so (0,1,2) plus (0,2,3) is a
decomposition and not a crossing pair. That is the claim worth making, and
it is the one this decoder relies on.

The twelve that fail in projection:

| Track | Polygon |
| --- | ---: |
| `TRACKB1001` | 9784 |
| `TRACKB1007` | 1034, 1043, 1072 |
| `TRACKB1103` | 6303 |
| `TRACKB1104` | 9143 |
| `TRACKB1107` | 2176, 2185, 2214 |
| `TRACKB1109` | 1457, 1466, 1495 |

They are three distinct shapes reused across tracks, and each is a
non-planar fold: corners 1 and 3 are the same horizontal point at two
different heights, so the horizontal projection is a creased path rather
than a quad and projected convexity is not defined for it at all. The
usual justification for the (0,1,2) plus (0,2,3) triangulation - that a
convex perimeter order makes it valid - therefore does not apply to these
twelve.
What does apply is narrower, and was measured rather than argued. Because
corners 1 and 3 coincide horizontally, the chosen split (0,1,2) plus
(0,2,3) gives two triangles that share the horizontal footprint of the
triangle `c0 c1 c2` and fold about the edge `c0 c2` in the vertical. The
other diagonal, (0,1,3) plus (1,2,3), would give two triangles of exactly
zero horizontal area and so no footprint at all. Measured on the three
distinct shapes, twice the horizontal area is 3, 5 and 2 in squared
eighths under the chosen split and 0 and 0 under the other. So the split
this decoder uses is not merely valid for these twelve, it is the only one
of the two that keeps them. They remain near-degenerate slivers either
way, of the order of a hundredth of a square world unit.


The order is still the right one. No other cyclic order is convex for any
of the 156980 polygons that neither repeat an adjacent corner horizontally
nor project to zero area. The other 134015 are convex read the other way
round as well, because a retraced triangle is convex however it is
traversed, so they are evidence for nothing; a previous revision claimed
the stronger thing for all of them, and that was wrong too.

85710 records repeat an adjacent corner in all three slots, which makes
them triangles stored in a quad slot; 134003 do so in the horizontal
projection alone, and 48237 project to zero horizontal area, which is what
a wall seen from above is. 8304 records are more than 0.01 world units off
their own plane, the worst by 0.0422, which is what 1/256 quantisation of
the vertical produces.

## The leaf cells (`0x00034131`) and the index list (`0x00034133`)

One 16-byte record per cell. Cells 0 to 4 of `TRACKB1210`:

```text
index  slot 0     slot 1     first   count
    0  999999.0   999999.0       0       0
    1     784.0      -16.0       0      49
    2     816.0      -16.0      49      31
    3     784.0       16.0      80      31
    4     816.0       16.0     111      34
```

| Offset | Type | Field |
| --- | --- | --- |
| 0 | f32 | cell centre, slot 0 |
| 4 | f32 | cell centre, slot 1 |
| 8 | u32 | first index |
| 12 | u32 | index count |

Cell 0 is a sentinel: 999999.0 on both axes, an empty slice, and no
quadtree entry names it.

The slices are **contiguous and in cell order**: cell 1 starts at 0 and
each later cell starts where the one before it ended, over all 31 tracks
and all 11898 real cells. What is left at the end is the padding the chunk
needs to reach a multiple of four bytes: 13 of the 31 tracks use an odd
number of indices and carry exactly one spare, and the other 18 carry none.
408569 of the 408582 indices are used.

Every index is below the polygon count, in all 31 tracks. Every polygon is
named by at least one cell: over the corpus, 290995 polygons and zero
unreferenced.

**A cell covers a 32 by 32 square of the two horizontal slots, centred on
its stored point, and is unbounded vertically.** Every one of the 408569
cell/polygon pairs of the corpus has the polygon overlapping that square,
tested exactly rather than by bounding box, with no exception. So nothing a
cell names is there by accident.

The converse holds for every overlap that has area. Taking each polygon
against every cell of its own track that exists and whose square it meets,
there are 410644 such pairs in the corpus, and **2075 of them are not
named** by the cell they meet. Repeating the count with a strict test, one
that requires the overlap to enclose area rather than merely touch a
boundary, gives **341498** pairs and **none** unnamed. The 2075 are
therefore all boundary contacts, and the exporter listed some of those and
not others; its exact rule was not reproduced.

The strict figure is the one a query's completeness rests on, so it was
measured twice by different means. Both work in exact arithmetic on the
stored integers, because a corner is an int16 in eighths of a world unit
and a cell edge is at a multiple of 32 world units, so no tolerance is
needed anywhere: clipping each polygon to each cell square with
Sutherland-Hodgman in exact rationals and asking whether the clipped area
is non-zero gives 341498, and so does an exact-integer separating-axis test
restricted to the polygons the test is valid for. An earlier revision of
this note reported 221868 here. That number was wrong, and wrong in a way
worth naming: the float test that produced it skipped an edge only when
`abs(ex) < EPS`, and the strict run set `EPS` negative, so that comparison
was never true, zero-length edges were kept, and every one of the 134003
polygons with a repeated horizontal corner was declared separate from every
cell.

One more count closes the chain, because "named by a cell that exists" is
not enough on its own: of those same 341498 positive-area polygon/square
pairs, **none** lies in a square that the cell list omits. So a polygon
that really covers ground in a square is both indexed there and reachable.

That is the limit of what a query over this index can promise, and
`track_collision.h` states it at the query declarations, in both
directions: a polygon whose footprint overlaps a query volume with positive
area is always returned, while one that meets it only along a cell boundary
may be reached through another cell or not at all. It is the game's own
index and this decoder does not rebuild it.

## The quadtree (`0x00034130`)

One 8-byte node, four u16 entry slots. Node 0 is the root. A zero entry is
empty.

**The tree's shape is stored nowhere in the file.** What is stored is the
cell centres, and there is exactly one shape that reproduces them: a square
centred on the world origin with a half extent of 16384 world units, split
ten times, with the four entry slots taken as the quadrants
`(-slot0, -slot1)`, `(+slot0, -slot1)`, `(-slot0, +slot1)`,
`(+slot0, +slot1)` in that order, and the entries at the tenth level read
as cell indices instead of node indices. Walking that, over all 31 tracks:

- every one of the 7748 nodes is reached exactly once, and node 0 by no
  entry;
- every one of the 11898 real cells is reached by exactly one leaf entry,
  and the sentinel by none;
- every leaf lands on the centre its cell record stores, to the bit, with
  **zero** mismatches.

Each of the three parts was searched rather than assumed, over all 31
tracks at once:

- **The quadrant order.** All 24 permutations of the four entry slots were
  run. The one above gives 0 centre mismatches. Every other one leaves
  essentially every cell in the wrong square: 21 of them miss all 11898,
  and the closest two miss 11820 and 11785.
- **The depth.** Depths 1 through 14 were run. Only 10 produces a strict
  tree at all; at every other depth the walk fails on all 31 tracks,
  because entries meant for one level are read as the other kind of index.
- **The root half extent.** 4096, 8192, 16384 and 32768 were run at depth
  10. Only 16384 gives 0 mismatches; the other three miss all 11898.

A half extent of 16384 over ten levels leaves leaves of 32 world units,
which is the cell size the pairs above already prove, and cell centres are
then odd multiples of 16, which every retail centre is.

## Parser rules (`nfsu::parse_track_collision`)

The decoder is `include/nfsu/track_collision.h` and
`src/track_collision.cpp`. It never throws for malformed input; every
problem is a `TrackIssue` with an absolute offset into the decompressed
image.

**Bounded by construction**, exactly as `parse_track_region` and
`parse_track_objects` are. `parse_track_collision(data, size, base_offset,
limits)` sees the image's bytes and is told the offset they start at,
because three of the five chunks align their record array to 16 bytes
relative to the start of the image. `load_track_collision` reads one file,
decompresses it into a bounded buffer and parses that; no directory is
scanned and no other file is opened.

1. The image must be a whole number of top-level chunks. A trailing
   fragment shorter than a header, and a chunk whose payload reaches past
   the end, are refused.
2. Exactly one of each of the five ids must be present. A missing one is
   refused by name and a second copy is refused rather than shadowed. Any
   other top-level chunk is counted and ignored, because a retail image
   holds about forty of them and this decoder claims nothing about the
   rest.
3. Each array must be a whole number of its records, must hold at least
   one, and must stay inside its limit. Every byte an alignment skips must
   be `0x11`.
4. Every index must name a polygon the list holds.
5. The cells must slice the index list contiguously from 0, in cell order.
   Cell 0 must slice nothing. At most one index may be left over, and only
   when the used count is odd, which is the padding rule above.
6. The quadtree must be a tree of the shape above: every node reached
   exactly once, every real cell reached exactly once, the sentinel reached
   by none, no entry naming a node or a cell out of range, and every leaf
   landing on the centre its cell stores. A cycle, a shared subtree, an
   orphan node, a cell in two leaves and a cell in none each fail this.
7. Every polygon a cell names must overlap that cell's 32 by 32 square.
   The parser tests the polygon's bounding box against the square, which
   the exact overlap above implies and which is what a broad phase needs;
   it does not re-run the exact test the measurement used.

   This rule runs in one direction only, and the asymmetry matters. It
   refuses a cell that names a polygon lying outside it; nothing in the
   parser requires a cell to name every polygon that lies inside it,
   because nothing in the format says it must. The completeness a query
   depends on is therefore a measured property of the retail exporter's
   output, not an invariant `parse_track_collision` establishes. An image
   that did not come from the game can satisfy all nine rules, report
   `valid == true`, and still answer a query incompletely.
8. The stored bounds must be finite, must not have a minimum above a
   maximum, and must contain every decoded corner. That rule was
   re-derived after the vertical base was added, because it is the one
   rule the correction could have broken: on all 31 tracks the minimum and
   maximum of the corrected corners still equal the stored bounds exactly,
   so containment holds per corner and no track is refused. What the
   vertical bound means is now clearer rather than different - it is the
   range of `base + slot2 / 256`, which saturates at the clamp because the
   walls that reach it carry no base.
9. Limits bound the packed file, the decompressed image, the chunk count
   and each of the four arrays. The three array limits default to 65536,
   which is what a u16 index or a u16 tree entry can address.

An image that fails any rule is reported with `valid == false` and answers
no query, rather than being searched in a state that failed its own checks.

## Queries (`nfsu::query_track_collision_*`)

Three entry points, all in the raw slot order and all bounded twice: by the
quadtree, which stops at the first empty quadrant, and by explicit caps on
cells visited and results returned. A cap that is reached refuses the query
rather than truncating the answer, because a truncated collision answer is
a wrong one.

- `query_track_collision_cells` returns the leaf cells a horizontal box
  touches.
- `query_track_collision_box` returns, ascending and without repeats, the
  polygons the selected cells name whose own box also overlaps a 3-D box.
  It is a broad phase, not a shape test, and it answers out of the index:
  "every polygon whose box overlaps" would be a larger promise than the
  mechanism keeps. On `TRACKB1207.lzc` the box (-336.875, -118.25, -1000)
  to (-325.625, -101.625, 1000) selects only cell 304 and returns only
  polygon 3429, while polygon 3428, whose box is px -342.5..-273.875 and pz
  -123.875..-73.375, overlaps that query box and is not returned; it is
  named by cells 167, 168, 183, 299 and 305 and not by 304, because its
  footprint is a triangle that never reaches into 304. Nothing that really
  overlaps was missed, but the wording had to change. The quadtree indexes
  the two horizontal slots only, so the vertical narrows the answer after
  the cells are chosen.
- `query_track_collision_ray` returns the nearest polygon a bounded ray
  meets, as a polygon index, which of its two triangles, the parametric
  distance and the point. The ray is `origin + t * direction` for `t` in
  `[0, max_distance]`, so nothing behind the origin is ever hit. Polygons
  are two-sided; a degenerate triangle is skipped rather than hit; a tie is
  broken by the lower polygon index so that a run is reproducible. A
  polygon several selected cells name is triangle-tested once per cell,
  which cannot change the answer.

A negative answer from either is a statement about the index, not about
the space: it means nothing is indexed there. Given the boundary-contact
gap above, it is not a proof that the volume is empty.

## The decoded track

```text
file        Tracks/TRACKB1210.lzc
packed      244396 bytes
image       671504 bytes, 42 top-level chunks
```

| | |
| --- | ---: |
| Polygons | 7841 |
| Cells | 277, of which one is the sentinel |
| Indices | 10424 |
| Quadtree nodes | 195 |
| Cell/polygon pairs | 10424 |
| Unreferenced polygons | 0 |
| Saturated vertical polygons | 1464 |
| Polygons with a repeated corner | 970 |
| Source polygon bytes | 250912 |
| Source cell bytes | 4432 |
| Source index bytes | 20848 |
| Source node bytes | 1560 |

```text
stored bounds: px -1033.12 .. 1115.38   pz -94.375 .. 178.375   py -127 .. 127
corner bounds: px -1033.12 .. 1115.38   pz -94.375 .. 178.375   py -127 .. 127
```

A column on a street of that track, at `px 783 pz -8.5`, touches one leaf
cell, the one centred at (784, -16) with 49 polygons. A 16 by 16 box around
it returns 20 polygons: eighteen road surfaces between -22.902 and -22.500
world units high and two walls that span the full -127 to 127. A ray straight
down the column lands on polygon 5221, its second triangle, at
`(783, -8.5, -22.6387)`, which is the surface the neighbouring quads bracket
between -22.676 and -22.602. Every one of those heights is 23 units below
what an earlier revision of this note printed, because that revision dropped
the +4 base and polygon 5221 carries -23.

### Whole-corpus check

Running the decoder once over every one of the 31 tracks:

| | |
| --- | ---: |
| Tracks decoded | 31 of 31 |
| Tracks refused | 0 |
| Polygons | 290995 |
| Cells, sentinels included | 11929 |
| Indices | 408582 |
| Quadtree nodes | 7748 |
| Cell/polygon pairs | 408569 |
| Unreferenced polygons | 0 |
| Saturated vertical polygons | 42466 |
| Polygons with a repeated corner | 85710 |

Not one track raises an issue, and every rule in the list above holds on
every one of them.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and no
game data, screenshot or exported asset may be committed.

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc

build/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc 783 -8.5

build/nfsu_probe track-collision X:/Games/NFSU/Tracks/STREAML1RB.BUN
```

Expected:

1. The first command exits 0 and writes nothing to standard error. Its
   summary lines are exactly the numbers in the table above: `collision:
   valid chunks 42 polygons 7841 cells 277 indices 10424 nodes 195`, then
   `cost: cell polygon pairs 10424 unreferenced polygons 0 saturated
   vertical 1464 degenerate corners 970`, and a `stored bounds` line and a
   `corner bounds` line that read the same.
2. The second command exits 0 and ends with `probe ray: polygon 5221
   triangle 1 distance 1022.64 point (783, -8.5, -22.6387)`.
3. The third command exits 3. `STREAML1RB.BUN` is not compressed and is a
   chunk tree, so it is parsed as it stands and refused with `no
   0x00034130 quadtree chunk in the image`, which is the whole point of the
   first section of this note.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged
   afterwards.

## Observed

- The five large `STREAML1R*.BUN` files hold seven top-level chunk ids, all
  of them accounted for by `TRACK.md`, `TRACK_OBJECTS.md`, issue #7 and the
  animation chunks above. None of them is collision.
- `Tracks/TRACKB<NNNN>.lzc` is a JDLZ image of a flat chunk tree, and all
  31 of them unpack to exactly their declared size and walk to the end with
  no remainder.
- `speed.exe` 0x00441380 sizes the five collision chunks by shifts of 3, 4,
  5 and 1 and two 16-byte-aligned float triples, and the loop at 0x00441400
  stamps 0xFFFF over +6 of every 32-byte record. Three independent places
  in the executable give the polygon stride as 32.
- `speed.exe` 0x0046A4D9 computes a record's index as `(record - base) /
  32` against the same array and formats it as `TrackPolygon%d`.
- `speed.exe` 0x0043FD20 reads a record into a world vector as
  `(slot0 / 8, slot1 / 8, base + slot2 / 256)`, where `base` is the int16
  at record +4. 240793 of the 290995 retail records carry a non-zero base,
  running from -27 to 94; the other 50202 carry zero.
- All 42466 saturated wall records carry `base == 0`, which is why the
  stored vertical bound reproduces under both the corrected reading and
  the one that drops the base.
- The stored `0x00034134` bounds are exactly the minimum and maximum of the
  three coordinate groups divided by 8, 8 and 256, on all 31 tracks.
- Group 2 saturates at 32512 in 42466 polygons, and those 42466 carry one
  single pattern, `-32512 +32512 +32512 -32512`; the stored vertical bound
  is exactly -127.0 to 127.0 on every track.
- Reading groups 0 and 1 as `px` and `pz`, all 290995 polygon centroids of
  the corpus land within one 64-unit cell of a scenery instance
  translation. Reading them the other way round, 150767 do.
- 12 of the 290995 polygons fail a turn-sign convexity test on the
  horizontal projection and 23 fail one on the 3-D turn normals; the two
  sets are disjoint, so 35 fail one or the other and 290960 fail neither.
  None is self-intersecting, which is the property the triangulation
  needs. No other cyclic order is convex for any of the 156980 polygons
  that are neither horizontally degenerate nor zero-area. 85710 repeat an
  adjacent corner in all three slots; the worst record is 0.0422 world
  units off its own plane.
- The cells slice the index list contiguously from 0 in cell order, with at
  most one spare index and only when the used count is odd.
- All 408569 cell/polygon pairs have the polygon overlapping the cell's 32
  by 32 square, tested exactly, and no polygon is unreferenced.
- Of the 410644 polygon/cell overlaps the corpus geometrically has, 2075
  are not named by the cell they meet; all 2075 are boundary contacts. All
  341498 overlaps that enclose any area are named, by two independent exact
  methods, and none of those 341498 lies in a square the cell list omits.
- The quadtree reproduces all 11898 cell centres exactly at depth 10 over a
  square of half extent 16384 centred on the origin, with the quadrant
  order given above. None of the other 23 quadrant orders, none of the
  other 13 depths tried and none of the other three half extents tried
  reproduces them.

## Inferred

- Group 0 is world slot 0 (`px`) and group 1 is world slot 1 (`pz`). The
  evidence is two range containments against the scenery instance
  translations and the centroid test above, not a traced instruction.
- The saturated group-2 records are walls whose vertical extent the
  exporter set to the clamp rather than to a measured height.
- The `<NNNN>` of a `TRACKB` file names the same track section the
  `STREAM*_NNNN.BUN` and `TEX####TRACK.BIN` files name, from the file
  naming alone.

## Unknown

- `0x00034132` +1, +2 and +3. +0 is zero in every retail record and is
  runtime scratch, and +4 is the vertical base decoded above. Of the
  remaining three, an independent review of this branch reported that +1
  packs four 2-bit corner indices read at 0x0043FF4D and that +3 bit 0x10
  is a four-corners-versus-three flag tested at 0x0043FFDD, each holding on
  290995 of 290995 records. Neither is decoded by this parser and neither
  claim is reproduced here; they belong to a separate issue together with
  +2.
- What the runtime does with a polygon once it has one: the surface type,
  the response, and whether any of the three bytes above selects them.
- The other chunks of the same container. A `TRACKB` image also holds
  `0x00034026`, `0x00034027`, `0x00034107`, `0x00034108`, `0x00034121`,
  `0x00034122`, `0x00034124`, `0x00034146`, `0x00034250`, `0x00035020`,
  `0x00035021`, `0x0003B200`, `0x0003B300`, `0x80034020`, `0x80034147`,
  `0x80034150`, `0x80036000`, `0x8003B000`, `0x80135000` and `0x80135100`.
  `0x80135000` and `0x80135100` carry names such as
  `EL_StreetlightAtoE_93`, so they are lights rather than collision;
  nothing else in that list is decoded here.
- The exporter's rule for putting a polygon in a cell it only touches along
  a boundary. It does so for most such pairs and not for 2075 of them.
- Why the vertical is split into a whole-unit base and a 1/256 offset at
  all, when the two horizontal slots are a single fixed-point field. The
  split buys a vertical range of about +-32000 units at 1/256 resolution,
  which neither slot alone could hold, but nothing observed says that was
  the reason.
- What the twelve non-planar folds are meant to be. Corners 1 and 3 share a
  horizontal point at two heights, which is a shape no other record in the
  corpus has.
- Why `0x00034130` needs a tree at all when the cells carry their own
  centres, and what the runtime does when a query leaves the 32768-unit
  square the root covers.
- Everything about dynamic collision: `CarCollisionBody`,
  `WorldObjectCollisionBody`, `DynamicCollisionBody` and the
  `AIWallCollision` pair at 0x007000DC are named in the executable and
  decoded nowhere.
