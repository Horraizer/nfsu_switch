# Track visibility zones: the records inside `0x80034150`

Clean-room notes on the data that decides which sections of a Need for
Speed: Underground track are drawn from a camera position. Everything here
comes from reading a user-provided retail installation and its executable
read-only. No game data is stored in this repository.

[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) recovered the culling path
from `speed.exe` and named the one thing it could not reach: the record
layout inside chunk `0x80034150`. This note is that layout. It also
corrects two claims that note made about the path into it.

Reference samples for this document:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Tracks/TRACKB1301.lzc` | 66720 | `6DEF53D7FF9D9246ADDCA8DFE266CF5A42A08B083232A3BC2B8ECBB5C29B5F19` |

The installation holds 476 files; the SHA-256 over the sorted manifest of
name, size and hash is
`79F69180AFBF86DDA3A7000023C276676565A04BBF955AD4534ADFDC34EE58FD`, before
and after the work in this note. The corpus every count below is measured
over is all 31 `Tracks/*.lzc`, decompressed with `nfsu::jdlz_decompress`,
and the five `STREAML1R*.BUN` containers beside them.

Executable addresses are `speed.exe` virtual addresses; the image base is
0x00400000 and `.text` maps with `file_offset = VA - 0x400000`.

## The path from the container to the five lists

`0x00446AB0` is not the loader. It is the reset callback, and all it does
is point five list heads at themselves:

```text
00446AB4  cmp  dword ptr [eax], 0x80034150
00446ABA  jne  0x446b0d                      ; not ours
00446ABC  mov  eax, 0x77a930                 ; five empty circular lists
00446AC1  mov  dword ptr [0x77a930], eax     ;   list 1 head
00446AC6  mov  dword ptr [0x77a934], eax     ;   list 1 tail
00446ACB  mov  eax, 0x77a938                 ;   list 2 at +8
00446ADA  mov  eax, 0x77a940                 ;   list 3 at +0x10
00446AE9  mov  eax, 0x77a950                 ;   list 4 at +0x20
00446AF8  mov  eax, 0x77a958                 ;   list 5 at +0x28
```

The loader is `0x00446DB0`, and it walks the container's children:

```text
00446DB5  cmp  ecx, 0x80034150
00446DC6  mov  ecx, dword ptr [eax + 4]      ; payload size
00446DCA  lea  ecx, [ecx + eax + 8]          ;   end
00446DCE  lea  edi, [eax + 8]                ;   first child
00446DF2  cmp  eax, 0x34151                  ; the zone polygons
00446E6D  cmp  eax, 0x34152                  ; the zones
00446F0C  cmp  eax, 0x34153
00446F47  cmp  eax, 0x34154
```

Each case divides the child's payload size by a record size and links every
record into one of the five lists. **Every record size in this note is one
of those strides, not a size inferred from the data:**

| Chunk | Stride | Goes to |
| --- | ---: | --- |
| `0x00034151` | `0x00446E5F add ecx, 0x8c` = 140 | list 1 at `0x0077A930`, or list 2 at `0x0077A938` |
| `0x00034152` | `0x00446EF3 add ecx, 0x94` = 148 | list 3 at `0x0077A940` |
| `0x00034153` | `0x00446F3D add eax, 0x110` = 272 | list 4 at `0x0077A950` |
| `0x00034154` | `0x00446F6D add eax, 0x20` = 32 | list 5 at `0x0077A958` |

Every record of every chunk opens with two pointer words the loader
overwrites the moment it links the record (`0x00446E43` and `0x00446E4B`
write `+0`, `0x00446E48` writes `+4`), so what the file holds there is read
by nothing. In all 4278 records of all four chunks across all 31 tracks,
both words read 11.

### The first correction

`TRACK_VISIBILITY.md` records `0x00446FD0` as walking "a linked list whose
head is at 0x0077A930". It does not. `0x0044641A` loads `0x0077A930` into
`eax` and `0x00446FD9` reads `[edi + 0x10]`:

```text
00446FD7  mov  edi, eax                      ; 0x0077A930
00446FD9  mov  esi, dword ptr [edi + 0x10]   ; the head of list 3
00446FDC  add  edi, 0x10                     ; the sentinel it stops on
```

So the zone lookup scans **list 3, at `0x0077A940`, which is the
`0x00034152` list**, and the `0x00034151` records it reaches are the ones a
`0x00034152` record points at. The rest of that note's description of
`0x00446FD0` holds: `[esi + 0xc]` is the record it tests, `+0xa` of that
record is the vertex count and `+0xc` the vertices.

## `0x00034151`: one zone polygon, 140 bytes

```text
+0   u32   a link word the loader overwrites (11 in every retail record)
+4   u32   the same
+8   i16   a section number, the key
+0xa i16   the vertex count
+0xc       16 vertices of two float32 each, 128 bytes
```

`+8` is read at `0x00446E20` `movsx eax, word ptr [ecx + 8]` to sort the
record into one of two lists, and again at `0x00446EB0` as the key a
`0x00034152` record is matched against. `+0xa` is `0x00446FE6`
`movsx ecx, word ptr [eax + 0xa]` and `+0xc` is `0x00446FEA`
`add eax, 0xc`, strided 8 bytes by `0x005679E3` `lea edx, [eax*8]`.

`12 + 16 * 8` is exactly 140, so the record holds room for sixteen
vertices and no more. No instruction bounds the count. Over the corpus:
**2110 polygons, 17096 vertices, counts from 4 to 16** and never outside
that range, with every slot at or past the count reading zero in all 2110.
The decoder keeps those slots rather than dropping them.

The two lists `0x00034151` is split between are chosen at `0x00446E2E` and
`0x00446E36`: the key's `number / 100` must give a letter in `'A'` to
`'T'` and its `number % 100` must lie in 1 to 9. All 2110 retail keys pass,
so all 2110 join the first list; the second is empty in every retail track.
Keys run 101 to 1309 and their letters are `A` to `H` and `J` to `M`.

## `0x00034152`: one zone, 148 bytes

```text
+0    u32  a link word the loader overwrites
+4    u32  the same
+8    i16  a section number, the key, matched against a polygon's +8
+0xa  i16  unread by the executable; zero in all 2110 retail records
+0xc  u32  where the loader writes the polygon's address; zero in the file
+0x10 i32  the section count
+0x14      64 int16 section numbers, 128 bytes
```

`+0x10` is `0x0044649E` `mov eax, dword ptr [ebp + 0x10]` and `+0x14` is
`0x004464A7` `lea ecx, [ebp + 0x14]`. `0x00446C10` `cmp edx, 0x40` refuses
to append a 65th number and `(148 - 20) / 2` is the same 64, so the two
agree. `+8` is read again at `0x004464F5`
`movsx eax, word ptr [ebp + 8]` as the identity of the zone the camera is
in, stored to `0x00700960` when it changes.

`+0xc` is filled by the loader, which searches the two `0x00034151` lists
for a record whose `+8` equals this one's:

```text
00446EB0  movsx ebx, word ptr [ecx + 8]      ; this zone's key
00446EB9  mov   edx, dword ptr [ebp]         ; walk list 1
00446EC0  movsx esi, word ptr [edx + 8]
00446EC4  cmp   esi, ebx
00446EC6  je    0x446eec                     ; found
00446ECE  mov   edx, dword ptr [ebp + 8]     ; then list 2
00446EEC  mov   dword ptr [ecx + 0xc], edx   ; store it, or zero
```

A zone whose key matches nothing gets a zero there, and `0x00446FE6` would
then read the vertex count through a null pointer. `parse_track_zones`
refuses such an image instead.

Over the corpus: **2110 zones, 49964 section numbers, counts from 4 to 59**
and never above 64, with every slot at or past the count reading zero. No
two polygons share a key and no two zones share one, in any of the 31
tracks, and **every one of the 2110 zone keys names exactly one polygon**.

Three further properties of every one of the 2110 lists: it holds its own
key, it holds its key plus ten, and it is in strictly ascending order with
no repeat. Entries run 100 to 2524 and **none of the 49964 is in the
2600..2699 band** the zone arm adds unconditionally.

## `0x00034153` and `0x00034154`: two more section lists

Both are looked up by a whole dword at `+8`: `0x004446B0`
`cmp dword ptr [ecx + 8], edx` for `0x00034153` and `0x00444BA4`
`cmp dword ptr [eax + 8], ecx` for `0x00034154`. `0x00034154`'s consumer
then reads its contents outright:

```text
00444BB4  mov  edx, dword ptr [eax + 0xc]    ; the count
00444BB7  test edx, edx
00444BBF  lea  esi, [eax + 0x10]             ; the array
00444BC6  rep movsd                          ; edx / 2 dwords
00444BCD  rep movsw                          ; and the odd word
```

`(edx >> 1) * 4 + (edx & 1) * 2` is `edx * 2` bytes, so the count is a
count of int16 entries at `+0x10`. That is the proof for `0x00034154`.
`0x00034153`'s consumer at `0x004446A1` only stores the record pointer into
`[eax + 0x598]`, and the instruction that walks its array was not traced;
`+0xc` and `+0x10` are the reading this note uses for it, from the shared
record shape and from the data, and that is an inference rather than a
reading of the code.

`(272 - 16) / 2` is 128 entries and `(32 - 16) / 2` is 8. Over the corpus
the `0x00034153` counts run 5 to 37 and the `0x00034154` count is 1 in all
30 records that exist. Three tracks ship an empty `0x00034153` and one
ships an empty `0x00034154`; an empty payload is not an error.

**All 58 of those records key on the track number.** `TRACKB1001.lzc` gives
1001, `TRACKB1301.lzc` gives 1301, and so on for all 58, with no exception.
`TRACKB1301.lzc`'s `0x00034154` holds the single number 2603, which is in
the always-drawn band.

Neither chunk is on the path a camera position takes, so neither changes a
visible set here. They are decoded because they share the container.

## The polygon test (`0x005679B0`)

The function takes a point in `eax` and a vertex array and a count on the
stack, and returns 0 or 1 in `al`. It reads two of the point's floats
(`0x005679B3` and `0x005679B5`) and two per vertex, strided 8 bytes. The
loop is unrolled four ways with a tail loop for the remainder; the
`cmp ebx, 4` at `0x005679C5` is the unrolling guard and not a rejection, so
a triangle is tested through the tail. A count of zero or less returns
without reading a vertex (`0x00567BC8`).

For each vertex `i`, with `j` the previous index and `count - 1` for
`i == 0`:

```text
005679FD  fld   dword ptr [ecx]              ; v[i].s1
005679FF  fcomp dword ptr [esp + 0x10]       ;   against p.s1
00567A08  jp    0x567a1b                     ;   greater, or unordered
00567A0A  fld   dword ptr [esp + 0x10]       ; p.s1
00567A12  fcomp dword ptr [esi]              ;   against v[j].s1
00567A19  jnp   0x567a3b                     ;   strictly below: crossing
```

`test ah, 0x41` followed by `jp` leaves the arm when the loaded value is
greater than the point or the pair is unordered, which is exactly
`!(a <= b)`; `test ah, 5` followed by `jnp` takes the arm when the point is
strictly below. So the first band is `v[i].s1 <= p.s1 && p.s1 < v[j].s1`,
and `0x00567A1B` to `0x00567A39` is the same pair the other way round:
`v[j].s1 <= p.s1 && p.s1 < v[i].s1`. **Both are half open at both ends.**

```text
00567A3B  fld   dword ptr [esp + 0x10]       ; p.s1
00567A3F  fsub  dword ptr [ecx]              ;   - v[i].s1
00567A41  fld   dword ptr [edx + ebp]        ; v[j].s0
00567A44  fsub  dword ptr [ecx - 4]          ;   - v[i].s0
00567A47  fmulp st(1)
00567A49  fld   dword ptr [esi]              ; v[j].s1
00567A4B  fsub  dword ptr [ecx]              ;   - v[i].s1
00567A4D  fdivp st(1)
00567A4F  fadd  dword ptr [ecx - 4]          ;   + v[i].s0
00567A52  fcomp dword ptr [esp + 0x14]       ; against p.s0
00567A5B  jne   0x567a6a                     ; skip unless strictly greater
00567A5D  mov   al, byte ptr [esp + 0x28]
00567A63  sete  al                           ; flip the parity
```

So the rule is:

```text
inside = false
for i in 0 .. count - 1:
    j = (i == 0) ? count - 1 : i - 1
    if (v[i].s1 <= p.s1 && p.s1 < v[j].s1) or (v[j].s1 <= p.s1 && p.s1 < v[i].s1):
        x = v[i].s0 + (p.s1 - v[i].s1) * (v[j].s0 - v[i].s0) / (v[j].s1 - v[i].s1)
        if x > p.s0:
            inside = not inside
return inside
```

Four edge cases the code decides rather than leaves open:

- **A horizontal edge is never taken.** Both its endpoints share an `s1`
  and neither half-open band can hold the point, so the division at
  `0x00567A4D` never divides by zero. Nothing else guards it.
- **A point level with a vertex crosses exactly one of the two edges that
  meet there**, because the band is closed at the lower end and open at the
  upper.
- **A point exactly on an edge does not flip on that edge**, because
  `0x00567A5B` requires the crossing to be strictly greater. A square from
  (0, 0) to (10, 10) therefore reports a point at (0, 5) inside, through
  its right edge alone, and a point at (10, 5) outside.
- **An empty polygon is never hit**, and neither is a one- or two-vertex
  one: no edge of either can produce two crossings of opposite parity.

Retail evaluates all of this in x87 80-bit registers and this project in
float32. The two can differ for a point within rounding of an edge. No
other difference is intended: `nfsu::point_in_track_zone_polygon` keeps the
comparisons, their direction and the association of the arithmetic, and
`src/track_zones.cpp` is compiled with floating-point contraction disabled
so that a fused multiply-add cannot move the boundary.

### Do the polygons overlap?

It matters, because `0x0044700B` moves a hit to the front of the list, so
retail's scan order changes as the camera moves while a decoder's does not.
Two polygons over one point would make the two answer differently after the
first hit.

**Measured over all 31 tracks, on a 90 by 90 grid of each track's own
vertex box: 251100 samples, 212046 in exactly one polygon, 39054 in none,
and none in two.** Measured again on real geometry - 16904 scenery instance
positions sampled from eight tracks' sections - 9930 landed in their own
section's polygon only, 5793 in a different polygon only, 1181 in none, and
**none in more than one**. The most-recently-used reordering therefore
cannot change the answer on this data.

That is a property of the retail data and not of the format. Nothing in the
records forbids an overlap and no instruction checks for one.

The 39054 grid samples in no polygon are not a defect either: the zones
cover the drivable area and not the bounding box around it, and a camera
outside every zone is the case `0x00446424` `test ebp, ebp` exists for.

### What the key means

The key associates a polygon with a section, but it is not that section's
footprint. Of the 1998 polygons whose key names a section the five
`STREAML1R*.BUN` containers hold, the section's own instance positions land
inside the polygon 52.3% of the time (235334 of 450319), against 0.12% for
a polygon of a different key (192 of 157296). On the per-point measurement
above, a section's instance is inside its own polygon in 58.7% of samples
and inside a neighbour's in 34.3%.

**Observed:** the association is specific, by three orders of magnitude
against the control. **Inferred:** the key names the section whose ground
the polygon covers, not the extent of that section's geometry. A third of a
section's scenery standing under a neighbouring zone is exactly why a zone
lists twelve to fifty-nine sections and not one.

## The section list (`0x00446400`), in full

### The second correction

`TRACK_VISIBILITY.md` shows the no-zone arm reading section numbers and
does not say where they come from. They come from a linked list of loaded
sections, not from the number table:

```text
0044642C  mov  edi, dword ptr [0x77a978]     ; the loaded-section list
00446448  mov  ecx, dword ptr [edi + 0xc]    ; this section's number
0044644E  mov  ebx, 0x64
00446453  idiv ebx
00446459  cmp  edx, 0xa
0044645C  jge  0x446469                      ; number % 100 >= 10: drop it
0044645E  cmp  esi, dword ptr [esp + 0x20]
00446462  jge  0x446469                      ; the buffer is full
00446464  mov  word ptr [ebx + esi*2], cx
```

`0x0077A978` is the list `TRACK_OBJECTS.md` records the loader linking at
`0x00445831`, so the no-zone answer is in load order and carries whatever
is resident, with no table lookup at all. The zone arm does look sections
up in the table at `0x0072A008`, because it starts from numbers rather than
from records.

### The cap

`0x004465B0` reserves `0x100` bytes and `0x004465C1` pushes `0x80`, so the
buffer holds **128 int16 section numbers**. Both arms check it before every
append (`0x0044645E` in the no-zone arm, `0x0044648B` and `0x004464C2` in
the zone arm). It can bite: the 2600..2699 band alone is 100 numbers and a
zone list may add 59 more.

### The section-number arithmetic, explicitly

`idiv` by 100 on the signed number gives a quotient and a remainder:

```text
index     = number / 100        truncating, signed
sub_index = number % 100
letter    = 0x40 + index        so index 1 is 'A' and index 26 is 'Z'
```

- **No zone:** a loaded section is drawn when `sub_index < 10`
  (`0x00446459 cmp edx, 0xa`). So 100..109 are drawn and 110..199 are not,
  2600..2609 are drawn and 2610..2699 are not.
- **A zone:** every number from 2600 to 2699 that the table at `0x0072A008`
  holds, ascending (`0x00446473 mov eax, 0xa28` and `0x00446497
  cmp eax, 0xa8c`), **then** every non-negative number of the zone's own
  list that the table holds, in the list's own order. Those 100 numbers are
  index 26, letter `'Z'`, and `STREAML1RX.BUN` is the container that holds
  them - its four sections are numbered 2601 to 2604.
- The two halves do not interact and are not de-duplicated. Retail appends
  each with a plain store (`0x00446491` and `0x004464C8`), so a zone list
  naming a band number would put it in the buffer twice and the walker at
  `0x004465E0` would draw it twice. No retail zone list does, but the
  decoder reproduces the behaviour rather than tidying it.
- A negative number in a zone list is skipped (`0x004464B3
  test eax, eax` / `jl`). A number the table does not hold is skipped in
  both arms.

The letter range `'A'` to `'T'` at `0x00446BF0` and `0x00446BF4` bounds two
runtime list-editing helpers (`0x00446BB0` and `0x00446C90`), which add or
remove the `+10` sibling of a number already in a zone's list. **It is not
a filter on what is drawn:** the band is letter `'Z'` and is outside that
range, and `0x00446400` never looks at a letter at all.

## What this project implements

`include/nfsu/track_zones.h` and `src/track_zones.cpp`. The container is
opened with the existing `nfsu::jdlz_decompress`, exactly as
`src/track_collision.cpp` opens the same files; no second decompressor
exists here.

- `parse_track_zones` decodes one image and `load_track_zones` reads and
  decompresses one file. Seven rules make a report not valid, each with an
  absolute offset into the decompressed image: the chunk walk must land
  exactly on the end, there must be exactly one container, every child must
  be one of the four ids and appear once, the polygon and zone chunks must
  be present and a whole number of records, every count must lie inside its
  record, no two polygons or two zones may share a key, and every zone key
  must name a polygon.
- `point_in_track_zone_polygon` is `0x005679B0`.
- `find_track_zone` is `0x00446FD0`, without the most-recently-used
  reordering, which the overlap measurement above shows cannot change the
  answer on retail data.
- `track_zone_residency` fills the `TrackResidency` of
  [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) from one decoded zone, so
  `TrackResidencyRule::zone_list` now carries the game's own list rather
  than a caller's guess.
- `track_zone_draw_list` and `track_no_zone_draw_list` are the two arms of
  `0x00446400`, in retail's order, with the 128 cap.

**The fallback is untouched.** `TrackResidencyRule::every_section` is still
the default and `TrackCullPolicy::draw_everything` still consults nothing,
and a report still names the policy every section was culled with, so a run
that consulted no zone data cannot pass for one that did.

### Deterministic tests, and the mutations they catch

`tests/track_zone_tests.cpp` builds every fixture out of bytes and parses
them, so a fixture goes through the same rules a retail image does. The
record sizes and offsets are written as literals in the test and compared
against the header's constants in one place, so a mutated constant cannot
move the fixture and the expectation together.

The suite pins, among other things: the two half-open bands at a vertex and
along a flat edge; the strict toggle, through the asymmetry between a point
on the left edge of a square (inside) and one on the right edge (outside);
the parity, through a point far to the left of a square being outside
because it crosses both vertical edges; a concave L whose notch is outside;
the same regular polygon at every vertex count from 3 to 16, which crosses
the unrolled loop's boundary four times; and that a third float of the
point never reaches the test.

It also pins the integration: a written region of three sections numbered
100, 110 and 2601, where `every_section` keeps all three, `no_zone` keeps
100 and 2601, and a zone list naming only 100 keeps 100 and 2601 through
the band. Under that zone residency the tree walk and the flat instance
test select the same instances and the fallback covers both.

**A mutation pass written with the implementation, 61 edits.** 53 ran, 8
would not compile because a mutated record size or capacity breaks a
`static_assert` that ties it to the offsets beside it. 48 of the 53 were
caught from the start. The other ten are worth recording, because six of
them were equivalent and four were real:

| Survivor | Why the suite could not see it |
| --- | --- |
| both of the rising band's comparisons reversed | a square is symmetric about a scanline through any of its vertices, so the falling clause covered for the rising one at every boundary the suite tested |
| the negative-entry skip in `track_zone_draw_list` deleted | a dropped skip reinterprets -7 as 65529, which the residency filter then dropped anyway |
| both validity guards deleted | the only report the suite could hand them that was not valid also held no resolvable zone |

What closed them: a diamond sampled at the level of its left vertex and to
the left of everything, where the correct answer is two crossings and
outside and opening the closed end drops one of them; a triangle sampled
exactly at its apex, where the correct answer takes neither edge and
closing the open end takes one; a residency that holds 65529, so the skip
has to do its own work; and a `TrackZoneReport` built field by field with
`valid` left false, then the same report marked valid, so the three
refusals cannot hold for the wrong reason.

The five that still survive are equivalent, and are listed rather than
papered over: taking the next vertex instead of the previous enumerates
the same undirected edges under a rule symmetric in the pair; the
zero-count guard in the polygon test is redundant with a loop that does
not run; the signed half of the section-count check is subsumed by the
`std::size_t` cast in its other half; the resolution's `break` cannot
matter because a duplicate key is refused first; and the draw list's cap
check gives the same answer before or after an append that adds at most
one entry per turn.

A pass written by the author of a suite is weak evidence about that
suite - it tends to mutate what the tests already name - which is why the
second pass was written by someone who had not read this list.

**An independent pass, 201 mutations.** Written by an adversarial reviewer
working from the diff and the repository in its own detached worktree,
without seeing either list above. 127 were caught, 12 were refused at
compile time by the `static_assert` block that ties the record sizes to
the offsets beside them, and **62 survived**; the reviewer discarded 10 of
those as provably equivalent. Re-measured against the head this note
describes, 12 more were already dead - the four the pass above closed and
the eight the file-loading tests closed - and 39 were live.

**Four of the 39 were defects in the code, not gaps in the suite.**

| Defect | What it was |
| --- | --- |
| `parse_track_zones` did not bound `base_offset` | every offset it reports is that base plus something, and `size_` is bounded while the base is a caller's number, so a base near the top of the range made the whole report wrong instead of refused |
| three count guards read as a parallel pair of rules and were not | the signed half of each was dead, because the `std::size_t` cast in the other half already rejected every negative |
| `kTrackZoneStoredLinkValue` was referenced nowhere outside a test comparing it to a literal | a check of that shape cannot fail for any reason worth knowing |
| `valid` and `ok()` were presented as two predicates | they are one, because every rule that raises an issue returns immediately |

The first two are fixed. The third now drives
`TrackZoneCost::records_with_unexpected_link`, which is zero for all 4278
retail records and is counted rather than refused, because nothing reads
those words. The fourth is documented where it lives rather than
restructured.

The other 35 were the suite's, and they fell into groups it had missed
systematically:

| Group | Count | What the suite could not see |
| --- | ---: | --- |
| inclusive boundaries | 11 | every count bound and every limit was tested from one side only: a 16-vertex polygon, a 64-section zone and a full section list being *rejected* were all invisible |
| the 2600 end of the band | 5 | no fixture made 2600 itself resident, which hid both ends of the band scan, both cap checks and the two default `cap` arguments |
| guards and index bounds | 7 | `report.valid` on all three entry points, and a zone index of exactly `size()` |
| fields nothing read back | 7 | `TrackIssue::offset`, the region's offset and size, `cost.region_bytes`, and a section-list record's index, offset and link words |
| fixtures too weak to bite | 5 | the duplicate-key sort was never load-bearing because both colliding records were adjacent; the two child-chunk refusals were asserted against *empty* chunks; the payload-overrun fixture overran by 0xFFFF bytes and not by one |
| values no fixture reached | 5 | no section number above 255, no polygon whose unused vertex slots were non-zero, no zone whose unused section slots named a resident section, and a single call passing three refusing arguments at once |

**Two of the 39 remain, and are named rather than papered over.** The index
bound inside `find_track_zone`'s loop cannot be exercised without reading
`report.polygons[size()]`, which is undefined, so no assertion can depend
on what it produces; and `valid` being `issues.empty()` is equivalent by
construction. Both are commented where they live.

The suite went from 235 checks to 354. Of the 262 mutations written across
the two passes, all but those two are caught, and the ten the reviewer
itself discarded as equivalent are not counted as catches.

### The retail differential

Over four `STREAML1R*.BUN` containers, eight `Tracks/TRACKB*.lzc` zone
sets, twelve multi-section regions, cameras placed on real scenery instance
positions, two view directions and two far planes - **1920 camera runs** -
the zone-driven `retail_tree` and `flat_instance_test` selected exactly the
same instances every time: **0 mismatches, 0 failed runs, 0 residency
disagreements**, and `draw_everything` covered the tree's answer in all
1920. 1193 of those runs had a non-empty visible set, of 87 distinct sizes
from 1 to 556; the other 727 saw nothing, and an agreement on two empty
sets proves nothing, so both numbers are reported. The zone lookup picked a
real zone in 1376 runs and fell through to the no-zone rule in 544, so both
arms of `0x00446400` are exercised.

Each side wrote a named file that was deleted by exact name first, and a
run that produced no file, printed to standard error or exited non-zero was
counted as a failure rather than an agreement. The first attempt at this
differential reported 1872 of 1872 runs failing with exit
`0xC0000139`, because the toolchain was not on the subprocess `PATH`; had
the comparison asserted only that the two sides matched, all 1872 would
have "agreed" on nothing at all.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and no
game data, screenshot or exported asset may be committed.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc

build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc 0 -1200

build/nfsu_probe track-zones X:/Games/NFSU/Tracks/TRACKB1301.lzc 9000 9000
```

Expected:

1. The first exits 0, writes nothing to standard error, and prints
   `zones: valid chunks 44 container children 4 polygons 42 zones 42
   section lists 2`, `cost: polygon vertices 220 zone sections 736`,
   `section list 0x00034153 record 0 key 1301 count 5: 701 702 703 704
   705` and `section list 0x00034154 record 0 key 1301 count 1: 2603`.
2. The second prints all of that and then `picked: zone 0`, whose line
   reads `zone 0 key 701 polygon 0 sections 12` with the section list
   `700 701 702 703 704 705 710 711 712 713 714 715`.
3. The third prints `picked: none; retail falls back to the no-zone rule`
   and still exits 0: a camera outside every zone is a retail case, not a
   failure.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged
   afterwards.

To drive the culler from a zone instead of from a caller's list:

```text
build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RA.BUN \
    <offset> <size> <eye> <target> --far 1500 \
    --zones X:/Games/NFSU/Tracks/TRACKB1301.lzc --zone-point <slot0> <slot1>
```

The eye and the target are in the world frame of
[`FORMATS.md`](FORMATS.md); the zone point is in the raw slot order,
because that is the frame the polygons are stored in.

## Observed

- The reset callback at `0x00446AB0` points five list heads at themselves
  at `0x0077A930`, `0x0077A938`, `0x0077A940`, `0x0077A950` and
  `0x0077A958`. The loader is `0x00446DB0`, and it strides the four child
  chunks by 140, 148, 272 and 32 bytes.
- `0x00446FD0` is called with `0x0077A930` and reads `[eax + 0x10]`, so the
  zone lookup scans the `0x00034152` list at `0x0077A940`.
- A `0x00034151` record is `link, link, i16 key, i16 count, 16 vertex
  pairs`. A `0x00034152` record is `link, link, i16 key, i16 unread, u32
  polygon pointer, i32 count, 64 int16`. `0x00034153` and `0x00034154` are
  `link, link, u32 key, i32 count, int16 array` of 128 and 8 entries.
- Over all 31 tracks: 2110 polygons with 17096 vertices and counts 4 to 16;
  2110 zones with 49964 section numbers and counts 4 to 59; 4278 records
  in all, every one carrying 11 in both link words; every slot past every
  count zero; no duplicate key in either chunk; every zone key naming
  exactly one polygon.
- Every one of the 2110 zone lists holds its own key, holds its key plus
  ten, and is strictly ascending. Its entries run 100 to 2524 and none is
  in 2600..2699.
- All 58 `0x00034153` and `0x00034154` records key on the number in their
  file's name.
- The polygon test is a crossing count with both vertical bands half open
  and a strict toggle, on the point's first two floats and the vertex's
  first two, strided 8 bytes.
- No sampled point lies in two polygons of one track: 251100 grid samples
  and 16904 instance positions, with 0 double hits in either.
- The no-zone arm walks the loaded-section list at `0x0077A978`, not the
  number table; the zone arm looks numbers up in the table at
  `0x0072A008`. The buffer holds 128 numbers and both arms check that
  before every append.
- A section's own instances lie inside the polygon keyed on it 52.3% of the
  time and inside a polygon keyed on another section 0.12% of the time.

## Inferred

- `+0xc` and `+0x10` of a `0x00034153` record are a count and an int16
  array, from the record shape `0x00034154`'s consumer proves and from the
  data. The instruction that walks that array was not traced.
- The key of a `0x00034151` or `0x00034152` record names the section whose
  ground the zone covers rather than the extent of its geometry, from the
  52.3% against 0.12% contrast above and from a third of a section's
  scenery standing under a neighbouring zone.
- `0x00034154` is a per-track addition to the always-drawn set: its one
  entry is a 2600..2699 number in all 30 records that exist, and that band
  is what the zone arm draws unconditionally. No traced instruction joins
  the two.
- The two link words holding 11 in every record are an exporter's
  placeholder rather than data. Nothing reads them.

## Unknown

- What `+0xa` of a `0x00034152` record is. It is zero in all 2110 records
  and no instruction reads it.
- What `0x00034153` is for: which code reads the record `0x004446C9`
  stores at `[eax + 0x598]`, and what its 5 to 37 section numbers select.
- What `0x00034154`'s single number is used for downstream of the copy at
  `0x00444BC6`.
- Why a zone list always holds its key plus ten. The `+10` sibling is what
  the two list-editing helpers at `0x00446BB0` and `0x00446C90` add and
  remove, but what makes the game call them was not traced.
- Whether the letter range `'A'` to `'T'` bounds track sections generally
  or only those two helpers. The band at 2600..2699 is letter `'Z'` and is
  outside it.
- Everything about streaming, unchanged from
  [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md): what decides that a section
  is in the table at `0x0072A008`, or in the loaded list at `0x0077A978`,
  in the first place. This note decodes which sections a zone asks for, not
  which ones are there to be asked for.
- The render mask byte and the projection scale that gate 1 and gate 3 of
  the per-instance path need. Those are still unrecovered, so
  `cull_track_region`'s default set is still a superset of the retail one
  on both gates, zone data or no zone data.
