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
   recovered from the cell centres it has to reproduce, and
6. for issue #158, the whole circuit of Underground race 1: why one
   `TRACKB` file is all of its collision, the proof that it covers the
   route, and the contact layer a vehicle queries, with its units, normals
   and memory cost.

It deliberately stops there. Five bytes of the polygon record are not
explained here, the other twenty chunks of the same container are not
decoded,
and nothing here claims anything about dynamic collision, the car bodies
the runtime builds, or how the game responds to a hit. The contact layer of
the sixth part answers where the track is and which way it faces; what a
contact does to a car is issue #60's.

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

What the file itself holds there is not that stamp. Counted over all 31
tracks, 261315 of the 290995 records already carry 0xFFFF and the remaining
29680 carry values that cover 0x0000 to 0x03FF exactly and with no gap. A
1024-wide range filled end to end is an index rather than noise, and
0x00441B90 below reads this same halfword to decide whether a record
already has a body, beside a pool retail names
`TrackPolygonCollisionBodySlotPool` at 0x006C8928. `TRACKB1301.lzc` is the
clean case: 582 polygons, not one stamp among them, and the values 0 to 581
once each. The reading that fits is a saved body-slot index with 0xFFFF for
"none", and it is not decoded here: `parse_track_collision` counts the two
populations into `TrackCollisionCost::scratch_sentinel_polygons` and keeps
the raw halfword in `stored_scratch`.

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
| 6 | u16 | scratch the loader stamps 0xFFFF over; 29680 records hold a value in 0x0000..0x03FF there instead |
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

## The whole circuit (issue #158)

Issue #158 asks for the static collision of every section the first-lap
circuit uses, loaded with its memory cost, with bounded point and segment
queries that return road or wall contacts in the units and normals issue #60
needs. The circuit is race 1 of Underground mode: event 1, a circuit on
track 1003, `DT1 - Olympic Square`, stream region `L1RA`, 2 laps, forward
([`RACE_EVENTS.md`](RACE_EVENTS.md)).

The answer to "where" is short, and it changes what "extend to every
section" means: **there is no per-section collision to assemble. The whole
circuit's static collision is the whole of `Tracks/TRACKB1003.lzc`**, which
the decoder above already parses in full. What this issue adds is the
evidence for that, the proof that the file covers the circuit, and the layer
a vehicle needs on top of the decode: `include/nfsu/track_contact.h` and
`src/track_contact.cpp`, library `nfsu_track_contact`, and the probe command
`track-contact`.

The files this section's evidence rests on:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |
| `Tracks/STREAML1RA_1003.BUN` | 0 | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `Tracks/TEX1003TRACK.BIN` | 1086216 | `081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Tracks/TRACKB1210.lzc` | 244396 | `3EF7DDACDA744DD1F5C610AEDD791A5D4FA5E95C950C340BBE9B6F31D7370B07` |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |

`GLOBALB.BUN` and not `Global/GlobalB.lzc`, for the reason
[`RACE_EVENTS.md`](RACE_EVENTS.md) gives: the `.lzc` in this installation is
a 2025 modification, and `GLOBALB.BUN` is byte for byte the decompression of
the 2003 backup.

### Where the whole circuit's collision is

**The game holds exactly one static collision set.** Every chunk of a
loaded file is offered to the loaders of one table: `0x00447DD8 push 0x2f`,
`0x00447DDA push 0x7007e0`, `0x00447DE3 call 0x447ac0`, which is 47
entries from 0x007007E0. The third, at 0x007007E8, is 0x00441ED0: it links
`0x00034146` records into a list of its own and hands every other chunk to
the collision loader with one fixed structure,

```text
00441F27  mov  eax, 0x77b130      ; the one collision structure
00441F2C  call 0x441380           ; the five compares of the loader above
```

and 0x00441380 **stores** a pointer and a count for each of the five chunks
into it (`0x00441393 mov [eax], esi`, `0x004413A4 mov [eax + 4], ecx` and
so on); it does not append. The matching unloader, 0x00441F40 at table
entry 0x007008A8, calls 0x00441480, which stores zero into the same fields.
A second collision image would replace the first, not join it.

**That set comes from one file per race.** The track-specific load formats
`TRACKS\TRACKB%d.LZC` from the current track number and nothing else:

```text
00449791  mov  ebp, dword ptr [0x78a2f0]   ; the current track number
004497BD  push ebp
004497BE  push 0x6c9b38                    ; "TRACKS\TRACKB%d.LZC"
004497CE  mov  dword ptr [0x77a91c], 0x6c9b4c
                                           ; "GameFlowLoadingTrackSpecificPart1"
004497EA  call 0x4482f0                    ; the load
```

`RACE_EVENTS.md` shows 0x0078A2F0 is the track number a career race
copies out of the event (0x004ABC58..0x004ABC5C), so event 1 loads
`TRACKB1003.LZC`. The path from the load at 0x004482F0 to the dispatch at
0x00447AC0 is not traced instruction by instruction here.

**No other file a race on 1003 loads carries any collision.** Besides the
track-specific files, the global and track-common loads name theirs with
literal paths, each referenced once, and hand each to the same bundle load
at 0x004482F0 that `TRACKB` goes through; each car's files come from
`CARS\`:

| Load | Path | Referenced at |
| --- | --- | --- |
| global | `GLOBAL\GLOBALA.BUN` | `0x00448608 push 0x6c9e2c` |
| global | `GLOBAL\GLOBALB.LZC` | `0x0044865F push 0x6c9e18` |
| track common | `GLOBAL\INGAMEA.BUN` | `0x004492D7 push 0x6c9cac` |
| track common | `GLOBAL\INGAMEB.LZC` | `0x0044933C push 0x6c9c78`, then `0x00449348` stores the phase name `GameFlowLoadingTrackCommonPart1` |
| track specific | `TRACKS\TEX%dTRACK.BIN` | `0x0044953E push 0x6c9c00` |
| track specific | `TRACKS\STREAM%s.BUN`, `TRACKS\STREAM%s_%d.BUN` | `0x0044970D`, `0x0044972C` |
| track specific | `TRACKS\TRACKB%d.LZC` | `0x004497BE`, above |
| each car | `CARS\%s\TEXTURES.BIN`, `CARS\%s\VINYLS.BIN`, and the shared files under `CARS\` | `0x0058BBDF`, `0x0058BC27` |

`GLOBALB.BUN` and `INGAMEB.BUN` are named beside their `.LZC` at
0x0044867C and 0x0044936D. Walked as chunk trees to the end, recursing into
every id with bit 31 set, a JDLZ file decompressed first:

| File | Chunks | Depths | Walk faults | Chunks with one of the five ids | Raw occurrences of the id bytes |
| --- | ---: | --- | ---: | ---: | ---: |
| `Tracks/STREAML1RA.BUN` | 49464 | 0 to 3 | 0 | 0 | 7, none at a chunk header |
| `Tracks/STREAML1RA_1003.BUN` | 0 | - | 0 | 0 | 0 (the file is empty) |
| `Tracks/TEX1003TRACK.BIN` | 13 | 0 to 2 | 0 | 0 | 0 |
| `Global/GLOBALB.BUN`, byte for byte the image of the 2003 `GLOBALB.LZC.bacc` | 240 | 0 to 3 | 0 | 0 | 0 |
| `Global/GLOBALA.BUN.bacc`, the 2003 `GLOBALA.BUN` | 20 | 0 to 2 | 0 | 0 | 0 |
| `Global/InGameB.lzc`, decompressed, byte for byte `INGAMEB.BUN` | 224 | 0 to 3 | 0 | 0 | 1, not at a chunk header |
| `Global/INGAMEA.BUN` | 0 | - | 0 | 0 | 0 (the file is empty) |
| `Cars/`, all 120 files | 120853 | 0 to 3 | 0 | 0 | 35, none at a chunk header |

`GLOBALA.BUN` in this installation is dated 2025, like `GlobalB.lzc`, and is
388 bytes longer than its `.bacc`. Neither modified file changes the
answer: `GLOBALA.BUN` walks as 23 chunks and `GlobalB.lzc` as 270, with
none of the five ids in either, and nor do the other files under `Global/`,
`DYNTEX.BIN` (14 chunks), `HUDTEX.BIN` (14) and the empty `CHARACTERSA.BIN`.

`TRACKB1003.lzc` itself holds exactly one of each of the five, which
`parse_track_collision` requires and reports valid. So the collision the
game has for this circuit is that one image, all of it, and "every section
the circuit uses" is a property of the image to check rather than a set of
images to load.

### The image covers the circuit

Coverage needs the circuit's path, and `TRACKB1003.lzc` carries one.
Container `0x80034147` holds `0x00034148`, 40 bytes, and `0x00034149`, 620
bytes, which is 31 records of 20:

| Offset | Type | Field |
| --- | --- | --- |
| +0 | f32 | slot 0, `px` |
| +4 | f32 | slot 1, `pz` |
| +8 | 8 bytes | `00 00 FF FF FF FF FF FF` in all 31 |
| +16 | f32 | a running value: 0 at the first point, rising to 3200.343 at point 29, and 0 again at point 30 |

The header stores 30 twice at +4 and +6 and 3318.270 at +12. The last point
lies 4.89 units from the first, so the line is closed. The running value
exceeds the straight-line length of the polyline by an amount that grows at
every corner, 0.1 after the first and 25.9 by point 29, which is what a
length measured along a curve through the points would do; that reading is
an inference.

Over all 31 `TRACKB` files the same container tells the race types apart
without being asked: the eight tracks 1001-1008, where every circuit race
runs, and the eight drift tracks 1301-1308 carry a closed line (the last
point 0.5 to 4.9 units from the first), and the eight sprint tracks
1102-1109 and the six drag tracks carry an open one (259 to 2652 units).
`TRACKB1099`, which no quick-race mode offers, carries a closed one with a
12.3-unit gap. **Observed:** a closed line on every loop track and an open
one on every point-to-point track. **Inferred:** it is
the race route. Decoding the route belongs to issue #61; here it is only a
line to sample the collision along, and it is labelled as that.

Sampled every world unit, 3316 samples, each probed with the column query
below from py 1000 down 2000 (`nfsu_probe track-contact <file> px pz` does
exactly this for one sample):

| First contact down the column | Samples |
| --- | ---: |
| road | 2858 |
| wall | 0 |
| clear: a cell covers it, no surface under it | 428 |
| outside: no cell at all | 30 |
| **road within 10 world units to either side, perpendicular to the line** | **3316 of 3316** |

The worst sample needs the full 10 units. The misses are the line running
where no car drives: points 3 and 4 lie at pz -322.04, on the median of a
divided road whose two carriageways are at pz -346..-328 and -304..-298 and
whose median has no surface, and the rest are bends the straight chords
cut across. Probed at the 31 points themselves, 28 hit road directly and
points 3, 4 and 28 are clear. The route passes over the ground of 13 of the
image's own visibility zones ([`TRACK_ZONES.md`](TRACK_ZONES.md)), in the
order 1003, 1002, 1001, 1104, 1105, 1106, 1103, 1201 to 1206 and back to
1003, and those 13 zones name 119 stream sections to draw between them:

| Zone section | Route samples | A road surface under the sample | Farthest to road |
| --- | ---: | ---: | ---: |
| 1001 | 271 | 141 | 8.0 |
| 1002 | 254 | 126 | 9.5 |
| 1003 | 331 | 300 | 9.5 |
| 1103 | 324 | 216 | 10.0 |
| 1104 | 247 | 247 | 0 |
| 1105 | 275 | 275 | 0 |
| 1106 | 139 | 139 | 0 |
| 1201 | 212 | 212 | 0 |
| 1202 | 308 | 308 | 0 |
| 1203 | 211 | 211 | 0 |
| 1204 | 282 | 282 | 0 |
| 1205 | 209 | 209 | 0 |
| 1206 | 253 | 192 | 4.5 |

**The collision is the track's own.** The same 3316 samples against other
tracks' images:

| Image | A road surface under the sample | Outside every cell |
| --- | ---: | ---: |
| `TRACKB1003` | 2858 | 30 |
| `TRACKB1001` | 0 | 3316 |
| `TRACKB1004` | 844 | 2334 |
| `TRACKB1102` | 2651 | 191 |
| `TRACKB1210` | 38 | 3223 |

Another circuit of the same stream region covers none of this one's route,
so the eight `L1RA` circuits do not share one city-wide collision: each
track's file covers that track.

Across a street, the walls are there: at the first route point, a sweep at
py 56.5 from `(904.24, -94.76)` toward `(944, -74)` meets polygon 9768 as
wall 14.562 units along, and the opposite sweep toward `(864, -115)` meets
polygon 9770 15.373 units along, each normal facing back toward the route.

### What `nfsu_track_contact` adds

`load_track_contact_world` reads one `TRACKB` file exactly as
`load_track_collision` does, releases the decompressed image as soon as it
is parsed, and derives a table of two triangles per polygon, (0,1,2) and
(0,2,3), each with its unit normal and whether it is road or wall. Two
queries sit on it, both answering out of the game's own index through the
same quadtree as the queries above:

- `query_track_contact_segment(world, from, to)`: the first triangle the
  segment crosses, road or wall. This is the sweep a body makes.
- `query_track_contact_point(world, point, rise, drop)`: the segment from
  `rise` above the point straight down to `drop` below it, first contact
  from the top. This is the column a wheel probes, and under a bridge it
  meets the deck before the road beneath.

Each answers one of four statuses, which are the distinctions issue #60's
airborne, out-of-bounds and reset cases need:

| Status | Meaning |
| --- | --- |
| `hit` | a contact, filled |
| `clear` | at least one leaf cell's closed square meets the query's horizontal projection, and nothing those cells name is met: airborne |
| `outside` | no leaf cell's closed square meets any part of the query's horizontal projection: the index places no collision there, out of bounds |
| `refused` | the world is not valid, an argument is null, not finite, zero-length, negative or over a cap |

A contact carries the polygon index (the `n` of retail's `TrackPolygon<n>`),
which triangle, road or wall, the distance from the segment's start in
world units, the point, the unit normal and the cosine of the slope.

**Bounded.** A segment longer than `max_length`, 4096 world units by
default, is refused, and so is one that would meet more than `max_cells`
leaf cells, 16384 by default, counting only the cells whose square the
segment's horizontal projection meets: a cap refuses, it never truncates,
because a truncated collision answer is a wrong one. Within the caps the
work is the cells visited times the polygons each names, both bounded by the
parsed arrays. A world whose report is not valid, and a world assembled by
hand whose triangle table does not match its polygons, answer nothing.

**Invalid geometry.** A triangle whose cross product is zero or not finite
is degenerate: it gets no normal, is counted, and is never met. On the
retail corpus that is exactly the 85710 records that repeat an adjacent
corner in all three slots, one triangle each. A segment that lies in a
triangle's plane never meets it, which is why a sweep along a barrier line
is clear.

### Which cells, and which triangle

**Cells.** A query selects the leaf cells whose closed 32 by 32 square
meets the horizontal projection of its segment. It walks the quadtree
`query_track_collision_cells` walks, from the same root and in the same
quadrant order, and prunes every node the segment misses: first by the box
test that query makes, then by asking whether all four corners of the
square lie strictly on one side of the segment's line. That side is a 2x2
determinant in double, Shewchuk's orient2d, and a corner nearer the line
than its rounding bound counts as on it, so a doubtful square is kept;
keeping one can only add polygons for the triangle test to reject. A square
the segment touches at a corner counts. For a column the footprint is a
point and the cells are exactly the ones issue #39's ray selects. So
`outside` means what the table above says, and the cell cap counts only the
cells the segment meets, never the others its bounding box reaches.

The review of this issue found the first version selecting cells by the
segment's bounding box. `nfsu_probe track-contact <TRACKB1003.lzc> 731 486
50 742 475 50`, a sweep passing 0.7 units diagonally off the corner of cell
(720, 464) with no cell beyond that corner, printed `probe segment: clear`;
it now prints `probe segment: outside`, as the columns at both of its ends
and at its midpoint do. Over 300000 random segments per track, 0.5 to 60
units long and a quarter of them horizontal, checked against every cell's
square by an independent clip and against a brute force that tests every
non-degenerate triangle in double with closed bounds through no game index:

| Image | Hit | Clear | Outside | Clear with no cell met | Outside with a cell met | Brute force: missed, extra, other distance |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `TRACKB1003` | 40647 | 59395 | 199958 | 0 (the first version: 1318) | 0 | 0, 0, 0 |
| `TRACKB1210` | 68298 | 102681 | 129021 | 0 (the first version: 1620) | 0 | 0, 0, 0 |

**Triangles.** Issue #39's Moller-Trumbore arithmetic decides first: the
nearest triangle it accepts among the triangles that are not degenerate, a
tie going to the lower polygon and then the lower triangle. So wherever the
first triangle `query_track_collision_ray` meets along the same line is not
degenerate, the contact is that hit, bit for bit in polygon, triangle,
distance and point. A degenerate triangle is the exception. The contact
never meets one; the #39 ray does not skip one, and on an oblique line its
float determinant for a triangle with two equal corners - a quad slot that
repeats a corner - can come out nonzero, so it accepts the triangle at a
distance that is only rounding. There the contact answers the nearest other
contact instead. The review of this issue found `track-contact
<TRACKB1003.lzc> 1478.58521 -8.32364178 61.1099014 1485.66479 -7.80135822
57.4213486` answering `probe segment: clear` while the #39 ray reports
polygon 536's degenerate triangle 1 at 3.692308; in exact arithmetic the
segment meets the closed outer edge of the same polygon's triangle 0,
3.99996 along, which the contact misses by rounding, a case measured under
"Tests, and the breaks they catch" below. On the edge-aimed oblique sweeps
described there, the first triangle the #39 ray meets is degenerate on 5076
of the 611352 on `TRACKB1003` and 729 of the 529632 on `TRACKB1210`, and
the contact answers farther along on 4978 and 715, nearer - through the
watertight test - on 85 and 13, at the same distance on another triangle on
0 and 1, and clear on 13 and 0; on every other sweep where the ray meets
something the contact is that hit. It never happens on a column: for a
vertical line the determinant of a triangle with two equal corners is
exactly 0, and every retail degenerate triangle has two. Nor did it happen
on any of 300000 random segments on each of the two tracks.

That arithmetic is not watertight either. It rounds each barycentric on its
own, and a point exactly on an edge can round outside every triangle that
has it: the review found road seams on retail data that it misses from both
sides, such as the column `track-contact <TRACKB1003.lzc> 961.1171875
-244.921875`, on the edge polygons 1980 and 1986 share, which answered
`clear`. So when it accepts nothing anywhere along the segment, the
watertight test of Woop, Benthin and Wald (2013) decides instead. Each
corner is sheared into the segment's frame in float by the same operations
whichever triangle it belongs to, and an edge function over two corners is
a difference of the same two products in both triangles that share the
edge, so it has the same value with the opposite sign in each; taken in
double, it has the exact sign for the sheared corners. A point on a shared
edge is met by both triangles, a point beside it by the one it lies in, and
a polygon's own outer edge is met too. On a column the shear only moves
each corner to the column's start - exact wherever that difference is
itself a float - so that sign is the triangle's own; on an oblique segment
the corners are rounded in float as they are sheared, so a point within
about a float rounding of an outer edge can go either way, as measured
under "Tests, and the breaks they catch" below. That column now answers
`hit road polygon 1980 triangle 0 distance 940.824`, and the sweep through
the same point from py 59.5 down to 58.5 `hit road polygon 1980 triangle 0
distance 0.324219`.

Columns at 17 points along every edge two road polygons share, and at 15
points inside every road quad's (0,2) diagonal, from py 1000 down 2000:

| Image | Shared road edges | Columns on them | The #39 ray misses | The contact misses | Diagonal columns | The contact misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `TRACKB1003` | 10568 | 179656 | 4 | 0 | 87780 | 0 |
| `TRACKB1210` | 8844 | 150348 | 4 | 0 | 80565 | 0 |
| `TRACKB1107` | 25448 | 432616 | 20 | 0 | 191085 | 0 |

**What that leaves.** The watertight test only adds contacts where the #39
arithmetic meets nothing that is not degenerate, and on the lattices under
"Region against whole" below every one it adds lies exactly on a triangle's
edge: of the 17 columns where the ray meets nothing and the contact meets
something, 16 are on a polygon's outer edge with nothing beyond it, such as
(754, 209) on polygon 3101 of `TRACKB1003`, and 1 is on a seam, (694, -4)
on polygon 1159 of `TRACKB1207`. The price of keeping every #39 hit on a
triangle that is not degenerate bit for bit is the converse: where the #39
arithmetic meets something farther along, a nearer surface met only on its
own edge does not replace it. Two columns of those lattices are that case,
both on `TRACKB1003` and both a polygon's outer edge above a lower road:
(1690, 486) answers polygon 4247 where polygon 3972's edge is 0.1 units
higher, and (979.5, 281.5) answers polygon 2315 where polygon 2259's edge
is 0.93 units higher. Both are single points; issue #39's ray answers them
the same way.

### Units, frame and normals, for issue #60

- **Units.** Every coordinate and distance is a world unit: the unit of the
  collision records, of the render geometry and of the scenery translations.
  One world unit is about one metre: the 240SX body measures 4.374 by 1.929
  by 1.243 units against a real 4.31 by 1.815 by 1.315 m car
  ([`TRACK_RENDER.md`](TRACK_RENDER.md)), and the aftermarket rim diameters
  quantise to exact inches as metres, 0.29210 being 11.5 inches
  ([`PART_PLACEMENT.md`](PART_PLACEMENT.md)).
- **Frame.** The raw slot order of this note: component 0 is `px`,
  component 1 is `pz`, component 2 is `py`, the vertical, and a larger `py`
  is higher. Nothing in the layer applies the renderer's map.
- **Conversion.** `track_slot_to_render` is the map of
  [`FORMATS.md`](FORMATS.md): `x = px`, `y = py`, `z = -pz`, and
  `track_render_to_slot` is its exact inverse. It is a rotation with
  determinant +1, so a normal converts the same way, keeps its length, and a
  road normal becomes the renderer's +y.
- **Normals.** Unit length, in the raw slot order, and **facing the query**:
  the dot product with the segment's direction is negative. The collision
  is two-sided, so a barrier hit from either side reports the normal of the
  side it was hit from, and a column from above onto a road reports a
  normal with `py > 0`.
- **Road or wall.** A triangle is road when the magnitude of its normal's
  `py` component is at least 0.5, a slope of 60 degrees, and wall otherwise.
  That is this project's rule, a compatibility approximation, and not the
  game's: the retail surface type is not decoded. It was chosen where the
  retail data is sparse. Over the 496280 non-degenerate triangles of all 31
  tracks:

  | `abs(normal py)` | Triangles |
  | --- | ---: |
  | below 0.05 | 96808 |
  | 0.05 to 0.3 | 705 |
  | 0.3 to 0.5 | 320 |
  | 0.5 to 0.7 | 589 |
  | 0.7 to 0.95 | 3092 |
  | 0.95 and above | 394766 |

  so moving the threshold anywhere between 0.3 and 0.7 reclassifies at most
  909 of them. Under it the corpus is 398447 road, 97833 wall and 85710
  degenerate triangles.

### The decoded circuit: `L1RA`, track 1003

```text
file        Tracks/TRACKB1003.lzc
packed      433352 bytes, 2 flag bytes unread
image       961328 bytes, 43 top-level chunks
```

| | |
| --- | ---: |
| Polygons | 9958 |
| Cells | 382, of which one is the sentinel |
| Indices | 13818 |
| Quadtree nodes | 243 |
| Cell/polygon pairs | 13817 |
| Unreferenced polygons | 0 |
| Saturated vertical polygons | 1038 |
| Polygons with a repeated corner | 2934 |
| Road triangles | 14634 |
| Wall triangles | 2348 |
| Degenerate triangles | 2934 |

```text
stored bounds: px 664.25 .. 1771.5   pz -469.875 .. 822.5   py -127 .. 127
```

**Memory**, every figure a size times a `sizeof`, so the same on every
64-bit build:

| | Bytes |
| --- | ---: |
| Resident: polygons, 9958 of 96 | 955968 |
| Resident: cells, 382 of 32 | 12224 |
| Resident: indices, 13818 of 4 | 55272 |
| Resident: quadtree nodes, 243 of 48 | 11664 |
| Resident: triangles, 19916 of 20 | 398320 |
| **Resident in all, 5 allocations** | **1433448** |
| The same five arrays as the file stores them | 354348 |
| Load buffers: the packed file and the image | 433352 + 961328 |
| Load peak: both buffers and the four parsed arrays at once | 2429808 |

The resident form is four times the stored one because the decoder keeps
each polygon's world-space corners and offsets beside the raw record. That
is a choice of this decoder, not a property of the format, and it is small
against every budget below.

**Against the budgets of issue #52** ([`MEMORY_BUDGETS.md`](MEMORY_BUDGETS.md)).
The collision is loaded once per race and lives until the race ends, which
is the persistent category; the load buffers are released when the load
returns, which is not a frame, so they are placed in the streaming arena
rather than the one-frame transient one. `place_track_contact_memory` does
exactly that against the plan `switch_memory_budget()` builds, and releases
the buffers afterwards:

| Arena | Capacity | Held | Per mille |
| --- | ---: | ---: | ---: |
| persistent, the resident arrays | 67108864 | 1433448, extent 1433456 | 21 |
| streaming, the load buffers at their peak, then released | 50331648 | 1394680 | 27 |

On the desktop there is no arena plan, and the figure a budget has to hold is
the derived ceiling: `track_contact_resident_ceiling` of the default
`TrackCollisionLimits` is 30932992 bytes, every array at its limit, which
fits the 64 MiB persistent placeholder by itself. Across the 31 retail
tracks the largest resident world is `TRACKB1107` at 3483760 bytes and the
largest load peak is 5491284, the same track.

### Region against whole, and the pins of issue #39

The pins above are untouched: the decoder, the parser rules and the three
queries of issue #39 are not modified by this issue, `nfsu_track_collision_tests`
passes unchanged, and the retail acceptance below reproduces every number
this note already printed.

**Synthetic.** `tests/track_contact_tests.cpp` builds four sections of track
far apart, each in its own part of the quadtree - a street between two
barriers with a gap in its surface, a hill with a 45-degree ramp and a
70-degree face, a bridge deck over a road with a pier under it, and a slab
across the world origin that every root quadrant shares - into one image,
and each section alone into an image of its own. Over a lattice of columns
from three heights and sweeps in four directions at six heights, **11694
queries, 5446 of them contacts, answer bit for bit the same** over the
whole and over the section alone, with only the polygon index offset by
where the section sits in the whole. Every column of that lattice, 10830 of
them, is also held to `query_track_collision_ray` of issue #39 over the
whole image whatever either answers: at the 5112 where the ray meets
something the column is the same polygon, triangle, distance and point, and
where the ray meets nothing so does the column, since no column of that
lattice lies on an edge the #39 arithmetic misses. The other side is held
by its own images, found by a search over the format's own grid, that the
ray misses and the contact meets: a flat seam, met by a column, by sweeps
through it from both sides and by sweeps that start and that end exactly on
it; a flat quad's outer edge; and a seam between two sloped quads, where the
contact is pinned to the road's exact height there, 7.30010986328125, to
1e-4.

**Retail.** Columns from py 1000 down 2000 over each track's stored
bounds, on three lattices: the one-unit lattice at half-unit offsets this
note has used from the start, and the two the review of this issue used, a
one-unit lattice on whole units and a 0.75-unit lattice offset by (0.37,
0.61). Wherever the ray of issue #39 meets something, on every column of
every lattice, the contact column is the same polygon, triangle, distance
and point:

| Image | Lattice | Columns | Ray hits | Contact hits | Clear | Outside | Ray hit, contact differs | Contact only, on an edge |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `TRACKB1210`, issue #39's own region | half-unit | 586677 | 125578 | 125578 | 153834 | 307265 | 0 | 0 |
| `TRACKB1003`, the circuit | half-unit | 1432644 | 178769 | 178771 | 206861 | 1047012 | 0 | 2 |
| `TRACKB1207`, issue #39's box pin | half-unit | 1058216 | 119675 | 119677 | 185571 | 752968 | 0 | 2 |
| `TRACKB1210` | whole-unit | 589100 | 126044 | 126044 | 157830 | 305226 | 0 | 0 |
| `TRACKB1003` | whole-unit | 1432644 | 178896 | 178903 | 212998 | 1040743 | 0 | 7 |
| `TRACKB1207` | whole-unit | 1060827 | 120124 | 120130 | 189961 | 750736 | 0 | 6 |
| `TRACKB1210` | 0.75, offset | 1039995 | 222949 | 222949 | 271682 | 545364 | 0 | 0 |
| `TRACKB1003` | 0.75, offset | 2543148 | 316319 | 316319 | 368714 | 1858115 | 0 | 0 |
| `TRACKB1207` | 0.75, offset | 1879890 | 212519 | 212519 | 329032 | 1338339 | 0 | 0 |

The seven whole-unit columns of `TRACKB1003` the contact alone meets are
the seven the review found answering clear over road, all on outer edges.

and issue #39's column pin reads the same through the new command:
`probe column: hit road polygon 5221 triangle 1 distance 1022.64 point
(783, -8.5, -22.6387)`.

### Tests, and the breaks they catch

`nfsu_track_contact_tests` runs 28184 checks over the synthetic sections:
the triangle table and its counts, every point and segment case above, the
region-against-whole lattice with every column held to the #39 ray, the
refusals and both caps at their exact boundaries, the memory plan including
a refusal one byte short and a world built in memory, the coordinate map,
and a load from a raw and a JDLZ-packed file. For the reviews of this issue
it adds a sweep whose bounding box reaches a cell the segment does not, a
sweep through a cell's corner, the cell cap inside one node of the tree and
across the root's quadrants, road wound both ways, a slope of exactly 60
degrees in float, a contact exactly at a segment's end and at its start, a
column on a deck's own outer edge above a road, and the edges the #39
arithmetic misses: a flat seam, also met by sweeps that start and end
exactly on it, a flat outer edge, a seam between two sloped quads with its
height pinned, and two walls' own (0,2) diagonals met by oblique sweeps
with the distance pinned. Each test says in a comment which break it would
catch, and a failure message reports the failing query's own answer.
`nfsu_probe_track_contact_guards` drives the command over the same image
written to disk, pinning exit status and output for 23 cases, and the
fixture writer's refusal to replace a file.

The first version of `src/track_contact.cpp`, run whole under these tests,
fails 14 checks - two sweeps off a cell's corner, the cell cap twice, the
flat seam as a column, as sweeps through it from above and from below and
as sweeps that start and end on it, the flat outer edge, the sloped seam
as a column and as a sweep, and the two walls' diagonals - and the guard
case of the sweep off the corner.

The sweep of deliberate breaks this note printed before the second review
counted 33 of 34 caught, and that overstated the watertight fallback: it
held no break of the fallback's distance or of its two closed ends, and
the second review found three there - `t < 0` made `t <= 0`, `along >
max_distance` made `>=`, and the distance weighting the wrong corner's
height - that change retail answers while every test passed. The fallback's
own cases were all flat, where every corner's height is the same and no
weighting can move the answer, and none put a fallback contact at either
end of a segment. The rows marked G1 below are those three, each caught by
a test added for it; the row after them, a full swap of the two weights,
is caught by the same sloped seam.

The 45-break sweep printed after that still overstated it. Every fallback
contact those tests made lay on a triangle's (b,c) edge, where the edge
function `u` is exactly 0, so the fallback's weight on corner a - the `u`
term of its distance and `u`'s share of its determinant - never counted,
and the third review found two breaks there that change retail answers
while every test passed: on `TRACKB1003` the sweep `track-contact
<TRACKB1003.lzc> 1473.10193 37.9745102 53.7840652 1467.63245 32.2598648
54.9791183` meets wall polygon 0 4.00002 along, and under the two breaks
2.41417 and 5.81821 along, and the sweep from `1468.14014 31.3876724
54.1409225` to `1470.76611 34.7685776 60.8991165` meets the same wall
3.99998 along and under either break passes through it. The rows marked G2
are those two, caught by two
oblique sweeps through walls' own (0,2) diagonals - an (a,b) and a (c,a)
edge, where `u` is not 0 - that the #39 ray misses, found by a search over
walls on the format's grid.

The sweep was rerun whole on the current tests with the third review's
other fallback breaks added: 50 breaks, each applied alone, the test
program and the probe rebuilt and both run, and the file restored byte for
byte and touched. A run counts as caught only with a non-zero exit, at
least one `FAIL:` line and the `check(s) failed` summary, as `CLAUDE.md`
requires.

| Break | Result | First failure |
| --- | --- | --- |
| the slope's magnitude dropped, `up = normal[2]` | caught, 3 | `both windings are road: got road 4 wall 2` |
| the first contact made to beat the untouched starting best | caught, 4 | `a contact exactly at the segment's end is met: got polygon 12 at 10` |
| the #39 test's `u + v > 1` made `>= 1` | caught, 1 | `a column on the deck's own edge meets the deck, not the road beneath: got polygon 2 at 7` |
| the road threshold made exclusive | caught, 1 | `a slope exactly at the threshold is road: got road 0 wall 2` |
| the #39 test's `u < 0` made `<= 0` | caught, 68 | `origin: the ray query of issue #39 agrees at (-29, -29, 150)` |
| the #39 test's `t > max` made `>= max` | caught, 9 | `a tie goes to the lower polygon index at px 3` |
| a same-polygon tie to the higher triangle | caught, 68 | `origin: the ray query of issue #39 agrees at (-29, -29, 150)` |
| zero-size load buffers placed | caught, 1 | `a world with no load buffers is placed without asking the streaming arena` |
| only the packed buffer released | caught, 1 | `the load buffers are released when the load is over` |
| the triangle-table size check removed | caught, 1 | `a triangle table that does not match the polygons refuses` |
| the column's rise ignored | caught, 2 | `a wheel sunk 1 unit finds the road above it` |
| no cell answered as clear, not outside | caught, 8 | `a column no cell covers is outside` |
| normal never turned to face the query | caught, 3 | `from outside, the normal faces -pz` |
| the query's cell cap not passed on | caught, 3 | `three cells met over a cap of two refuse` |
| slope read from `pz` instead of `py` | caught, 28 | `road triangles: got 6, want 15` |
| a tie broken by visiting order | caught, 9 | `a tie goes to the lower polygon index at px 3` |
| distance reported as a fraction of the segment | caught, 5127 | `two units down: got 0.200000003` |
| load peak forgets the image | caught, 1 | `the peak is the buffers and the parsed arrays together` |
| the renderer map written as a reflection | caught, 3 | `slot (px, pz, py) is render (px, py, -pz)` |
| a negative rise accepted | caught, 2 | `a negative rise or drop refuses` |
| the length cap refusing its own length | caught, 1 | `a sweep exactly at the length cap runs` |
| cells chosen from the segment's start only | caught, 6 | `of two barriers, the nearer: got polygon 2 at 4` |
| the road threshold inverted | caught, 26 | `road triangles: got 10, want 15` |
| a refused placement keeping what it placed | caught, 1 | `a refused world leaves nothing placed` |
| resident bytes forgetting the triangle table | caught, 5 | `resident bytes are the five arrays` |
| a cell's index-list slot taken as the polygon | caught, 9122 | `the higher surface first: got polygon 0 triangle 0` |
| cells chosen by the bounding box | caught, 4 | `a sweep past the corner of every cell is outside, not clear` |
| a corner touch not counted as meeting | caught, 5163 | `a wheel over the street finds the road` |
| a square separated when three corners lie on one side | caught, 7 | `a sweep through a cell's corner meets the cell and is clear` |
| the cell cap refusing one cell late, `>` for `>=` | caught, 3 | `three cells met over a cap of two refuse` |
| the cap counting the cells of the bounding box | caught, 1 | `inside one node, three cells met under a cap of three run` |
| the watertight fallback removed | caught, 10 | `a column exactly on the seam meets the road` |
| the watertight bounds made open | caught, 8 | `a column exactly on the seam meets the road` |
| the watertight sign test dropping one edge function | caught, 835 | `the gap in the road surface is clear` |
| the watertight determinant's sign ignored | caught, 1 | `a sweep up through the seam meets it facing down` |
| the watertight determinant's sign inverted | caught, 8 | `a column exactly on the seam meets the road` |
| the shear's handedness swap removed | caught, 6 | `a column exactly on the seam meets the road` |
| the watertight test deciding first | caught, 112 | `hill: the ray query of issue #39 agrees at (1635, 707, 150)` |
| G1: the fallback's `t < 0` made `t <= 0` | caught, 1 | `a sweep that starts exactly on the seam meets it at distance 0: got clear` |
| G1: the fallback's `along > max_distance` made `>=` | caught, 2 | `a sweep that ends exactly on the seam meets it at its end: got clear` |
| G1: the fallback's distance weighting `z[2]` for `z[1]` | caught, 2 | `a column exactly on a sloped seam meets the road at its own height: got hit polygon 0 at distance 31.2421875 height 8.7578125, want height 7.30010986` |
| the fallback's distance with `z[1]` and `z[2]` swapped | caught, 2 | `a column exactly on a sloped seam meets the road at its own height: got hit polygon 0 at distance 31.4212036 height 8.57879639, want height 7.30010986` |
| the fallback's distance weighting `z[0]` for `z[2]` | caught, 3 | `a column exactly on a sloped seam meets the road at its own height: got hit polygon 1 at distance 29.6627197 height 10.3372803, want height 7.30010986` |
| G2: the fallback's distance weighting `z[1]` for `z[0]` | caught, 2 | `a sweep rising through a wall: a sweep through the wall's diagonal meets it 2.43990254 along, facing it: got hit polygon 0 at distance 2.72446704 height 18.1445312` |
| G2: the fallback's determinant without `u`, `v + w` | caught, 2 | `a sweep rising through a wall: a sweep through the wall's diagonal meets it 2.43990254 along, facing it: got hit polygon 0 at distance 2.60256267 height 18.0570965` |
| the fallback's `u` term dropped from its distance | caught, 3 | `a column that ends above the road is clear` |
| the watertight edge function `u` taken in float | survived | |
| all three watertight edge functions taken in float | survived | |
| the dominant axis chosen by `>=`, a tie to the last axis | survived | |
| the side test's rounding bound set to 0 | survived | |

46 caught, 4 survived, none unknown. Each survivor was run over oblique
sweeps as well as the tests: through points a quarter, a half and three
quarters along every edge of every triangle of `TRACKB1003` and
`TRACKB1210` that is not degenerate, 8 units long with the edge point at
the middle, in four directions - 611352 and 529632 sweeps - each answer
compared with the current code's in polygon, triangle and distance:

- The dominant axis on a tie and a rounding bound of 0 change no answer
  there. Any axis the direction is longest along gives a valid watertight
  frame. The side test's inputs are floats, and the difference of two
  floats is exact in double while their exponents are within 28 of each
  other; then each product is its exact value rounded once, and rounding
  never reverses an order, so the side comes out with its exact sign or as
  0, which keeps the square. The bound matters only for coordinates that
  differ in scale by more than that, which no test or sweep here has.
- The edge functions in float do change answers, and only within a float
  rounding of an edge: `u` alone in float differs on 1899 and 709 of those
  sweeps, all three in float on 3941 and 2071. Decided in exact rational
  arithmetic on `TRACKB1003`, of the 1899: at 214 the current code answers
  clear where the segment really meets the triangle the variant names, at
  3 the variant names a triangle the segment misses, 1271 name the same
  triangle and differ only in the distance - by at most 2.4e-6 in the 1258
  where the segment meets it exactly - and 411 name a different triangle.
  So neither version is exact in that band - on an oblique
  sweep the shear rounds each corner in float before the edge functions are
  taken, and the exact sign the current code takes is the sign for those
  rounded corners - and no test here pins an answer inside it. Within it
  the float version shares an edge between two triangles as watertightly
  as the double one: in float too the two products of that edge are the
  same two roundings in both.

What that band leaves on its own, measured with the same sweeps: 6304 of
the 611352 on `TRACKB1003` answer clear although the segment, in exact
arithmetic, meets the closed triangle it was aimed at. Every one lies on
an edge no neighbouring surface continues - a polygon's outer edge (5815),
the (0,2) diagonal of a triangle stored in a quad slot, which is that
triangle's outer edge (454), or the vertical edge where two walls meet at a
convex corner (35) - within a few float roundings of the sheared corners:
at most 4.5e-8 world units on these sweeps, and up to 3.81e-6 on the
review's, which swept in other directions. Those are measurements, not a
bound, and not a tolerance a caller can rely on. The #39 ray meets nothing
along 6291 of them; along the other 13 it meets the degenerate triangle of
the same quad, at 0 to 3.53 units, where the segment really meets that
quad's other triangle about 4 units along - the exception under
"Triangles" above. On a column the shear is the identity and the lattices
above have no such case.

### What this does not do

- It is not a vehicle model. Issue #60 decides what a contact does to a car;
  this answers where the track is and which way it faces.
- It tests segments and columns, not a swept box or sphere. A body wider
  than a segment needs several, or a shape test this layer does not have.
- It is desktop only for now. `cmake/draw_libraries.cmake` is unchanged, so
  the Switch tree does not compile `nfsu_track_contact`; `nfsu_track`, which
  it does compile, is unchanged by this issue.
- A sweep that meets a wall only within a float rounding of the wall quad's
  own (0,2) diagonal, while the #39 arithmetic meets something farther
  along, passes through the wall; and on an oblique line the #39 ray can
  meet a degenerate triangle the contact never meets, which leaves the
  contact answering a farther surface or clear (under "Triangles" above).
  Issue #167 follows both up.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and no
game data, screenshot or exported asset may be committed.

The Release configuration goes in the sibling `build-release/`, which
`.gitignore` covers, so the shared default `build/` keeps whatever
configuration another session left in it.

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

build-release/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc

build-release/nfsu_probe track-collision X:/Games/NFSU/Tracks/TRACKB1210.lzc 783 -8.5

build-release/nfsu_probe track-collision X:/Games/NFSU/Tracks/STREAML1RB.BUN

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 904.24 -94.76 56.5 944 -74 56.5

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1210.lzc 783 -8.5

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/STREAML1RA.BUN

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 731 486 50 742 475 50

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 961.1171875 -244.921875

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 961.1171875 -244.921875 59.17578125 961.1171875 -244.921875 58.17578125

build-release/nfsu_probe track-contact X:/Games/NFSU/Tracks/TRACKB1003.lzc 754 209
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
4. The fourth command exits 0 and writes nothing to standard error. Its
   lines are the totals of "The decoded circuit" above: `collision: valid
   polygons 9958 cells 382 indices 13818 nodes 243`, `triangles: road 14634
   wall 2348 degenerate 2934 (road when |normal py| >= 0.5)`, `memory
   resident: 1433448 bytes in 5 allocations: polygons 955968 cells 12224
   indices 55272 nodes 11664 triangles 398320`, `memory stored: 354348
   bytes as the file holds the same arrays`, `memory load: packed 433352
   image 961328 peak 2429808`, `memory ceiling: 30932992 resident bytes at
   the default collision limits` and `switch plan: persistent 1433448 of
   67108864 bytes (21 per mille) extent 1433456; streaming load peak
   1394680 of 50331648 bytes (27 per mille), released`.
5. The fifth exits 0 and ends with `probe column: hit road polygon 2145
   triangle 0 distance 944.586 point (904.24, -94.76, 55.4141) normal
   (-0.0713877, 0.0141278, 0.997349) up 0.997349`: the road under the first
   point of the route.
6. The sixth exits 0 and ends with `probe segment: hit wall polygon 9768
   triangle 1 distance 14.562 point (917.148, -88.0201, 56.5) normal
   (-0.672366, -0.740219, 0) up 0`: the barrier beside it, its normal
   facing back toward the route.
7. The seventh exits 0 and ends with `probe column: hit road polygon 5221
   triangle 1 distance 1022.64 point (783, -8.5, -22.6387) normal (0,
   0.0212006, 0.999775) up 0.999775`: the second command's pin, through the
   contact layer.
8. The eighth exits 3 before reading the file: `STREAML1RA.BUN` is 92394688
   bytes, over the 67108864 a collision file may be.
9. The ninth exits 0 and ends with `probe segment: outside`: the sweep off
   a cell's corner that the review of this issue found answering clear.
10. The tenth exits 0 and ends with `probe column: hit road polygon 1980
    triangle 0 distance 940.824 point (961.117, -244.922, 59.1758) normal
    (0, 0, 1) up 1`: the seam the #39 arithmetic misses from both sides.
11. The eleventh exits 0 and ends with `probe segment: hit road polygon
    1980 triangle 0 distance 0 point (961.117, -244.922, 59.1758) normal
    (0, 0, 1) up 1`: a sweep that starts exactly on that seam, met at its
    start.
12. The twelfth exits 0 and ends with `probe column: hit road polygon 3101
    triangle 0 distance 948.755 point (754, 209, 51.245) normal (0.022816,
    0.0356822, 0.999103) up 0.999103`: a sloped polygon's outer edge the
    #39 arithmetic misses, met at the edge's own height: the edge runs from
    corner 1, (754, 205.25, 13153/256), to corner 2, (754, 209.625,
    13113/256), and 6/7 of the way along it, at pz 209, the height is
    exactly 91831/1792 = 51.2449777. The contact's own point is
    51.2449951 in float, 1.7e-5 above, the rounding of 1000 - 948.755005;
    both print as 51.245.
13. Running any of them twice gives byte-identical output.
14. The SHA-256 and size of every file named above are unchanged
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

- One static collision set is resident at a time: 0x00441ED0, entry
  0x007007E8 of the 47-entry loader table 0x00447DE3 dispatches through,
  hands the five chunks to 0x00441380 with the fixed structure 0x0077B130,
  which stores rather than appends, and 0x00441F40 zeroes it. The race
  loads it from `TRACKS\TRACKB%d.LZC` formatted with the current track
  number at 0x004497BE.
- `STREAML1RA.BUN`, `STREAML1RA_1003.BUN`, `TEX1003TRACK.BIN`,
  `GLOBALB.BUN`, the images of `GLOBALB.LZC` and `InGameB.lzc`,
  `GLOBALA.BUN`, the empty `INGAMEA.BUN` and all 120 files under `Cars/`
  walk as chunk trees with no fault and none of the five collision ids at
  any depth. The game flow loads each of the global and track-common files
  by a literal path through the bundle load at 0x004482F0.
- The closed line in `TRACKB1003.lzc`'s `0x00034149`, sampled every world
  unit, has road collision within 10 units to either side at 3316 of 3316
  samples and directly under it at 2858; the same samples find road under
  0 in `TRACKB1001` and 844 in `TRACKB1004`.
- Every circuit and drift track carries a closed line there and every
  sprint and drag track an open one.
- Over the 496280 non-degenerate retail triangles, 394766 have
  `abs(normal py)` of 0.95 or more, 96808 less than 0.05, and 909 lie
  between 0.3 and 0.7.
- Wherever the ray of issue #39 meets something on the nine lattices
  under "Region against whole", 11623141 columns in all, the contact column
  is the same polygon, triangle, distance and point. The contact meets
  something where the ray meets nothing at 17 of them, every one exactly on
  a triangle's edge.
- On every edge two road polygons share, sampled at 17 points, the #39
  arithmetic misses 4 columns of `TRACKB1003`, 4 of `TRACKB1210` and 20 of
  `TRACKB1107`, and the contact misses none.
- On oblique sweeps aimed at points of every edge of every triangle, 611352
  on `TRACKB1003` and 529632 on `TRACKB1210`, the first triangle the #39 ray
  meets is degenerate on 5076 and 729, and on those alone the contact is not
  the ray's hit; on 300000 random segments per track it never is.

## Inferred

- Group 0 is world slot 0 (`px`) and group 1 is world slot 1 (`pz`). The
  evidence is two range containments against the scenery instance
  translations and the centroid test above, not a traced instruction.
- The saturated group-2 records are walls whose vertical extent the
  exporter set to the clamp rather than to a measured height.
- The `<NNNN>` of a `TRACKB` file names the same track section the
  `STREAM*_NNNN.BUN` and `TEX####TRACK.BIN` files name, from the file
  naming alone.

- The `0x00034149` line is the race route, and its running value is a
  length along a curve through the points. Issue #61 owns both.

## Unknown

- `0x00034132` +1, +2 and +3. +0 is zero in every retail record and is
  runtime scratch, and +4 is the vertical base decoded above. Of the
  remaining three, an independent review of this branch reported that +1
  packs four 2-bit corner indices read at 0x0043FF4D and that +3 bit 0x10
  is a four-corners-versus-three flag tested at 0x0043FFDD, each holding on
  290995 of 290995 records. Neither is decoded by this parser and neither
  claim is reproduced here; they belong to a separate issue together with
  +2.
- What the `0x00034132` +6 halfword holds in the file. 29680 of the 290995
  retail records carry a value in 0x0000..0x03FF there rather than the
  0xFFFF the loader stamps over it, and that range is filled exactly and
  with no gap across the corpus, so it is an index into something
  1024 wide. A saved `TrackPolygonCollisionBodySlotPool` index is the
  reading that fits; the code that would have written it is not traced
  here, and the parser counts the two populations apart without decoding
  either.
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
- What the game does over a column the index covers and no surface
  meets, such as the median of the divided road at pz -322 on track 1003:
  `track-contact` answers clear there, which a vehicle would read as
  airborne, and whether the game has something else there - a barrier this
  layer does not see, or nothing - is not known.
- The game's own road and wall classification. The 0.5 threshold here is
  this project's; the polygon's +1..+3 bytes, which may carry a surface
  type, are not decoded.
- How the contact layer behaves on the console. It is not compiled for the
  Switch yet, and its determinism across the two compilers is not measured.
- The instruction path from the bundle load at 0x004482F0 to the loader
  table dispatch at 0x00447AC0.
- Everything about dynamic collision: `CarCollisionBody`,
  `WorldObjectCollisionBody`, `DynamicCollisionBody` and the
  `AIWallCollision` pair at 0x007000DC are named in the executable and
  decoded nowhere.
