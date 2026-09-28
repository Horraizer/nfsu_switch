# The racing line: where an opponent's path comes from

Clean-room notes on whether Need for Speed: Underground ships a racing line
for a track or computes one, what the shipped data holds, and what this
project now does with it. Everything here comes from reading a user-provided
retail installation and its executable read-only. No game data is stored in
this repository: this note holds offsets, counts, field meanings, ranges and
the marker names the executable itself spells.

**The answer: it ships one.** All 31 `Tracks/TRACKB*.lzc` files carry a
`0x00034121` chunk of route records - 1099's is empty and the other 30 are
not - polylines of points, each point with a
unit direction to each neighbour, the chord length to each neighbour, a
distance along the track path spline and a table of lateral lane offsets. The
executable's own string table at `0x007009D0` names the two record kinds
`Racing route` (kind 0, `0x006CA258`) and `Traffic route` (kind 1,
`0x006CA248`), and the debug input events `JOY_EVENT_TRACKROUTE_EDITOR_*`
(`0x006BAD7C`..`0x006BB03C`) are an in-game editor for exactly these records:
`EDIT_POSITION`, `EDIT_LEFT_WIDTH`, `EDIT_RIGHT_WIDTH`, `PREV_POINT`,
`NEXT_POINT`, `NEXT_ROUTE`. Nothing in the loader or in the code that reads
the records fits a line, smooths one or solves for one: the geometry is
authored and the runtime interpolates between authored points.

What this issue does **not** do: it does not add an opponent car to a race,
and it does not touch the player's driving model or the race session. The
last section says what wiring an opponent in would change.

Executable addresses are `speed.exe` virtual addresses; `file_offset = VA -
0x400000` for `.text`, `.rdata` and `.data`. Offsets into a data file are
decimal offsets into the decompressed image.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |

`TRACKB1003.lzc` is JDLZ; its image is 961328 bytes with SHA-256
`2B8A91ADD76C172A56A50AD0C10239F75E2102C8796A33F255F9E8C10CFFF6B9`. The 31
`TRACKB` files of this installation were all read for the survey below.
This installation's `Global/GlobalB.lzc` and `Global/GLOBALA.BUN` are 2025
modifications and nothing here reads them; the checked `Global/GLOBALB.BUN`
was searched for route data and holds none, see the next section.

## Which candidate holds the line

Three candidates were checked, not assumed.

**Records in the track stream** ([`TRACK_STREAMING.md`](TRACK_STREAMING.md),
`Tracks/STREAML1RA_1003.BUN`) - **ruled out.** The route records are not in
the stream. They are in the single-file `Tracks/TRACKB1003.lzc` image, whose
chunk walk finds `0x00034121` at 274916 with a 160276-byte payload, and the
chunk dispatcher that claims that id (0x00442DD0, `cmp eax, 0x34121` at
0x00442DD6) is reached from the `TRACKB` load, not from a section stream.

**A separate file under `Tracks/`** - **ruled out.** The directory holds
exactly four kinds of file: `STREAM*.BUN` sections, `TEX*TRACK.BIN` texture
packs, `TRACKB*.lzc` track images and, for the drag tracks, the
`DragCameras.bin` the string at `0x006C806C` names. No file carries route
geometry, and the only route loader in the executable takes a
`0x00034121` chunk.

**Attribute records in `Global/GLOBALB.BUN`** - **ruled out.** The route
loader 0x00442B70 is reached only from the `0x00034121` case of 0x00442DD0,
and the route manager at `0x0077AF20` is cleared by 0x00442BD0 on track
unload. Nothing in the global bundle feeds it. What `GLOBALB.BUN` does hold
for racing is the career event table ([`RACE_EVENTS.md`](RACE_EVENTS.md)) and
the car type records ([`VEHICLE_TUNING.md`](VEHICLE_TUNING.md)); neither
names a route.

So the line is shipped, per track, in the track's own image, beside the
track path spline [`RACE_ROUTE.md`](RACE_ROUTE.md) decodes and the zones
[`TRACK_ZONES.md`](TRACK_ZONES.md) decodes.

## The chunk and its loader

The `TRACKB1003` image's chunk walk finds, after the path container:

| Chunk | At | Payload | Read by |
| --- | ---: | ---: | --- |
| `0x00034121` | 274916 | 160276 | 0x00442B70, the routes |
| `0x00034122` | 435200 | 1728 | 0x00442DF4, 32-byte records |
| `0x00034123` | 436936 | 1104 | 0x00442E15, 368-byte records |
| `0x00034124` | 438048 | 792 | 0x00442E3C, 264-byte records |

0x00442DD0 dispatches all four. The three companions are only stored:
0x00442DF4 puts the `0x00034122` base at `0x00736150` and its count, the
payload size shifted right by 5, at `0x00736154`; 0x00442E15 divides the
`0x00034123` payload by 368 (`mul 0xB21642C9`, `shr edx, 8`) into
`0x00736158` / `0x0073615C`; 0x00442E3C divides the `0x00034124` payload by
264 (`mul 0x3E0F83E1`, `shr edx, 6`) into `0x00736160` / `0x00736164`.

0x00442B70 is the route loader. Given the chunk in `ecx` and the manager in
`edx`, it walks from `ecx + 8` to `ecx + 8 + [ecx + 4]` and for each record:

- `movsx ecx, word [eax + 0xa]` then `mov [edx + ecx*4 + 0x10], eax`
  (0x00442B80, 0x00442B84): the record's `+0x0A` is its index, and the record
  pointer goes into the manager's table. The manager is `0x0077AF20`
  (0x00442DDD), so the table is `0x0077AF30`, and the clear loop at
  0x00442BD0 walks 0x80 dwords of it, so an index is 0..127.
- `mov ecx, [eax + 0xc]` then a two-way branch (0x00442B88..0x00442BA3):
  kind 0 is appended to the list at manager `+0x00` / `+0x04`, kind 1 to the
  one at `+0x08` / `+0x0C`, and any other kind is listed nowhere.
- `movsx ecx, word [eax + 0x10]`, `imul ecx, ecx, 0x88`,
  `lea eax, [eax + ecx + 0x27c]` (0x00442BB2..0x00442BBC): the record is a
  0x27C-byte header followed by `+0x10` points of 0x88 bytes each, and a
  record's own count is the only thing that finds the next one.

`nfsu::parse_racing_lines` (`include/nfsu/racing_line.h`) walks the payload
the same way and refuses, rather than guessing about, a payload that does not
tile into whole records.

## The route header: 0x27C bytes

| Offset | Type | Meaning | Code |
| --- | --- | --- | --- |
| +0x00, +0x04 | u32 | 11 in every retail record; not read by traced code | |
| +0x08 | u8 | 1 in every retail record | |
| +0x0A | s16 | route index, 0..127 | 0x00442B80, 0x00442B84 |
| +0x0C | u32 | kind: 0 `Racing route`, 1 `Traffic route` | 0x00442B88..0x00442BA3 |
| +0x10 | s16 | point count | 0x00442BB2 |
| +0x12 | s16 | the same count again in every retail record | |
| +0x14 | u32 | 1 in every retail record | |
| +0x16, +0x17 | u8 | direction flags, written at load, zero in the file | 0x004430ED, 0x004430F2 |
| +0x18..+0x264 | | not decoded here; see *What is not decoded* | |
| +0x265..+0x26F | u8 | lane-enable slots 0..0x0A: zero in every retail record | 0x00456F54 |
| +0x270..+0x27B | u8 | lane-enable slots 0x0B..0x16, 0 or 1 | 0x00456F54 |

The two direction flags are the one thing the file does not carry and the
load computes. 0x004430B0 walks the `0x00034122` records at stride 0x20, and
for each one whose `+0x0B` is 5 (`cmp byte [ecx + 2], 5` with `ecx` at base
+ 9, 0x004430D0) it takes the route named by the record's `+0x09`
(0x004430D6, indexing `0x0077AF30` unchecked) and sets that route's `+0x16`
when the record's `+0x0D` is not 1 and its `+0x17` when it is
(0x004430E8..0x004430F2). So a type-5 record marks a route as usable in one
race direction. The same function then sorts the `0x00034122` table by each
record's leading float (the qsort comparator 0x00443090 is
`fld [ecx]` / `fsub [eax]` on the two records' `+0x00`) and rewrites every
route point's `+0x81` and `+0x82` through the sort permutation
(0x004431B0..0x004431D2), which is what makes those two bytes indices into
the sorted table rather than into the file order.

## The route point: 0x88 bytes

Cross-checked over every point of every retail track: 504 route records,
32460 points, 31956 consecutive pairs. Ranges below are over all 32460
points.

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x00, +0x04 | f32 | position, the two horizontal slots | 0x00456EC8..0x00456ED0 subtracts them from a car position; -1334.08..2392.56 and -1920.01..870.855 |
| +0x08 | f32 | height, -26.9629..93.3733; no traced instruction reads it | |
| +0x0C, +0x10 | f32 | unit direction to the **next** point | 0x00456EDC..0x00456EF1; dot with the unit chord to the next point is 1.0 for all 31956 pairs |
| +0x14, +0x18 | f32 | unit direction to the **previous** point | 0x00456EFB..0x00456F0C; dot with the unit chord to the previous point is 1.0 for all 31956 pairs |
| +0x1C | f32 | chord length to the next point; 4.0 at a route's last point | matches the chord to within 1.4e-6 over all 31956 pairs |
| +0x20 | f32 | chord length to the previous point; 4.0 at a route's first point | the same, to the same tolerance |
| +0x24, +0x28 | f32 | unknown; 0..33.589 and 0..33.2152 | see below |
| +0x2C | f32 | unknown; -0.329079..1.82691 | |
| +0x30, +0x34 | f32 | unknown, a mirrored pair; -2.70805..3.16066 and -3.16066..2.70805 | neither is the heading of +0x0C or of +0x14 |
| +0x38 | f32 | distance along the track path spline | see below |
| +0x3C + 2*i | s16 | lane offset of slot `i`, in 1/256 | 0x00456F45, 0x00456F6D, 0x00456F78 |
| +0x81, +0x82 | u8 | two indices into the sorted `0x00034122` table, 0xFF for none | 0x004431B0..0x004431D2 writes them; 0x00456EF5, 0x00456F17 test 0xFF |
| +0x83 | u8 | the index of the point's own route | 0x00456EA8, 0x004552DC index `0x0077AF30` with it; equals the record's `+0x0A` for all 32460 points |
| +0x84..+0x87 | u8 | unknown; no traced instruction reads them | |

`+0x1C` and `+0x20` were the trap in this record. Their range, 4.0 to
23.9874, is exactly the range of the chords between consecutive points
(minimum 4.0028, maximum 23.9874, mean 9.5118, median 9.9615), which is why
a first reading of them as the left and right widths the editor events name
looked plausible and is wrong. They are the chord lengths, each paired with
the unit direction beside it, with 4.0 as the sentinel at the end of a route
that has no neighbour on that side.

Where the widths the editor edits live is **unknown**. `+0x24` and `+0x28`
are the obvious candidates by range, and they do not hold: of the 86024
enabled lane offsets, 2890 fall outside `[-(+0x24), +0x28]` and 2831 outside
`[-(+0x28), +0x24]`, so neither order bounds the lanes. `+0x24` is zero at 16
points and `+0x28` at 19.

`+0x38` is the distance along the closed or open Catmull-Rom track path
spline of [`RACE_ROUTE.md`](RACE_ROUTE.md) - the same measure race progress,
laps and split boundaries use. On each of the eight circuit tracks
1001-1008 every point's value lies inside `[0, path length]` and reaches
99.90 to 100.00 percent of it; on the drag tracks 1201 and 1210 the smallest
value is negative (-43.935 and -53.834), which an open path allows, so the
field is **not** bounded below by zero in general. The per-track ranges are
in the survey table below.

## The lane table

A route point carries a table of lateral offsets from the point, and the
route header says which entries of it exist. 0x00456E90 is the one traced
reader:

- `movzx edx, byte [eax + 0x83]` then `[edx*4 + 0x77af30]` (0x00456EA8,
  0x00456EB3): it starts from the point's own route.
- Two modes, on its third argument. Mode 0 crosses the car's offset from the
  point with the direction at `+0x0C`/`+0x10` (0x00456EDC..0x00456EF1) and
  scans slots `0x0B`..`0x10` (`mov ecx, 0xb` at 0x00456EE6, `lea edi,
  [ecx + 6]` at 0x00456F2A). Mode non-zero uses `+0x14`/`+0x18`
  (0x00456EFB..0x00456F0C) and scans slots `0x11`..`0x16` (`mov ecx, 0x11`
  at 0x00456F01). Each cross product is a signed lateral distance.
- A slot exists only when the route header byte at `+0x265 + i` is non-zero
  (`cmp byte [eax + ecx + 0x265], 0` at 0x00456F54; `eax` there is the route
  pointer saved at 0x00456EBA).
- The slot's offset is the s16 at `point + 0x3C + 2 * i`
  (`lea esi, [eax + ecx*2 + 0x3c]` at 0x00456F45), shifted left by 8
  (0x00456F6D) and multiplied by `0x006CC83C` = 1.52587890625e-05 = 1/65536
  (0x00456F78), so the stored unit is **1/256** and the representable range
  is -128.0 to +127.99609375.
- It keeps the slot whose offset is nearest the car's signed lateral
  distance (0x00456F7E..0x00456F91) and returns that slot's index, less 6 in
  mode 1 (0x00456FB6), or -1 when it found none.

So the twelve slots are two groups of six, one per traversal direction,
paired with the two unit directions. The two groups are consistent with that
reading in the data: their enable patterns mirror each other, and the
commonest pattern by far, on 95 of the 410 racing routes and 69 of the 94
traffic routes, is slots 0x0B, 0x0C, 0x11 and 0x12 - two lanes in each
group. The most crowded route enables all twelve (4 racing routes do). 149
of the 410 racing routes enable no slot at all.

Enabled-slot offset ranges, over all 30 tracks with routes:

| Slot | Samples | Offset range |
| --- | ---: | --- |
| 0x0B | 16924 | -12.035 .. 19.812 |
| 0x0C | 16535 | -6.398 .. 23.688 |
| 0x0D | 6024 | -5.730 .. 18.102 |
| 0x0E | 2107 | -2.441 .. 12.395 |
| 0x0F | 1290 | 1.125 .. 12.273 |
| 0x10 | 672 | 4.129 .. 10.645 |
| 0x11 | 16963 | -12.238 .. 40.809 |
| 0x12 | 16728 | -12.375 .. 42.770 |
| 0x13 | 5780 | -5.230 .. 47.586 |
| 0x14 | 1574 | -1.855 .. 6.188 |
| 0x15 | 1070 | 1.809 .. 12.020 |
| 0x16 | 357 | 5.062 .. 9.852 |

Which sign is left and which is right is **unknown**: the cross product
0x00456E90 forms fixes a sign convention relative to the point's own
direction, and no traced code ties that convention to a side of the road.

## How dense the line is, and what each track ships

The survey below is this document's pin of record, because no test in this
repository can reach 30 retail track files. Track 1003's row is also pinned,
with a digest over every decoded field, by `nfsu_racing_line_tests`; that
test is the pin of record for 1003.

Totals: **504 route records, 410 racing and 94 traffic, over 32460 points**,
on 30 of the 31 `TRACKB` files. Track **1099's `0x00034121` chunk is present
but its payload is zero bytes**, which is why the loader's walk over it ends
at once (0x00442B75..0x00442B7E) and 1099 has no route.
A route holds between 4 and 194 points. Point spacing, over all 31956
consecutive pairs: minimum 4.0028, maximum 23.9874, mean 9.5118, median
9.9615 - so roughly one point every 10 units, an order of magnitude denser
than the 30-control-point track path spline of 1003.

Racing routes use indices 1..46, 51, 52, 60..64, 66 and 67; traffic routes
use 100..114. Twenty-two of the 30 tracks with routes carry a route of index
1.

| Track | Path length | Routes | Racing | Traffic | Points | +0x38 range | 0x34122 | 0x34123 | 0x34124 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| 1001 | 4410.5728 | 25 | 21 | 4 | 1361 | 2.590 .. 4406.111 | 66 | 0 | 4 |
| 1002 | 3965.6624 | 13 | 10 | 3 | 999 | 4.616 .. 3963.653 | 34 | 2 | 3 |
| 1003 | 3318.2698 | 17 | 14 | 3 | 1099 | 2.589 .. 3315.978 | 54 | 3 | 3 |
| 1004 | 3138.5923 | 19 | 16 | 3 | 1141 | 2.162 .. 3137.272 | 64 | 1 | 3 |
| 1005 | 3117.0320 | 19 | 16 | 3 | 1222 | 4.275 .. 3116.488 | 52 | 2 | 3 |
| 1006 | 2910.5037 | 17 | 11 | 6 | 931 | 0.294 .. 2910.393 | 42 | 5 | 6 |
| 1007 | 3127.5928 | 13 | 12 | 1 | 1379 | 0.359 .. 3127.462 | 42 | 0 | 1 |
| 1008 | 3121.3928 | 12 | 12 | 0 | 862 | 4.188 .. 3120.408 | 40 | 0 | 0 |
| 1099 | 3066.5002 | 0 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| 1102 | 4703.6807 | 22 | 19 | 3 | 1450 | 36.700 .. 4581.595 | 84 | 3 | 3 |
| 1103 | 6679.7661 | 28 | 25 | 3 | 1723 | 125.181 .. 6221.661 | 78 | 2 | 3 |
| 1104 | 7213.6284 | 35 | 28 | 7 | 2126 | 37.620 .. 7109.154 | 92 | 1 | 7 |
| 1105 | 7755.1665 | 31 | 25 | 6 | 2317 | 62.013 .. 7477.954 | 83 | 6 | 6 |
| 1106 | 7211.1050 | 37 | 32 | 5 | 2746 | 41.253 .. 7087.238 | 126 | 6 | 5 |
| 1107 | 8236.0781 | 43 | 37 | 6 | 3216 | 72.966 .. 8081.583 | 144 | 7 | 6 |
| 1108 | 6553.5996 | 33 | 29 | 4 | 2532 | 325.204 .. 6433.706 | 96 | 4 | 4 |
| 1109 | 6641.5586 | 28 | 24 | 4 | 1946 | 48.699 .. 6534.056 | 96 | 3 | 4 |
| 1201 | 2420.3271 | 24 | 16 | 8 | 763 | -43.935 .. 1907.358 | 52 | 4 | 8 |
| 1202 | 3036.9619 | 13 | 13 | 0 | 785 | 115.657 .. 2463.927 | 36 | 0 | 0 |
| 1206 | 2564.6248 | 17 | 11 | 6 | 678 | 41.937 .. 2049.954 | 34 | 0 | 6 |
| 1207 | 2183.3027 | 12 | 7 | 5 | 477 | 29.067 .. 2108.327 | 24 | 0 | 5 |
| 1210 | 2420.3271 | 18 | 10 | 8 | 772 | -53.834 .. 1907.358 | 36 | 0 | 8 |
| 1214 | 2564.6248 | 12 | 6 | 6 | 577 | 41.937 .. 2049.954 | 26 | 0 | 6 |
| 1301 | 237.0866 | 2 | 2 | 0 | 63 | 6.382 .. 234.336 | 4 | 0 | 0 |
| 1302 | 409.6458 | 2 | 2 | 0 | 103 | 7.039 .. 406.809 | 4 | 0 | 0 |
| 1303 | 598.0447 | 2 | 2 | 0 | 136 | 2.408 .. 592.178 | 4 | 0 | 0 |
| 1304 | 793.7496 | 2 | 2 | 0 | 194 | 9.749 .. 791.559 | 4 | 0 | 0 |
| 1305 | 774.2444 | 2 | 2 | 0 | 193 | 7.317 .. 769.660 | 4 | 0 | 0 |
| 1306 | 1226.5618 | 2 | 2 | 0 | 291 | 4.233 .. 1224.111 | 4 | 0 | 0 |
| 1307 | 903.8456 | 2 | 2 | 0 | 187 | 9.596 .. 901.860 | 4 | 0 | 0 |
| 1308 | 913.3228 | 2 | 2 | 0 | 191 | 9.538 .. 910.740 | 4 | 0 | 0 |

The path length column is the `+0x0C` of the track's single `0x00034148`
record ([`RACE_ROUTE.md`](RACE_ROUTE.md)), quoted so the `+0x38` range can be
read against it.

Three observations about groups of tracks, each counted rather than
generalised:

- All 8 circuit tracks (1001-1008) and all 8 drift tracks (1301-1308) keep
  every point's `+0x38` inside `[0, path length]`. The circuits reach 99.90
  to 100.00 percent of their length at the top end, the drift tracks 99.44 to
  99.88.
- Of the 6 drag tracks, 1201 and 1210 are the only two tracks anywhere whose
  smallest `+0x38` is negative. All 6 stop short at the top end, from 78.8
  percent of the path length on 1201 and 1210 to 96.6 on 1207. The 8 sprint
  tracks also stop short, from 93.1 percent on 1103 to 98.6 on 1104.
- Each of the 8 drift tracks ships exactly 2 routes, both racing, and exactly
  4 `0x00034122` records. Of the 30 tracks with routes, 10 ship no traffic
  route: 1008, 1202 and the 8 drift tracks.

## What retail does with the line

This is the part that is **named but not traced**. What is established:

- The opponent is a goal-and-action machine. The debug names are
  `AIPlan` / `AIGoal` / `AIAction` (`0x006C9668`..`0x006C9670`),
  `AICarController` (`0x006C9990`) and a roster of actions at
  `0x006C93D0`..`0x006C9640`: `AIActionJustDrive`, `AIActionPass`,
  `AIActionDraft`, `AIActionBlock`, `AIActionDriveTrafficLane`,
  `AIActionPickRouteToAvoidAnimation`, `AIActionAvoidAvoidable`,
  `AIActionNudgeAvoidable`, `AIActionObeyTrafficIntersections`,
  `AIActionAvoidCrossTraffic`, `AIActionPullOver`, `AIActionEndOfRace` -
  and, for the recovery cases issue #63 names, **`AIActionGetUnstuck`**
  (`0x006C9510`), **`AIActionAccidentRecovery`** (`0x006C9640`),
  **`AIActionIJustGotHit`** (`0x006C94FC`) and its drag-race pair
  `AIActionHitWallDragRace` / `AIActionIJustGotHitDragRace`.
- Being off the line is a three-valued state, not a boolean. The name table
  at `0x007006C4` holds, in order, `okay` (`0x006C9610`), `out of route
  heading forwards` (`0x006C95F0`) and `out of route heading backwards`
  (`0x006C95D0`), with `not set! That's probably bad!` as the fourth entry.
  The table beside it, `0x007006D4`, names a relative placement: `in front`,
  `behind`, `left side`, `right side`.
- Lateral intent is likewise an enum, `DrivingChoice` (`0x006C8DBC`), whose
  names are `movement towards line` (`0x006C8E54`), `movement towards line
  offset` (`0x006C8E34`), `movement parallel to line` (`0x006C8E18`),
  `movement towards left side of track` and `movement towards right side of
  track`. "Line" there is the route; "line offset" is a lane slot.
- The AI reads route points through the lane picker 0x00456E90 and through
  0x00442110, which turns a `0x00034122` record into a point pointer
  (`movzx eax, byte [ecx + 9]` for the route, `movzx ecx, word [ecx + 4]`
  for the point index, then `+ 0x27C + 0x88 * index`). Call sites in the AI
  region include 0x004531DA, 0x0045367C, 0x00453F32, 0x004542C0, 0x004552E3,
  0x00456546 and 0x00456EB3.
- `AICarController`'s serialised fields (`0x006C99A0`..`0x006C9A38`) name a
  longitudinal controller with a line speed: `Linespeed fraction`,
  `Linespeed gap`, `Use Linespeed`, `My Des`, `My Des Gap`, `My Speed`,
  `Pl Des Gap`, `Pl Speed Gap`, `Boost gap`, `Gas Brake`, `Boost`, `EBrake`.

What is **not** traced, and is therefore unknown here: which route a given
racer is assigned and when it changes; how `Linespeed` is computed from a
route point (no field of the point decoded above is a speed); what the
`0x00034123` and `0x00034124` records hold; and the bodies of the recovery
actions. The strings at `0x006CA268`..`0x006CA304` - `Players`, `Traffic`,
`Racers`, `Avoid Object`, `Player Always`, `Misc`, `Don't take`, `Equal`,
`Stop`, `Always`, `Slower`, `Faster`, `Must brake early`, `Don't brake
sfter` (the executable's own spelling), `Normal braking` - are almost
certainly the enums of the `0x00034122` intersection records, and that is an
inference from adjacency, not a decode.

## What this project ships

Two libraries, neither wired into the race.

`nfsu_racing_line` (`include/nfsu/racing_line.h`, `src/racing_line.cpp`)
parses the chunk. It is bounded: it takes a payload and a `RacingLineLimits`,
never throws, and turns every malformed input into a diagnostic rather than a
crash - a payload under one header, a record whose points overrun the
payload, a trailing run that cannot be a header, a negative or over-limit
point count, a `+0x12` copy that disagrees with `+0x10`, an index outside
0..127 or repeated, a kind other than 0 or 1, a lane-enable byte other than
0 or 1, a non-zero byte in the eleven slots below 0x0B, a non-finite float,
a coordinate over the limit, a point whose `+0x83` is not its record's own
index, and too many routes or points. `parse_racing_line_image` finds the
single `0x00034121` of a decompressed `TRACKB` image and refuses an image
with none or with two.

`nfsu_path_follow` (`include/nfsu/path_follow.h`, `src/path_follow.cpp`) is
the path follower, separated from vehicle actuation as issue #63 asks. It
takes a path and a vehicle state and returns steering, throttle, brake and a
reverse flag, with telemetry: the segment and fraction it projected onto, the
signed cross-track error, the distance along the path, the target point and
the lookahead that produced it, the heading error, the chosen speed and the
lane offset. It reads no clock, no file and no random device, touches nothing
in the race session, and is deterministic: identical inputs give identical
outputs, and its one optional source of randomness, a lateral jitter of the
target, is off by default and driven by the seed `reset` is given.

It is **this project's controller, not a port of retail's**: the retail
machine above is named, not decoded, so there is nothing to port yet. The
rules, all of which `nfsu_path_follow_tests` derives rather than echoes:

- The projection is a full scan of the segments, so a follower that was
  displaced or that skipped part of the path re-acquires from scratch instead
  of walking forward from a stale index.
- Following aims at the point `max(lookahead_min, lookahead_seconds *
  max(speed, 0))` further along the path, wrapping on a closed path.
- Steering is the wrapped heading error to the target over `steer_gain`,
  clamped to `max_steer`.
- The chosen speed is the smallest non-zero speed limit of the points inside
  the lookahead window, else `cruise_speed`; throttle and brake split the
  speed error over `speed_band`.
- Three recovery cases, each with a test:
  - **stalled**: `|speed|` under `stall_speed` while throttle is demanded
    accumulates a timer, and at `stall_seconds` the follower reverses for
    `reverse_seconds` with the steering mirrored. Contact multiplies the
    timer's rate by `contact_stall_scale`, so a car pinned against geometry
    gives up sooner than one merely slow; a car that is slow but rolling
    never stalls.
  - **missed checkpoint**: the follower tracks the earliest uncaptured
    checkpoint, captures one within `capture_radius`, and when its
    projection passes an uncaptured one it reports `checkpoint_missed`,
    enters `rejoining` and aims back at that checkpoint at `rejoin_speed`.
  - **collision displacement**: cross-track error over
    `rejoin_cross_track` enters `rejoining`, which aims at the projection on
    the path itself rather than at a lookahead point, and holds until the
    error falls under `rejoin_clear`.

## What is not decoded

- Route header `+0x18`..`+0x264`. It holds a float block from `+0x20`, runs
  of 0x20-filled bytes - ASCII spaces - and, on every one of the 392 route
  records of the 8 circuit and 8 sprint tracks, the same 15-character blob
  `GUHLER2KW207023` behind a byte or two that varies between records. None of
  the 112 records of the 6 drag and 8 drift tracks carries it. A string that
  is identical across tracks, absent on half of them and preceded by varying
  bytes is the mark of a buffer that was never cleared, not of a field with a
  meaning, and nothing traced here reads any of this range.
- Point `+0x24`, `+0x28`, `+0x2C`, `+0x30`, `+0x34`, `+0x84`..`+0x87`.
- The `0x00034122` record beyond `+0x00` (a float the load sorts by), `+0x04`
  (a u16 point index), `+0x09` (a route index), `+0x0B` (a type, 5 being the
  direction marker) and `+0x0D` (a direction). The `0x00034123` and
  `0x00034124` records are only stored by the traced code.
- 0x004430B0 past its sort: the remainder of the function reallocates and
  copies the `0x00034122` table and is not followed here.
- Everything under *What retail does with the line* that that section marks
  unknown.

## Wiring an opponent in: what would change, and what would confirm it

Nothing in this branch adds a car to a race, on purpose: the retail
drive-replay pins - `state-sha256`, `steps-sha256`, the step count and the
elapsed time that `tests/drive_replay_guards.cmake` and
[`CONTROLS_CAMERA.md`](CONTROLS_CAMERA.md) hold - are a bit-exact record of
one car's simulation, and an opponent that shares a contact solver, a frame
budget or a random draw with it would move them. What a follow-up would have
to do, and what evidence would settle each part:

1. **Choose a route.** Decide, per opponent and per race direction, which
   `Racing route` index it drives. Evidence: trace the assignment in retail -
   the racer construction path through the manager lists at `0x0077AF20`
   `+0x00`/`+0x04` - or, failing that, state the project's own rule and show
   that a car driven on it completes the event-1 lap chain
   [`RACE_ROUTE.md`](RACE_ROUTE.md) defines, measured with
   `nfsu::step_race_progress`.
2. **Turn a route into a `PathFollowPath`.** The point positions are the
   polyline; a lane slot chosen per point gives the lateral offset; a speed
   limit has to come from somewhere, because no decoded point field is a
   speed. Evidence: a test that builds the path from the retail 1003 route of
   a named index and pins its length and point count, against the
   `+0x1C`/`+0x20` chords, which already agree with the polyline to 1.4e-6.
3. **Actuate it.** Feed the follower's steering, throttle and brake into the
   vehicle model of [`VEHICLE.md`](VEHICLE.md) and
   [`VEHICLE_TUNING.md`](VEHICLE_TUNING.md), in its own instance. Evidence:
   a deterministic replay of N steps whose state hash is pinned in a test,
   the way the player's replay is pinned - and, critically, a rerun of the
   player's own replay pins in the same build showing them unchanged.
4. **Share the track.** Contact between two cars, and the order the two are
   stepped in, is where the player's pins are most at risk. Evidence: the
   player's four pins unchanged with an opponent present but not touching,
   and a separate pinned hash for the touching case.
5. **Recover.** The controller's three recovery cases are tested against a
   synthetic path; on a real track they also have to respect the `RESET` and
   `RESET_TO_POINT` zones of [`TRACK_ZONES.md`](TRACK_ZONES.md), which move
   a car without the controller's knowledge. Evidence: a test in which a
   reset teleports the follower and the telemetry shows it re-acquiring on
   the step after, which the full-scan projection above is designed for.

Until all five have evidence, the honest claim is the one this note makes:
the racing line is shipped data, this project can read it, and it has a
controller that can follow a path and recover on its own.
