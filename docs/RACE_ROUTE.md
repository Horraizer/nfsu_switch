# Race route: the track path, the boundaries, laps and the wrong way

Clean-room notes on what decides, in Need for Speed: Underground, where a
race starts and finishes, which boundaries a car passes on the way, when a
lap counts, and when the car is going the wrong way. Everything here comes
from reading a user-provided retail installation and its executable
read-only. No game data is stored in this repository: this note holds
offsets, counts, hashes, field meanings and the few marker names the
executable itself spells.

The answer first, for issue #61 and the first-lap target of #159 (event 1,
a two-lap forward circuit on track 1003, `DT1 - Olympic Square`,
[`RACE_EVENTS.md`](RACE_EVENTS.md)):

- **There are no checkpoint gates in space.** The game measures a car's
  progress as a percentage along one closed spline, the *track path* of
  chunk `0x80034147` in `TRACKB1003.LZC`: 30 control points, 3318.2698
  long. Every car is projected onto it every frame.
- **The start line, the split boundaries and the finish line are
  percentages on that spline**, each found once per race by projecting a
  named position marker of chunk `0x00034146` onto it. For event 1 the start
  line is at **39.343525** percent (marker `ForwardStartLine`), split 1 at
  **83.33022** (`SplitTimeBoundary1`) and split 2 at **12.659791**
  (`SplitTimeBoundary2`). A circuit's finish line is its start line.
- **A lap counts when the car's percentage passes the finish percentage
  going forward and lands within 25 percent past it.** Passing it backward
  takes the lap back. The per-car lap counter starts at -1; the grid
  (`ForwardStartingPosition`, 39.265125 percent) is behind the line, so the
  first forward crossing starts lap 1, and the race is finished when the
  highest lap reached equals the race's lap count - on event 1 at the third
  forward crossing, two laps after the first.
- **The split boundaries do not gate a lap.** Nothing in the lap update
  requires them; they only number the car's progress and time it.
- **The lap count and direction** come from the career event table (2,
  forward, [`RACE_EVENTS.md`](RACE_EVENTS.md)); the race forces one lap on
  a track whose lap-skip byte is set (a point-to-point track), and a reverse
  race measures the same spline from its other end.
- **Wrong way** is the car's heading more than 110 degrees off the path's
  direction at the car, at more than 10 mph, with the velocity also more
  than 110 degrees off; 100 degrees once the flag is on. Zones of the path
  container can suppress it or impose their own direction.

`nfsu::build_race_route` (`include/nfsu/race_route.h`) recovers all of it
from the retail files, `nfsu::step_race_progress` replays a position
sequence through it, and `nfsu_probe race-route` prints the route of an
event and, given a path file or a built-in drive, every crossing, lap,
finish and wrong-way change.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |

`TRACKB1003.lzc` is JDLZ; its image is 961328 bytes with SHA-256
`2B8A91ADD76C172A56A50AD0C10239F75E2102C8796A33F255F9E8C10CFFF6B9`. The
two tables come from `GLOBALB.BUN` and `FrontB.lzc` for the reasons
[`RACE_EVENTS.md`](RACE_EVENTS.md) gives; this installation's
`Global/GlobalB.lzc` and `Global/GLOBALA.BUN` are 2025 modifications and
nothing here reads them. Of the track table only the track number and the
lap-skip byte are used, and those are identical in the modified file.

Executable addresses are `speed.exe` virtual addresses; `file_offset = VA -
0x400000` for `.text`, `.rdata` and `.data`. Offsets into a data file are
decimal offsets into the decompressed image.

## Where the route is

A walk of the top-level chunks of the `TRACKB1003` image finds, beside the
collision chunks [`TRACK_COLLISION.md`](TRACK_COLLISION.md) decodes and the
zone container [`TRACK_ZONES.md`](TRACK_ZONES.md) decodes:

| Chunk | At | Payload | Read by |
| --- | ---: | ---: | --- |
| `0x00034146` | 264192 | 968 | 0x00441ED0, the position markers |
| `0x80034147` | 265688 | 7164 | 0x004434D0, the track path: `0x00034148` (40), `0x00034149` (620), `0x0003414A` (6480) |
| `0x00034121` | 274916 | 160276 | 0x00442B70, routes (the AI's, by inference, below) |
| `0x00034122` .. `0x00034124` | 435200 .. 438048 | 1728, 1104, 792 | 0x00442DD0, 32-, 368- and 264-byte records beside them |

The search started from the executable's strings. `ForwardStartLine`,
`ForwardFinishLine`, `BackwardStartLine`, `BackwardFinishLine`,
`SplitTimeBoundary1..3` (0x006CA4E8..0x006CA560) and
`ForwardStartingPosition` / `BackwardStartingPosition` (0x006CAE20 /
0x006CAE04) are hashed and looked up in the list `0x00034146` fills, and
`TrackPathCoordinate` (0x006CA190) names the object every car carries on
the path `0x80034147` loads. The string table 0x00700964 names the path
container's eleven zone types: `NO_WRONG_WAY`, `WRONG_WAY`, `RESET`,
`RESET_TO_POINT`, `TRAFFIC_DENSITY`, `TUNNEL`, `DRIFT_GATE`,
`DRIFT_HOT_SPOT`, `TUTORIAL`, `SHORTCUT` and
`TRACK_PATH_ZONE_STREAMER_PREDICTION`, types 0 to 10.

The routes of `0x00034121` (14 of kind 0 and 3 of kind 1 for 1003; a
0x27C-byte header plus 0x88 bytes per point, walked at 0x00442B80..
0x00442BC5) are, by inference from the string table 0x007009D0 (`Racing
route`, `Traffic route`) and the `TRACKROUTE_EDITOR` debug events that edit
points and left and right widths, the AI's routes. **They are not on the
lap chain**: the chain from a car's position to its lap count below reads
the track path and the markers only. 0x004430B0, which the track load calls with the
direction (0x0044993D..0x00449947), prepares those routes and their
`0x00034122` records for the direction; it is not traced further.

## The position markers: `0x00034146`

The loader 0x00441ED0 checks the id (0x00441ED6), rounds the payload start
up to 16 bytes (0x00441EE1 `lea eax, [ecx + 0x17]`, `and eax, -16`),
divides what is left by 48 (0x00441EF0..0x00441EF7, magic 0xAAAAAAAB and
a shift of 37) and appends each record, in file order, to the list at
0x0077B190 (0x00441F03..0x00441F1D). Two lookups walk that list:
0x0043FEB0 counts the records with a given name hash and 0x0043FEF0
returns the n-th of them, both comparing `+8` (0x0043FED0, 0x0043FF12).

| Offset | Type | Meaning | Code |
| --- | --- | --- | --- |
| +0x00, +0x04 | u32 | list links, written by the loader | 0x00441F09..0x00441F13 |
| +0x08 | u32 | name hash: -1, then `hash * 33 + byte` per byte | 0x00434677..0x00434692 inline, compared at 0x0043FED0 |
| +0x0C | u32 | a number the lookup may match | 0x004346FC `mov edx, [eax + 0xc]` |
| +0x10, +0x14 | f32 | position, the two horizontal raw slots | 0x0047CB26..0x0047CB32; 0x00434722 projects +0x10 |
| +0x18 | f32 | height | not read by the traced code |
| +0x20 | u16 | angle | 0x0047CB35, turned by 0x4000 |

Retail 1003 holds 20 markers from 264208: eight `BackwardStartingPosition`
and eight `ForwardStartingPosition` (the +0x0C number is the grid slot, 0
to 7), one `BackwardStartLine` at 264592, one `ForwardStartLine` at 265120,
one `SplitTimeBoundary1` at 265024 and one `SplitTimeBoundary2` at 265072.
The two start lines sit 0.5 apart, with angles 0x0000 and 0x8000; the
forward grid is on the side the forward race comes from and the backward
grid on the other.

The circuit tracks 1001-1008 carry both start lines and both splits; the
sprint tracks 1102-1109 those and both finish lines; the drag tracks both
start and both finish lines and no split; the drift tracks 1301-1308 only a
`ForwardStartLine`; 1099 a `ForwardStartLine` and a `ForwardFinishLine`. No
track carries a `SplitTimeBoundary3`.

## The track path: `0x80034147`

0x004434D0 checks the container id (0x004434D5) and **takes its three
children by position without reading their ids**: the first payload's size
over 40 (0x004434EA..0x004434EF), the second's over 20 (0x004434FE..
0x00443503), the third's over 180 (0x00443513..0x00443528). 0x00443450
stores the three tables and counts at +0x08..+0x1C of the manager at
0x007372B0 and indexes the third by type, eleven entries of "first record
whose type is at least t" (0x00443480..0x004434AD), which works only on a
table sorted by type. 0x00444200 clears it all when the chunk is unloaded.

### `0x00034148`: one path, 40 bytes

| Offset | Type | Meaning | Code |
| --- | --- | --- | --- |
| +0x00 | u8 | not read | |
| +0x01 | u8 | closed: the spline type is 0 (closed) when set, 2 (open) when not | 0x004432E4..0x004432F0 |
| +0x02 | s16 | first node | 0x00443380, 0x00443651 |
| +0x04 | s16 | end node, exclusive | 0x00443655 |
| +0x06 | s16 | control points, read from the first node on | 0x00443302, 0x00443364 |
| +0x08 | f32 | added to every distance | 0x00443C2E |
| +0x0C | f32 | path length; record 0's is the divisor of every fraction | 0x004432A9..0x004432AF, 0x00443C7B |
| +0x10 | f32 | multiplied into every distance | 0x00443C26 |
| +0x14, +0x1C | 2 x f32 | the first and the last node in retail; not read | |
| +0x24 | u32 | the spline object, written by the loader | 0x0044336A |

Retail 1003: one record at 265704, closed 1, nodes 0 to 30, 30 control
points, offset 0, length **3318.2698**, scale 1.

Every one of the 31 `TRACKB` files holds exactly one path. It is closed on
the circuit tracks 1001-1008, on 1099 and on the drift tracks, and open on
the sprint and drag tracks. A closed path's node table holds one node more
than the path uses (for 1003 node 30, at 266352, distance 0).

### `0x00034149`: one node, 20 bytes

| Offset | Type | Meaning | Code |
| --- | --- | --- | --- |
| +0x00, +0x04 | f32 | position | 0x0044339D..0x004433A3 copy them into the spline |
| +0x08 | s8 | the path this node belongs to | 0x00443B02, used as an index unchecked |
| +0x09 | u8 | not read | |
| +0x0A, +0x0C, +0x0E | 3 x s16 | linked nodes, -1 for none | 0x00443AB3..0x00443AFD |
| +0x10 | f32 | distance along the path | 0x00443862 (0x004437E0) |

No node of any retail track links another.

### `0x0003414A`: one zone, 180 bytes

| Offset | Type | Meaning | Code |
| --- | --- | --- | --- |
| +0x00 | u32 | type 0..10 | 0x00443492 |
| +0x04 | 2 x f32 | the point `RESET_TO_POINT` puts a car at | 0x00427F01..0x00427F07 |
| +0x0C | 2 x f32 | a direction `WRONG_WAY` measures a car against | 0x00430EA9..0x00430EB1 |
| +0x14 | f32 | a height a car at or above is not reset | 0x00427ED9..0x00427EF4 |
| +0x18 | s8 | race direction the zone applies to, -1 both | 0x00443710..0x0044371E |
| +0x1C .. +0x24 | 3 x f32 | distances along the path, turned round for a reverse race | 0x004435F3..0x00443615 |
| +0x28 .. +0x34 | 4 x f32 | box: minimum and maximum of both slots | 0x00443720..0x00443750 |
| +0x38 | s32 | polygon vertex count | 0x00443752 |
| +0x3C | 8 bytes each | polygon vertices, tested by 0x005679B0 | 0x00443756..0x0044375C |

0x004436D0 finds the first zone of a type, in table order, whose direction
filter admits the race's direction, whose box holds the point, and whose
polygon holds it by the same test [`TRACK_ZONES.md`](TRACK_ZONES.md)
recovered at 0x005679B0. Retail 1003 has 36 zones: 16 `RESET`, 2
`RESET_TO_POINT` (forward only), 1 `TRAFFIC_DENSITY`, 4 `TUNNEL`, 2
`SHORTCUT` and 11 `TRACK_PATH_ZONE_STREAMER_PREDICTION`, and no
`NO_WRONG_WAY` or `WRONG_WAY` zone. Across the 31 tracks, `WRONG_WAY` zones
occur on 1008 (one) and on the drag tracks, and `NO_WRONG_WAY` on none.

## The spline

0x004432A0 builds one spline object per path (0x004432F2 allocates 0x2C
bytes, 0x0044331E its control points) and 0x00443336 gives it the basis
matrix at 0x007376B0, which 0x0059C85D..0x0059CA3A write once and
0x0059CA44 transposes. Its rows, the weight of control point k as a
polynomial in the segment fraction u (1, u, u^2, u^3):

```text
k 0:  0   -0.5   1    -0.5
k 1:  1    0    -2.5   1.5
k 2:  0    0.5   2    -1.5
k 3:  0    0    -0.5   0.5
```

That is the **uniform Catmull-Rom** basis; 0x007376F0 holds its derivative
and 0x00737730 its second derivative. 0x0059D350 evaluates a parameter: it
wraps it into [0, count) (0x0059CE10: t / n, less its floor, times n),
splits it into a segment i and a fraction u, and weights control points
i - 1 to i + 2 (0x0059CDB0, through `D3DXVec4Transform` at 0x005AC443). A
closed spline keeps copies of its last point before its first and of its
first two after its last (0x0059CD1B..0x0059CDA5).

Every control point carries a fourth value, the **running length**.
0x0059D9F0, called with 16 (0x004433BF), sets each point's fourth value to
the length so far and adds 16 chords per segment, measured between points
evaluated at i + k/16; the copies around the seam carry the total added or
subtracted (0x0059CD8D..0x0059CDA5). The spline interpolates the fourth
value like a coordinate, and **that interpolated value is the distance
along the path**.

The data agrees exactly with that reading. For all 17 closed paths in the
installation, the running lengths of a uniform closed Catmull-Rom spline
through the nodes, measured with 16 chords per segment in float, match
every node's stored `+0x10` distance to within 0.0025 and the record's
stored length to within 0.003; for 1003 the worst node is 0.0015 off (the
probe prints this) and the spline measures 3318.2705 against the stored
3318.2698. The chord polygon through the same nodes, the obvious
alternative, is 3292.269 long and misses node 1's stored 110.2589 by 0.05
and the worst node by 25.9.

## A car on the path: `TrackPathCoordinate`

Every car carries a coordinate at +0x800 (constructed by 0x004440C0,
serialised under the name `TrackPathCoordinate` at 0x00444100):

| Offset | Meaning |
| --- | --- |
| +0x00, +0x04 | the position last given |
| +0x08 | the tracked node |
| +0x0C | the spline parameter |
| +0x10 | the fraction of the path, 0 to 1 |
| +0x14 | the distance along it |
| +0x18, +0x1C | the unit direction of the path, in race direction |

0x00443A10 updates it from a position:

1. The tracked node is kept when the new position is within 128 of the last
   one (0x00443A27..0x00443A4C, `[0x6CC7C4]` = 128.0); otherwise, or when
   there is none, 0x00443630 takes the node nearest the position over every
   path's node range.
2. 0x00443920 moves to the nearest of that node, the one before and the one
   after it, wrapping on a closed path (the first strictly nearer wins), and
   0x00443AB3..0x00443AFD to a linked node when nearer still.
3. The nearest parameter is searched from the node's own parameter when the
   node changed, otherwise from the last parameter (0x00443B12..0x00443B48),
   by 0x0059D540: at most 998 iterations (0x0059D596) of a secant step
   along the tangent - the first step uses the tangent's length as the
   rate, later ones the last two steps' - clamped to a step size that
   starts at 1 and halves whenever the step changes direction, until the
   query lies within 0.05 (pushed at 0x00443B57) of the tangent's normal.
4. The distance is the interpolated running length at that parameter times
   `+0x10` plus `+0x08` of the path (0x00443C1F..0x00443C31); the direction
   is the normalised derivative (0x00443BCA..0x00443C0D); for a reverse race
   the distance is the path length less it and the direction is turned
   round (0x00443C37..0x00443C51); the fraction is the distance over the
   path length (0x00443C7B).

A reverse race also turns round the three distances of every zone
(0x004435E0, called at 0x00449938 with `0x0078A2FC == 1`). That is all
0x004435E0 does.

The car's own update calls 0x00443A10 every frame with its position
(0x00426AFE). A coordinate more than 300 from its tracked node is searched
again from every node on a frame whose counter is a multiple of 60
(0x00426BAE..0x00426BFD: `[0x6CCB74]` = 300.0, `idiv` by 0x3C, then the
node is cleared at 0x00426BFA before the update). One more than 5000 away
sets a flag at +0x87C instead (0x00426B8C..0x00426BA2).

## The race: laps and boundaries

The race status 0x00434270 (kept at 0x0073619C) takes its lap count from
0x0078A31C, the career race's (0x004342A4..0x004342B1), and forces **one
lap** when the count is not 0 or 1 and the track's lap-skip byte 0x0078A318
is set (0x004342B9..0x004342D6). Two more flags do the same, 0x0078A344 and
0x007361A8; what sets them is not traced. [`RACE_EVENTS.md`](RACE_EVENTS.md)
records that the lap-skip byte is set on exactly the sprint and drag
tracks: it is the point-to-point flag, as another of its readers,
0x0047CAD4, confirms by choosing `ForwardFinishLine` over
`ForwardStartLine` when it is set.

0x00434810 then builds the boundaries, an array of at most five
percentages at +0x540, counted at +0x53C, with the start line's index at
+0x53E and the finish line's at +0x53F. Each comes from 0x00434640, which
takes the first marker with the name (0x0043FEF0 with index 0) - or, given
a non-zero number, the last one whose `+0x0C` equals it
(0x004346A6..0x00434711) - projects its position onto the path with a fresh
coordinate (0x00434722..0x00434773) and stores fraction times 100
(0x00434778..0x0043479D). With no such marker it stores a default instead,
100 less when above 100, or adds nothing when the default is negative
(0x004347BA..0x004347F7):

| Race | Boundaries in order (index 0 first) | Defaults |
| --- | --- | --- |
| forward | `ForwardStartLine`, `SplitTimeBoundary1`, `SplitTimeBoundary2`, `SplitTimeBoundary3`, then `ForwardFinishLine` on a point-to-point track | 5, start + 33, start + 66, none, start + 95 |
| reverse | `BackwardStartLine`, `SplitTimeBoundary3`, `SplitTimeBoundary2`, `SplitTimeBoundary1`, then `BackwardFinishLine` on a point-to-point track | 5, none, start + 33, start + 66, start + 95 |

The defaults are 0x0043482B `push 0x40a00000`, `[0x6CCD84]` = 33.0,
`[0x6CCD80]` = 66.0, `[0x6CCD7C]` = 95.0 and 0x004348B0 `push 0xbf800000`.
On a circuit the finish index is the start index (0x004349E1..0x004349FF).
The number 0x00434640 matches is 0 for the forward start line and the
splits and `[0x0078A340]` for the backward start line and the finish lines;
every retail track holds one marker of each of those names, so the number
never changes which marker is used.

**The data orders them as the code does.** On all 15 circuit
(track, direction) pairs the table races, the markers project so that,
going forward from the start line, split 1 comes before split 2 in a
forward race and split 2 before split 1 in a reverse race. The drift tracks
carry no split markers and take the defaults.

### The lap rule: 0x004338C0

Each car has a lap record of 0x13C bytes (four at +0x30 of the race
status). 0x004333C0 initialises it with the lap counter at +0x08 **-1**,
the highest lap at +0x09 0, the checkpoint number at +0x04 and its highest
at +0x06 -1 (0x0043342B..0x0043348F), and the car's current percentage at
+0x1C (0x004333DD..0x004333F6). Every frame, for every car not yet
finished (0x00435603), 0x004338C0 takes the car's percentage (+0x810 of
the car, the coordinate's fraction, times 100) and, for every boundary in
index order, asks 0x00433650 whether it was crossed:

```text
now    = wrap(percent  - boundary)       wrap: below -50 add 100, above 50 subtract 100
before = wrap(previous - boundary)       (only on a closed path, 0x00433665..0x004336D5)
if |now| > 25: no crossing               (0x004336EF..0x00433700, [0x6CCB18] = 25.0)
forward  when before < 0 and now >= 0    (0x0043370B..0x00433734)
backward when before >= 0 and now < 0    (0x00433736..0x00433757)
```

A forward crossing sets the checkpoint number to `5 * lap + boundary`
(0x0043398B..0x00433999) and raises its highest; a forward crossing of the
finish boundary first adds one to the lap counter, and when that is a new
highest lap marks a new lap (0x00433973..0x00433987). A backward crossing of
the finish boundary takes one from the lap counter (0x00433AA1..0x00433AA7).
Nothing else changes the lap counter. Last, +0x1C takes the new percentage
(0x00433DF5) and +0x14 the lap counter plus the fraction of a lap since the
finish line (0x00433D91..0x00433DAE, via 0x00434A10).

The race status then finishes every car whose highest lap equals the lap
count (0x00435A30..0x00435A4F) and does not update a finished car again.

So for event 1: the grid slot 0 marker projects to 39.265125, 0.08 short of
the line at 39.343525. The first forward crossing takes the counter from -1
to 0; the next two make it 1 and 2, the highest lap reaches 2, and the race
is finished.

## Missed and reverse crossings

Two things follow from the rule, and both are deterministic:

- **A reverse crossing** of the finish line takes a lap back, and crossing
  it forward again gives it back without counting as a new lap, because the
  highest lap does not change. A car that drives the wrong way round
  therefore drives its lap counter below -1 and has to make up every
  backward crossing before it counts a lap again. A reverse crossing of a
  split changes nothing.
- **A boundary passed by more than 25 percent in one step is not crossed at
  all.** A split skipped that way costs nothing - the lap still counts at
  the line. The finish line skipped that way costs the lap, and the car
  must go round again. The 25 percent window is what stops the wrap from
  counting a crossing on the far side of the track.

A jump can also be *read* as a crossing the car never made: the rule sees
only the two wrapped differences. A step from 16.7 to 52.8 percent moves
forward, yet split 2 at 75 goes from 41.7 past to 22.2 short of it, and
0x00433650 calls that a backward crossing of split 2
(`test_missed_checkpoint` pins it). On 1003 a jump from node 23 (81.3
percent) to node 5 (18.7) would likewise be read as a backward crossing of
the finish line and cost a lap; the pinned run below jumps to node 3 (10.5)
instead, which is more than 25 short of the line and so crosses nothing.

`nfsu::step_race_progress` reproduces both exactly, and adds one report of
its own the game has no counterpart for: `missed-forward` (or
`missed-backward`) when a step's forward travel - the wrapped difference of
the two percentages - carries the car over a boundary but lands beyond the
window. That is the "missed checkpoint" of issue #61, and it is labelled as
this project's report wherever it is printed.

## The wrong way: 0x00430E00

0x00430730 calls 0x00430E00 every frame with the last answer and stores the
new one at +0x4AC (0x0043073F..0x0043074D). With the 16-bit angle of the
path direction at the car (0x00430E17..0x00430E26, through 0x00564DB0) as
the reference:

1. It looks for a zone of type `NO_WRONG_WAY`, `WRONG_WAY`, `RESET` and
   `RESET_TO_POINT`, in that order, at the car's coordinate, filtered by
   the race direction (0x00430E4B..0x00430E98). `NO_WRONG_WAY` answers no.
   `WRONG_WAY` makes the zone's own direction the reference. A reset zone
   resets the car (0x00427ED0, unless the car is at or above the zone's
   height) and answers no if it did not.
2. The car's heading (+0x3B4) must be at least 110 degrees off the
   reference, or 100 once the flag is on: `(int)(110 * 65536.0) / 360` =
   20024 and a hysteresis of 1820 added to the difference while the flag is
   on (0x00430EE2..0x00430F4C, `[0x6B7B08]` = 110.0, `[0x6B7B10]` = 10.0).
3. Below 10 mph (`[0x6CC9F0]` = 2.237 times the speed at +0x3D0, against
   `[0x6B7B14]` = 10.0) the flag keeps its value (0x00430F52..0x00430F6E,
   0x00430FF4).
4. The velocity (+0x70, +0x74) must also be at least 110 degrees off, 100
   once on (`[0x6B7B0C]` = 110.0, 0x00430F74..0x00430FE8).
5. To turn the flag on, the car's +0x3CC must also be at least 4
   (0x00431002..0x00431010) - read as a wheel-contact count from its other
   comparisons with 1 to 4, not traced.

## Float arithmetic

The game creates its Direct3D 9 device with behaviour flags 0x40 and, on
failure, 0x20 (0x00408A57, 0x00408A83), never with
`D3DCREATE_FPU_PRESERVE` (0x2), so Direct3D leaves the thread's x87
control word in single precision: the spline, projection and lap arithmetic
above runs rounded to float. That is inference from the documented
Direct3D 9 behaviour and the flags, not a measurement; it is why this
project computes all of it in float, in the retail order of operations,
with floating-point contraction disabled for the library. The one product
retail routes through `D3DXVec4Transform` (0x005AC443) jumps through the
pointer at 0x006D4AC8, which starts out at 0x005AC436: a first-call
dispatcher that calls 0x005B065B and jumps through the pointer again, so
which implementation runs is chosen at run time and is not traced. The
order used here is that of the x87 routine that follows it, at
0x005AC449.

## What this project implements

`include/nfsu/race_route.h` and `src/race_route.cpp`, the library
`nfsu_race_route`. It links `nfsu_core` for the JDLZ decoder and
`nfsu_track_zones` for the polygon test, reads with `std::ifstream`, reads
no clock, and is classified neutral for the platform-neutrality gate.

- `parse_race_route_track` / `load_race_route_track` decode the markers and
  the path container. Refused, each with an offset: an image over the limit
  or whose top-level chunks do not tile it; not exactly one `0x80034147` or
  one `0x00034146`; a container that is not exactly the three children in
  order, or a child that is not a whole number of records (the loader floors
  the divisions; a remainder is refused rather than guessed about); an
  empty path table; a node range outside the node table, a control-point
  count other than `end - first` or below 3, a path length that is not
  finite and positive (every fraction divides by it); a node naming a path
  the table does not hold (0x00443B02 indexes with it unchecked) or a link
  that is neither -1 nor a node; a zone type above 10 or out of order (the
  index of 0x00443450 needs sorted types); a zone vertex count above 15, the
  most a record holds; anything not finite. Every rule holds for all 31
  retail bundles.
- `build_track_path_spline`, `evaluate_track_path` and
  `update_track_path_coordinate` are the spline and the coordinate.
  A track with more than one path, a linked node or an open path is refused
  by name: 0x0059CEA0 extrapolates past the ends of an open spline and is
  not implemented, so a sprint or drag route is refused rather than
  approximated.
- `race_laps` and `build_race_route` are 0x004342A4 and 0x00434810.
- `start_race_progress` and `step_race_progress` are 0x004333C0 and one
  frame of 0x00426AFE, 0x00426B63..0x00426BFD, 0x00430E00 and 0x004338C0
  in that order, the finish check of 0x00435A30 and the stop of 0x00435603.
  Events: `crossed-forward`, `crossed-backward`, `missed-forward`,
  `missed-backward`, `lap-complete`, `finished`, `wrong-way-on`,
  `wrong-way-off`, `reset-zone` and `recovered`.
- `make_race_drive` builds the synthetic drive the end-to-end runs below
  use: from the first grid marker of the race direction to the first path
  node ahead of it, then node after node in race order in straight steps of
  at most 0.5, until the finish line has been passed the lap count more
  times and the next node reached; `backward` the same number of nodes the
  other way from the node behind the grid; a skip jumps once, in one step,
  from one node to the next occurrence of another. It is a test tool, not a
  retail rule.

`nfsu_probe race-route <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>`
reads the two tables and the bundle, takes the event's first race, refuses
a bundle whose name is not the one the race loads (`TRACKB%d.LZC`,
0x006C9B38), and prints the race's laps and direction, the path record, the
spline's length and its worst disagreement with the stored node distances,
every node with its distance and percentage, every marker, every zone, and
every boundary with its source and percentage. `--path <FILE>` replays one
position per line, two numbers (the two horizontal raw slots), `#` comments
and blank lines skipped, every other malformed line refused by number;
`--drive forward|backward [--skip <node> <node>]` replays the built-in
drive. One step is 1/60 s, this project's choice; it matters only to the
wrong-way speed gate. Exit status 1 for a usage error, 3 when a table or
the bundle fails to parse, the event is missing, the bundle is another
track's, the route cannot be built or a path file is refused, 0 otherwise.
It writes nothing.

## Event 1 on track 1003

`nfsu_probe race-route` over the retail files prints, among its 104 lines:

```text
race: laps 2 (0x004342A4) direction forward point-to-point 0
path 0 @265704 closed 1 @265705 nodes 0..30 count 30 @265710 length 3318.2698 @265716 scale 1 @265720 offset 0 @265712
spline: uniform closed Catmull-Rom (0x007376B0), length 3318.2705 (0x0059D9F0, 16 chords per segment); node distances differ from it by at most 0.0014648438
node 11 @265972 x 1646.3746 y 268.57516 distance 1274.2603 @265988 percent 38.401337
node 12 @265992 x 1646.3746 y 347.427 distance 1353.2732 @266008 percent 40.782486
marker 9 @264640 ForwardStartingPosition 0x8E55C895 param 0 x 1645.6929 y 297.2468 z 55.94176 angle 0x00004000
marker 19 @265120 ForwardStartLine 0x6A56B90A param 0 x 1647.4514 y 299.9922 z 55.78148 angle 0x00000000
boundaries (0x00434810): 3 start 0 finish 0
boundary 0 ForwardStartLine marker 19 @265120 percent 39.343525 distance 1305.5243 start finish
boundary 1 SplitTimeBoundary1 marker 17 @265024 percent 83.33022 distance 2765.1216
boundary 2 SplitTimeBoundary2 marker 18 @265072 percent 12.659791 distance 420.08603
```

The start line lies between nodes 11 and 12; the lap, forward from it, runs
to split 1 near node 24, round past node 0, to split 2 near node 3 and back
to the line.

### End to end

Five runs, over the retail files, from the repository root in Git Bash
with `R` the installation root. Each prints the listing above and then the
replay; each exits 0. The hashes are of standard output with CR LF folded to
LF, measured with `| tr -d '\r' | sha256sum`, and are the same from the
default and the Release build:

```text
P="build/nfsu_probe race-route $R/Global/GLOBALB.BUN $R/FrontEnd/FrontB.lzc $R/Tracks/TRACKB1003.lzc 1"
$P                               104 lines  9970 bytes  8B09EBAEAD1C4A3F400085A1987246D8589F5BF59CE42C6E6A030BFB0EC94F7B
$P --drive forward               119 lines 11064 bytes  8D70BDE01CE5F3E6BAC5408D7B163DB50AB4B927B4DA63338C27202226843BBE
$P --drive backward              116 lines 10900 bytes  B62D7A1F0B23F16C53959C18FBB5EB3738D5B987B0F329047BDC588A4EB76106
$P --drive forward --skip 23 3   118 lines 11049 bytes  87E763A4CA5651E16D539C6C97CF54C17DDD2D62AABBCFE059E3CA76AE84A609
$P --drive forward --skip 10 20  117 lines 10995 bytes  571FC4AC09CEAC5352A08AB62E3AECCB22B3383638E6A51A2CC4775C7950D962
```

**Two laps, finished.** The drive runs from grid slot 0 through 61 nodes,
0.5 a step:

```text
replay: drive forward from marker 9 through 61 nodes, 13300 positions, 13299 steps of 1/60 s
start: x 1645.6929 y 297.2468 percent 39.265125 next boundary 0
step 6 crossed-forward boundary 0 ForwardStartLine lap 0 percent 39.35487
step 2912 crossed-forward boundary 1 SplitTimeBoundary1 lap 0 percent 83.33289
step 3630 reset-zone zone 13 lap 0 percent 94.29788
step 4846 crossed-forward boundary 2 SplitTimeBoundary2 lap 0 percent 12.670117
step 6605 crossed-forward boundary 0 ForwardStartLine lap 1 percent 39.34716
step 6605 lap-complete lap 1 percent 39.34716
step 9511 crossed-forward boundary 1 SplitTimeBoundary1 lap 1 percent 83.33289
step 10229 reset-zone zone 13 lap 1 percent 94.29788
step 11445 crossed-forward boundary 2 SplitTimeBoundary2 lap 1 percent 12.670117
step 13204 crossed-forward boundary 0 ForwardStartLine lap 2 percent 39.34716
step 13204 lap-complete lap 2 percent 39.34716
step 13204 finished lap 2 percent 39.34716
result: steps 13204 laps 2 of 2 finished yes at step 13204 lap-counter 2 highest 2 checkpoint 10 highest-checkpoint 10 next-boundary 1 wrong-way no
```

`reset-zone zone 13` is the straight step between nodes 27 and 28 clipping
`RESET` zone 13 (box 694.36 to 775.62 by -12.83 to 41.89). The spline itself
clips it too, between parameters 28.185 and 28.215: the path is the
progress measure, not a line a car drives, and a car on the road stays
outside the zone. The game would reset a car there; the replay reports it
and carries on.

**Reversed: no lap, the wrong way.** The same 61 nodes the other way from
the node behind the grid:

```text
replay: drive backward from marker 9 through 61 nodes, 13257 positions, 13256 steps of 1/60 s
step 1 wrong-way-on lap -1 percent 39.250973
step 1755 crossed-backward boundary 2 SplitTimeBoundary2 lap -1 percent 12.655221
step 3689 crossed-backward boundary 1 SplitTimeBoundary1 lap -1 percent 83.3185
step 6595 crossed-backward boundary 0 ForwardStartLine lap -2 percent 39.33236
step 13194 crossed-backward boundary 0 ForwardStartLine lap -3 percent 39.33236
result: steps 13256 laps 0 of 2 finished no lap-counter -3 highest 0 checkpoint -1 highest-checkpoint -1 next-boundary 0 wrong-way yes
```

(the `start:` line and four event lines - the second round of splits and
two `reset-zone` lines - are omitted here and included in the hash).

**A skipped split: the lap still counts.** Jumping in one step from node 23
(81.3 percent) to node 3 (10.5), 27.2 past split 1 (an excerpt):

```text
replay: drive forward from marker 9 through 52 nodes, jumping from node 23 to node 3 past 9, 11375 positions, 11374 steps of 1/60 s
step 2779 missed-forward boundary 1 SplitTimeBoundary1 lap 0 percent 10.535881
step 2921 crossed-forward boundary 2 SplitTimeBoundary2 lap 0 percent 12.670117
step 4680 crossed-forward boundary 0 ForwardStartLine lap 1 percent 39.34716
result: steps 11279 laps 2 of 2 finished yes at step 11279 lap-counter 2 highest 2 checkpoint 10 highest-checkpoint 10 next-boundary 1 wrong-way no
```

**A skipped finish line: the lap is lost.** Jumping from node 10 (36.8
percent) to node 20 (68.6), 29.3 past the line (an excerpt):

```text
replay: drive forward from marker 9 through 52 nodes, jumping from node 10 to node 20 past 9, 11200 positions, 11199 steps of 1/60 s
step 6438 missed-forward boundary 0 ForwardStartLine lap 0 percent 68.60373
step 11104 crossed-forward boundary 0 ForwardStartLine lap 1 percent 39.34716
step 11104 lap-complete lap 1 percent 39.34716
result: steps 11199 laps 1 of 2 finished no lap-counter 1 highest 1 checkpoint 5 highest-checkpoint 5 next-boundary 1 wrong-way no
```

### Every event on a closed path

Run over every event of the table with its first race's bundle, the
command builds 63 routes and refuses 53: the 53 are every sprint, time
trial and drag event, whose paths are open. `--drive forward` finishes all
63 - every circuit, lap knockout and drift event, forward and reverse,
with its own lap count - and `--drive backward` finishes none of them and
leaves every one flagged the wrong way.

## Tests

`tests/race_route_tests.cpp` (ctest `nfsu_race_route_tests`) builds
synthetic bundles - a circle of 16 nodes, radius 100, start line at 90
degrees, splits at 180 and 270 - from decimal literals, so a Debug and a
Release build write the same bytes, and checks: every decoded field at its
offset and every refusal above at the offset it names; that the spline
passes through its nodes, measures the circle's length, wraps and carries
the running length over the seam; that the coordinate measures points on,
inside and outside the circle, in both directions, keeps its node over a
small step and searches every node after a jump of more than 128; the
boundaries forward, reverse, from defaults and on a point-to-point track,
and the laps rule; two laps from the grid; a start past the line; a path
that touches the finish percentage exactly and backs off (a crossing, then
a reverse crossing taking the lap back) and one that stops one float short
(nothing); landing exactly 25 past a boundary (counted) and one float
further (missed); a skipped split and a skipped finish line; the wrong way
round; a creeping car; `NO_WRONG_WAY`, `WRONG_WAY` and `RESET` zones and
the direction filter; the recovery rule; and a load from a file, and a
fixture writer that does not replace one. `tests/race_route_guards.cmake` (ctest
`nfsu_probe_race_route_guards`) drives the command: the usage and option
refusals, three missing files, the listing and the forward drive over the
fixture pinned by SHA-256, the backward drive, both skips, a reverse event,
a missing event, another track's bundle, a skip outside the path, a path
file round a square, seven malformed path files and a missing one.

### Deliberate breaks

Each mutant was applied to `src/race_route.cpp` alone, the tests rebuilt
and run, the source restored byte for byte and touched. Twelve mutants, and
each made `nfsu_race_route_tests` print a failure line and exit 1:

| Mutant | First failure |
| --- | --- |
| window `<= 25` made `< 25` | landing exactly 25 past counts |
| forward `now >= 0` made `now > 0` | touching the line and backing off |
| backward crossing of the finish no longer takes a lap | touching the line and backing off; the lap is taken back |
| finished at `max_lap > laps` | two laps |
| recovery every 61 steps | recovery happens on a multiple of 60 |
| the 128 re-search removed | a jump across the circle is searched from every node |
| the running length not shifted at the seam | the running length is carried over the seam |
| reverse splits in forward order | a reverse race lists the splits the other way round |
| the lap-skip byte ignored | a lap-skip track runs one lap |
| `missed-forward` not reported | landing past the window is missed |
| no speed gate | a creeping car is not flagged |
| the zone direction filter ignored | a zone for the other direction does not apply |

The 128 re-search mutant survived the first version of the tests, whose
jumps all landed where the search from the old node converges anyway on a
circle; the jump straight across the circle was added for it.

## Approximations, labelled

- **Position only.** A replay has no car: the wrong-way test's heading and
  velocity are both taken as the step's direction, its speed as the step's
  length over 1/60 s, and its wheel-contact gate (+0x3CC at least 4) as
  met. A step of length zero keeps the flag.
- **The frame counter.** The recovery rule's multiple of 60 is counted on
  replay steps from the first; the phase of the game's own counter
  (0x006F08A4) against the race start is not traced. The car's +0x28 test
  beside it is taken as set.
- **Resets are reported, not done.** A `reset-zone` event marks where
  0x00427ED0 would put the car back; the height gate at zone +0x14 is not
  evaluated, the car is not moved, and the wrong-way test then runs as it
  does after a reset.
- **The start.** 0x004333C0 reads the coordinate the car already has; a
  replay starts a coordinate at the first position with a search from every
  node.
- **Angles.** 0x00564DB0 is not traced; `atan2` rounded to a 16-bit angle
  stands in for it, and only differences of two such angles are used.
- **Float.** The arithmetic is float in retail's order, which the Direct3D
  precision setting above makes the right model; x87 exponent range and
  the run-time `D3DXVec4Transform` choice can still differ in the last bit.
- **Two flags.** 0x0078A344 and 0x007361A8, which also force one lap, are
  taken as clear.

## Unknown

- An open path: 0x0059CEA0's extrapolation past the ends and 0x0059CB90's
  type-2 end points, so every point-to-point route. Not needed for a
  circuit.
- What `0x00034122` .. `0x00034124` and the `0x00034121` routes' fields
  mean, and what 0x004430B0 does with them for a reverse race.
- The zone distances at +0x1C .. +0x24 and every zone type but the four the
  wrong-way test reads.
- What the car's +0x87C flag (set more than 5000 from the tracked node)
  and the zone record's +0x14 height gate lead to.
- What sets 0x0078A340, the number matched for the backward start line and
  the finish lines.
- The marker height at +0x18 and what reads the start line's angle beyond
  0x0047CB35.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`.
