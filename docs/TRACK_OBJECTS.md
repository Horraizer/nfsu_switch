# Track scenery: the object directory, instance transforms and bounding tree

Clean-room notes on the scenery sections of Need for Speed: Underground
(PC). Everything here comes from reading a user-provided retail
installation and its executable read-only. No game data is stored in this
repository.

[`TRACK.md`](TRACK.md) decodes the geometry a track region draws. This
note decodes what decides *where* that geometry goes:

1. the `0x80034100` scenery section and its four chunks,
2. the 72-byte object directory record and the six mesh hashes it names,
3. the 48-byte instance record: its object reference, its float
   translation and its fixed-point 3x3 matrix, and
4. the 24-byte bounding tree that indexes the instances of a section.

It deliberately stops there. Which sections are resident together, what
the streaming tables do with a section number, and collision are still
not decoded, and nothing here should be read as a claim about a complete
track. What the game draws from these records, and the visibility rule
that walks the bounding tree, are decoded in
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md).

Reference samples for this document:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |
| `Tracks/STREAML1RB.BUN` | 30675616 | `2C960E19F2EFE9DD260C2E4AFFEF53205B49F666DEF0D0E9BC98DA373065FCC4` |
| `Tracks/STREAML1RC.BUN` | 28523156 | `D07DFD642B0605DC9CE2BC0274DB74A02C9B06BC4EB12115B06303A078CD4955` |
| `Tracks/STREAML1RE.BUN` | 21216288 | `18D66F11DE44B410CFCBF003CCF6E18EEB8FA6FB0CE6A6A5D8A83EE066258B55` |
| `Tracks/STREAML1RX.BUN` | 16732096 | `2F3CFBFA779076C7E0AE3AED825B821F030861F7C81FB83FF46FCF9FC1CF7AE1` |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |

Those five `STREAML1R*.BUN` files are the whole corpus. The installation
holds 476 files; reading each one's top-level chunks as (u32 id, u32
payload size) pairs from offset 0, **207** of them are a whole number of
such chunks ending exactly at end of file, and the rest are not chunk
trees at all. A `0x80034100` appears in no file outside these five, and
**593** times inside them: 423 in `RA`, 77 in `RB`, 45 in `RC`, 44 in `RE`
and 4 in `RX`.

Offsets are decimal absolute file offsets unless written as `+n`, which is
relative to the start of the named record. Executable addresses are
`speed.exe` virtual addresses; its image base is 0x00400000 and all three
sections map with `file_offset = VA - 0x400000`.

## Where the scenery lives

A track file is a flat sequence of top-level chunks, and the scenery sits
beside the geometry rather than inside it. In `STREAML1RB.BUN`:

```text
off         80 id 0x80034100 size      2960     scenery section
off       3048 id 0x00000000 size        16
off       3072 id 0x80134000 size    161168     geometry root
off     164248 id 0x00000000 size       1632
off     165888 id 0x80034100 size      11336     scenery section
off     177232 id 0x00000000 size         40
off     177280 id 0x80134000 size    173364     geometry root
```

The pairing is by position only. A section's objects name meshes by hash,
and those hashes are resolved against whatever geometry is loaded, not
against the root that happens to follow. Take a section's *adjacent root*
to be the first top-level `0x80134000` after it, stopping at the next
`0x80034100`, and take a hash to be *in* that root when it appears in the
root's `0x00134003` hash table, which in all 1231 geometry roots of these
five files is the same set as the hashes at `0x00134011 +16` of the
root's own meshes. Counting every
non-zero mesh slot, not distinct hashes, **13023 of the 19734** slots the
objects fill, and **4534 of the 9681** slot-0 entries alone, name a mesh
that is not in their own section's adjacent root, so the adjacency is a
layout convention and not a reference.

Each section holds exactly four chunks, in this order in all 593 retail
sections:

| Chunk | What it is | Record size |
| --- | --- | ---: |
| `0x00034101` | section header | 60 |
| `0x00034103` | instances | 48 |
| `0x00034102` | object directory | 72 |
| `0x00034104` | bounding tree | 24 |

The decoder does not depend on that order, but it does refuse a fifth
chunk, a second copy of any of the four, or a missing one.

## The loader, and where every record size comes from

`speed.exe` 0x004457C0 takes the chunk, checks the id and walks the
children once. Each of the four cases writes into the section object, and
the divisions in it are what fix the record sizes; none of them is
inferred from the data.

```text
004457C9  cmp  eax, 0x80034100         ; accepted by id alone
004457FE  lea  edi, [ecx + 0x17]       ; 0x00034101: align the payload
00445801  and  edi, 0xfffffff0         ;   up to 16 bytes
00445804  mov  edx, [edi + 0xc]        ; +12 of that record
00445807  mov  [edx*4 + 0x72a008], edi ;   indexes a global section table
00445860  lea  edx, [ecx + 8]          ; 0x00034102: the payload itself
00445866  mov  eax, 0x38e38e39         ;   count = size / 72
0044586B  mul  dword ptr [ecx + 4]
0044586E  shr  edx, 4
0044587D  lea  eax, [ecx + 0x17]       ; 0x00034103: align up to 16
00445894  mov  eax, 0xaaaaaaab         ;   count = (end - aligned) / 48
00445899  mul  edx
0044589B  shr  edx, 5
004458AA  lea  eax, [ecx + 8]          ; 0x00034104: the payload itself
004458B0  mov  eax, 0xaaaaaaab         ;   count = size / 24
004458B5  mul  dword ptr [ecx + 4]
004458B8  shr  edx, 4
```

`0x38E38E39 >> 36` is a division by 72 and `0xAAAAAAAB >> 37` and
`>> 36` are divisions by 48 and by 24. The header record and the instance
array are aligned up to 16 bytes from the payload; the object directory
and the bounding tree start at their payload with no alignment at all. In
every retail section the bytes skipped by an alignment are `0x11`, the
same filler the geometry chunks use, and the decoder checks them.

The 60-byte header size is not stored anywhere. It is what the runtime
needs: every retail payload ends exactly 60 bytes after the aligned
record, and the loader writes the array pointers and counts into +24
through +44 of that same record, so it cannot be shorter.

## The section header, and the section number

Only one field of the 60-byte record is set on disk. Section 303 of
`STREAML1RB.BUN`, header record at 23011360:

```text
+0  .. +11   0
+12         section number
+16 .. +59   0
```

`+12` is the section number, not a count: 0x00445807 uses it to index a
global table of sections. It is distinct inside each file, and it falls
from one section to the next at every step but one per file, so it also
orders them; 112 of the 426 distinct numbers are used by more than one
file, which the global table alone could not tell apart. 0x0044580E then
divides it by 100 and adds 0x40:

```text
0044580E  mov  edx, [edi + 0xc]
00445811  mov  eax, 0x51eb851f
00445816  imul edx                     ; edx = number / 100
00445818  sar  edx, 5
00445822  add  dl, 0x40                ; 'A' for 1xx, 'Z' for 26xx
00445825  cmp  dl, 0x5a                ; 'Z' takes the other list
```

So 303 is section `C`, 1500 is `O` and 2608 is `Z`, matching the
`STREAML1R<letter>.BUN` family. A `Z` section is linked at the head of the
list at 0x0077A978 and every other section at the tail at 0x0077A97C. All
593 retail numbers give a letter in `A` to `Z`; the decoder reports the
letter and says so when a number does not produce one.

Everything else in the record is written by the loader: +24 and +28 are
the object array and its count, +32 and +36 the instance array and its
count, +40 and +44 the bounding tree and its count, and +0 and +4 the list
links. They are zero in the file because the file is a memory image of a
structure the loader fills in.

## The object directory (`0x00034102`)

One 72-byte record per object. Object 0 of section 303, at 23013320:

```text
23013320  47 DF 48 58  C0 94 56 58  00 00 00 00  00 00 00 00
23013336  C0 94 56 58  00 00 00 00  64 00 10 00  00 00 00 00
23013352  00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
23013368  00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
23013384  00 00 00 00  8A 57 12 41
```

| Offset | Type | Field |
| --- | --- | --- |
| 0, 4, 8, 12, 16, 20 | u32 | six mesh name hashes |
| 24 | u32 | flags; read as two int16 at draw time, see below |
| 28 | u32 | flags; read as two int16 at draw time, see below |
| 32 | u32 | zero in all 9681 records |
| 36 .. 56 | u32[6] | written by the loader, zero on disk |
| 60 | u32 | zero in 9673 of 9681 records, 1 in eight |
| 64 | u32 | zero in all 9681 records |
| 68 | f32 | radius |

**The six hashes are a level-of-detail chain.** They are the same
`0x00134011 +16` mesh name hashes the geometry chunks carry, and the loop
at 0x00445900 reads exactly six of them:

```text
00445900  mov  edi, [edx + esi*4]      ; slot esi
00445903  test edi, edi                ; a zero slot resolves nothing
0044590B  cmp  edi, 0xbe43edbb         ; skipped without a lookup
00445917  cmp  edi, 0x72923c81         ; skipped without a lookup
00445A69  mov  [edx + esi*4 + 0x24], eax   ; the solid it resolved
00445A74  cmp  esi, 6
```

The retail names say the same thing. Object 0 above names `5848DF47` and
`585694C0`; over the corpus the pattern is systematic, for example

```text
7E323172  XO_TRACKBARRIERA_1A_00
7E3FE6EB  XO_TRACKBARRIERA_1Z_00
4808FADC  XO_TRACKTURNBARRIERA_1B_00
4809873D  XO_TRACKTURNBARRIERA_1C_00
```

so the letter before the trailing `_00` walks `A`, `B`, `C` down to `Z`
as the slot index rises. Slot 0 is set in all 9681 retail objects and the
decoder refuses one that is not; the rest are optional:

| Slot | +0 | +4 | +8 | +12 | +16 | +20 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Objects that fill it | 9681 | 503 | 2220 | 1168 | 3168 | 2994 |

Slots 4 and 5 are not part of the chain. When they resolve nothing, the
loader copies into them the first solid that slots 0 to 3 resolved:

```text
00445A81  mov  eax, [edx + 0x38]       ; slot 5
00445A86  jne  0x445a8f
00445A88  mov  eax, [esp + 0x14]       ;   the first of slots 0 to 3
00445A8F  mov  eax, [edx + 0x34]       ; slot 4, same fallback
```

`0xBE43EDBB` is the hash of `SKYDOME`, which appears in slot 0 of five
objects and which the loader skips rather than looks up. `0x72923C81`
names no mesh anywhere in the installation. The decoder reports both like
any other hash and never resolves them.

`+68` is finite and positive in all 9681 records, running from 0.3248766
to 10000.0. 2295 of them hold exactly 10000.0, stored as `00 40 1C 46` =
`0x461C4000`, and those 2295 are exactly the records at or above 9000,
which is the size a skybox-sized object would need; the rest scale with
the object, so the field is named `radius`. `speed.exe` 0x00445F3B adds
it to the constant 4.0 at 0x006B780C and passes the sum to the
screen-size function at 0x00445E10 as the radius around the instance
position, which is what settles the name;
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) carries that trace.

## The instance record (`0x00034103`)

One 48-byte record per placed copy. Instance 0 of section 303, at
23011440:

```text
23011440  0E 03 61 00 F3 FF  12 03 63 00 FD FF  00 00  20 00
23011456  87 AD 43 44  44 5C C3 42  DE 0F 48 C1
23011468  04 FB 9C 1F 00 00  64 E0 04 FB 00 00  00 00 00 20
23011484  00 00
```

| Offset | Type | Field |
| --- | --- | --- |
| 0 | i16[3] | world bounds, minimum |
| 6 | i16[3] | world bounds, maximum |
| 12 | u16 | index into the object directory |
| 14 | u16 | flags |
| 16 | f32[3] | translation |
| 28 | i16[9] | 3x3 matrix, row-major, 8192 is 1.0 |
| 46 | i16 | padding, zero in all 29715 records |

Decoded, that record reads:

```text
bounds   (782, 97, -13) .. (786, 99, -3)
object   0
flags    0x0020
position 782.71136, 97.68021, -12.503874
matrix   -1276  8092     0
         -8092 -1276     0
             0     0  8192
```

**The object index.** `+12` is below the section's object count in all
29715 retail instances, which is what makes it an index rather than a
hash or a flag word.

**The flags.** `+14` is the one mutable field of the record. The loader's
own fix-up pass sets and clears bit 0x10 of it:

```text
00445B16  movsx eax, word ptr [eax + 6]  ; an instance index
00445B1A  lea   eax, [eax + eax*2]       ; times 48
00445B1D  shl   eax, 4
00445B20  add   eax, [esi + 0x20]        ; the instance array
00445B23  and   word ptr [eax + 0xe], bx ; bx = 0xffef, clear bit 0x10
00445B36  or    word ptr [eax + 0xe], di ; di = 0x10, set it
```

That instruction pair is also the second independent proof of the 48-byte
stride. No other bit of the field was traced, so the decoder keeps it raw.

**The matrix is row-major and 8192 is 1.0.** Neither of those is a guess.
Dividing the nine int16 by 8192 and transforming every vertex of the mesh
the instance's object names in slot 0 reproduces the stored int16 box. An
instance can be checked when that slot-0 hash names a mesh the same file
decodes; over all five files **29674** instances qualify, and for
**29347** of them the stored box contains every transformed vertex.
Reading the same nine words column-major, that is `world[c] = t[c] + sum_r
local[r] * m[3c + r]` instead of `m[3r + c]`, and changing nothing else,
holds for only **19011** of the same 29674. The two readings also fail by
different orders of magnitude: over that whole set the worst a transformed
vertex falls outside its stored box is 48.7 units row-major and 815.7
column-major.

The stored box is also *tight*: over all five files and all three axes,
the stored minimum equals `floor` of the exact minimum in 88329 of 89022
comparisons and is one unit below it in 379 more, so the box is the
transformed mesh bounds rounded outward and nothing else.

Of the 327 instances whose box does not contain the whole mesh, 256 miss
by at most one unit, which is rounding. The remaining 71 miss by up to
48.7 units; for ten of them another mesh slot of the same object does
fit, and the other 61 are objects such as `XO_NEONSTRIPES_1B_00` and
`XO_LAMPBULBA_1A_00` whose exported box looks simply stale, the same way
`TRACK.md` records four stale `0x00134B02` bound records. The decoder
therefore *reports* this cross-check as a count and never refuses on it:
the box is the game's own conservative bound, not a rule of the format.

**The coordinate convention is the file's raw slot order, with no
swizzle.** The three translation floats, the three rows and columns of the
matrix and the three axes of the int16 box are the same three slots the
24-byte vertex uses, in the same order. That follows from the check above:
it only succeeds when the mesh vertex slot `i` is multiplied into world
slot `j` by matrix word `i * 3 + j` with no permutation anywhere.
`TrackVertex` names file slot 1 `pz` and file slot 2 `py`, so the same
names apply here, and `py` is vertical: over all 29715 instances the
translation slots span

```text
slot 0 (px)  -1543.30 ..  3204.68
slot 1 (pz)  -3069.55 ..  2132.55
slot 2 (py)   -401.59 ..   332.17
```

**The matrix is a general 3x3, not only a rotation.** 23379 of the 29715
instances have a determinant within 0.005 of 1 or -1, and 2417 have a
negative one, so mirrored placements exist. The rest carry a scale: the
determinant runs from -15.84 to 62.17 over the corpus. The decoder stores
the nine words as they are and divides by 8192 on request, and does not
assume the result is orthonormal.

## The bounding tree (`0x00034104`)

One 24-byte node per record. Node 0 of section 303, at 23015272:

```text
23015272  B4 01 3E 00 E7 FF  96 03 D2 00 8A 00  03 00
23015286  FD FF FE FF FF FF 00 00 00 00
```

| Offset | Type | Field |
| --- | --- | --- |
| 0 | i16[3] | bounds, minimum |
| 6 | i16[3] | bounds, maximum |
| 12 | u16 | entry count, 1 to 5 |
| 14 | i16[5] | entries; only the first `entry_count` are used |

An entry that is negative, `-n`, is child node `n`; a non-negative entry
is an instance index. Node 0 above reads `(436, 62, -25) .. (918, 210,
138)`, three entries, `-3 -2 -1`: the root of a tree whose three children
are nodes 3, 2 and 1. Node 0 is always the root, so the negation is
unambiguous: no entry can name node 0.

This is a tree, and the retail data proves it is a strict one. Over all
593 sections, **7588** nodes and **29715** instances:

- every node except node 0 is named by exactly one entry, and node 0 by
  none;
- every instance is named by exactly one leaf entry;
- no node names itself, and no node is reachable twice;
- fan-out is 1 to 5, with 7078 of the 7588 nodes full at 5;
- the deepest tree is 10 nodes below its root.

The boxes are consistent with the tree in the strongest sense that can be
checked: for every one of the 7588 nodes and all three axes, the node's
stored box contains the union of the stored boxes below it, and the slack
is exactly 0 or 1 unit on each side, never more (min slack: 16132 axes at
0 and 6632 at 1; max slack: 10898 at 0 and 11866 at 1). The exporter's
exact rounding rule was not reproduced, so the decoder enforces
containment, which is what a culler relies on, and does not enforce the
one-unit bound.

## Parser rules (`nfsu::parse_track_objects`)

The decoder is `include/nfsu/track_objects.h` and `src/track_objects.cpp`.
It never throws for malformed input; every problem is a `TrackIssue` with
an absolute file offset.

**Bounded by construction**, exactly as `parse_track_region` is.
`parse_track_objects(data, size, base_offset, limits)` sees the region's
bytes and is told the absolute file offset they came from, because the
16-byte alignment of the header record and of the instance array is
relative to the start of the *file*. `load_track_objects` seeks to that
offset and reads exactly that many bytes; the rest of the file is never
read and no directory is ever scanned.

Structure:

1. The region must be a whole number of top-level chunks. A region with no
   `0x80034100` is refused, as a region with no geometry root is.
2. A section must hold exactly one `0x00034101`, one `0x00034102`, one
   `0x00034103` and one `0x00034104`, in any order, and nothing else. An
   unknown child is refused rather than ignored: a decoder that ignored it
   would silently drop a chunk this project has never seen.
3. The header record starts at `align_up(payload, 16)` and the payload
   must end exactly 60 bytes later: all 593 retail payloads do, and a
   trailing byte is refused for the same reason an unknown chunk id is.
   The object directory and the bounding tree start at their payload. The
   instance array starts at `align_up(payload, 16)` and the bytes to the
   end of the payload must be a whole number of 48-byte records. Every
   skipped alignment byte must be `0x11`; positions are computed, never
   found by scanning for `0x11`.
4. Each of the three arrays must hold at least one record. A section that
   placed nothing would still have to carry a tree over nothing.
5. Every object must name a mesh in slot 0, and its radius must be finite.
6. Every instance's object index must be inside its own section's
   directory, its three translation floats must be finite, its box must
   not be empty on any axis, and the padding word after its matrix must be
   zero.
7. Every bounding node must declare 1 to 5 entries and hold a box that is
   not empty on any axis.
8. The tree must be a tree: walking it from node 0 must reach every node
   exactly once and every instance exactly once, no entry may name a node
   out of range, and the walk must stay inside the depth limit. A cycle, a
   shared subtree, an orphan node and an instance in two nodes all fail
   this rule, and so does an instance in none.
9. Every node's stored box must contain the stored boxes of everything
   below it.
10. Limits bound the region bytes, the chunk count, the section count, the
    records per section, the objects, instances and bounding nodes of the
    whole region, and the tree depth. The per-section limits default to
    what the format can address: an instance names its object with a u16
    and a tree entry is an int16. The three region totals are added up as
    each array is about to be decoded, so they bound what a caller has to
    hold; they count the arrays of a section a later rule refuses, and the
    section that crosses one is refused while the region walk goes on.

A section that fails any rule is kept with `valid == false` and no decoded
record, exactly as an invalid track mesh is, and it contributes nothing to
the cost statistics.

Every multiplication and addition that sizes a buffer is overflow-checked.

## Mesh references (`nfsu::resolve_track_objects`)

`resolve_track_objects` takes a `TrackObjectReport` and a
`TrackRegionReport` from `TRACK.md` and binds every non-zero mesh slot to
a valid mesh of that region, producing one `TrackObjectBinding` per used
slot in section, object and slot order. A hash no mesh claims is reported
unresolved rather than treated as an error, because a section routinely
names meshes that arrive with another region; when two valid meshes claim
one hash the lowest index wins, so a run is reproducible.

It also runs the box cross-check described above for every instance whose
slot-0 mesh resolved, and reports how many stored boxes contain their
mesh, plus the world bounds over every checked instance.

The two reports need not come from the same bytes. Passing an empty region
resolves nothing and checks nothing rather than failing.

## The decoded section

```text
file        Tracks/STREAML1RB.BUN
section     offset 23011344  size 4168
geometry    offset 23015540  size 178268
```

The geometry range is the region `TRACK.md` decodes; the scenery range is
the `0x80034100` chunk that precedes it.

| | |
| --- | ---: |
| Sections | 1, valid 1 |
| Section number | 303, letter C |
| Objects | 27 |
| Instances | 39 |
| Bounding nodes | 10, deepest 5 |
| Used mesh slots | 61 |
| Distinct mesh hashes | 44 |
| Source object bytes | 1944 |
| Source instance bytes | 1872 |
| Source bound node bytes | 240 |

Resolved against the geometry region beside it, 28 of the 61 used slots
find a mesh and 33 do not, because they name meshes of other sections. 18
instances can be checked and 17 of them store a box that contains their
mesh.

```text
instance boxes  px 436 .. 917   pz  62 .. 209   py -24 .. 137
world bounds    px 436.15 .. 864.808   pz 69.7045 .. 208.307
                py -23.554 .. 136.623
```

### A second region, for the multi-section path

```text
file        Tracks/STREAML1RB.BUN
offset      0
size        177232
```

Two sections, 2608 and 2606, both letter `Z`, with 11 and 15 objects, 38
and 188 instances and 10 and 47 bounding nodes; 14 chunks walked, 26
objects, 226 instances, 57 nodes, 51 used mesh slots and 35 distinct mesh
hashes. It also covers the unrelated top-level chunks and the geometry
root between the two sections, which are listed and otherwise ignored.

### Whole-corpus check

The decoder is meant for one region at a time, but running it once over
every scenery section of the five large files is the strongest check that
its rules match the data. Over all **593** sections:

| | |
| --- | ---: |
| Sections decoded | 593 of 593 |
| Sections refused | 0 |
| Objects | 9681 |
| Instances | 29715 |
| Bounding nodes | 7588 |
| Used mesh slots | 19734 |

Not one retail section raises an issue, and the totals match an
independent walk of the same bytes.

Pointed at a geometry root instead, the decoder refuses everything, which
is the intended behaviour: `nfsu_probe track-objects
Tracks/STREAML1RB.BUN 23015540 178268` reports 0 sections and the single
issue `no scenery section chunk 0x80034100 in the region`.

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

build-release/nfsu_probe track-objects X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23011344 4168 23015540 178268

build-release/nfsu_probe track-objects X:/Games/NFSU/Tracks/STREAML1RB.BUN 0 177232

build-release/nfsu_probe track-objects X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23015540 178268
```

Expected:

1. The first command exits 0 and writes nothing to standard error. Its
   summary lines are exactly the numbers in the table above: `sections: 1
   valid 1 chunks 5`, a section line reading `number 303 letter C` with
   `objects 27 instances 39 nodes 10 tree depth 5`, `cost: region bytes
   4168 chunks 5 sections 1 objects 27 instances 39 nodes 10`, `slots:
   total 162 empty 101 resolved 28 unresolved 33` and `instances: checked
   18 bounds contain the mesh 17`.
2. The second command exits 0, writes nothing to standard error, and
   prints `sections: 2 valid 2 chunks 14` followed by section lines for
   2608 and 2606, and `cost: region bytes 177232 chunks 14 sections 2
   objects 26 instances 226 nodes 57`.
3. The third command exits 3 and prints the single issue `no scenery
   section chunk 0x80034100 in the region` on standard error.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged
   afterwards.

To find another section, list the container first and pick one
`0x80034100` line; the size to pass is its payload size plus the 8-byte
header:

```text
build/nfsu_probe chunks X:/Games/NFSU/Tracks/STREAML1RB.BUN 100000
```

## The intermittent segmentation fault, and what it was

Issue #122. `nfsu_track_object_tests` was seen to exit with a segmentation
fault twice, by two sessions that could not explain it: once on a reviewer's
first full `ctest` run after a fresh configure on a documentation-only
branch, and once on a mutant confined to a `src/main.cpp` handler this test
does not link. Both passed every recheck. It is fixed, and it was two
defects rather than one.

`test_load_reads_only_the_region` writes a real file and loads a region out
of it. It named that file `nfsu_track_objects_region.bin` under the system
temporary directory, with nothing in the name to make it this process's own,
wrote it with `std::ios::trunc` and removed it at the end. CLAUDE.md says
several sessions are routinely live on this machine, each with its own build
directory, so two copies of this binary overlap whenever two of them run
`ctest` at the same time - and each one truncates and deletes the other's
fixture. A copy that called `load_track_objects` while another was between
its open and its close read a file that was missing, empty or half written.

That alone would only have failed. What crashed was the second defect: the
letter assertion after the load subscripted `report.sections[0]` with no
guard, and a refused load returns a report with issues and no section at
all. `sections[0]` on an empty vector is an access violation.

Both halves are needed, and both were measured separately.

| Experiment | Before | After |
| --- | --- | --- |
| The target alone, sequentially, default build | 200 of 200 passed | - |
| Six concurrent copies, 20 rounds | 120 of 120 passed | - |
| Two `ctest` runs, two build directories, `--repeat until-fail:6000` | one side crashed on its 18th run, the other ran 6000 clean | 6000 and 6000, no failure |
| The fixture written and removed by another process throughout | 33 segmentation faults in 40 runs | 48 reported failures, 0 crashes in 60 |

The last row measures the guard on its own: it puts the shared name back on
top of the fixed source, so the race still happens and the crash does not.
The third row is the whole fix. The first two rows are why this took two
sightings to pin - the fault cannot occur in the one condition under which a
flaky target is normally investigated, which is running it alone.

The fixture name now carries the process id, the way `tests/arg_path_tests.cpp`
has since issue #87. Every container subscript in the test file was then
enumerated mechanically rather than by grep, over 235 bracket sites; four
more unguarded ones were found and guarded, and two `static_assert`s now pin
the fixture's fixed-length arrays to the parser constants that index them.
The suite went from 197 checks to 247.

Issue #124 covers the seven other test files that still build fixtures at a
fixed name. They do not collide with each other - all eight names differ - so
each one collides only with another copy of itself, and each can be fixed on
its own. Whether any of the seven crashes rather than fails depends on
whether it, too, subscripts a container after a load that can fail.

## Observed

- The five large `STREAML1R*.BUN` files hold all 593 `0x80034100` chunks
  in the installation, one before each geometry root. No other file holds
  one: of the 476 files installed, 207 walk as top-level chunks to end of
  file and none of the other 202 carries a `0x80034100`.
- A section holds exactly `0x00034101`, `0x00034103`, `0x00034102` and
  `0x00034104`, in that order, in all 593 cases.
- The header record is 60 bytes at a 16-byte aligned offset and carries
  only its section number at +12; every other field is written at load
  time.
- The object record is 72 bytes and names up to six mesh hashes; slot 0 is
  always set and slots 4 and 5 fall back to the first of slots 0 to 3.
- The instance record is 48 bytes: an int16 box, a u16 object index, a u16
  flags word, a float translation, a row-major int16 3x3 with 8192 as 1.0,
  and two padding bytes that are zero in all 29715 records.
- The instance transform reproduces the stored box for 29347 of the 29674
  instances whose slot-0 mesh is in the same file; the column-major
  reading of the same words manages 19011.
- A section number is distinct inside its file but not across the five,
  and the matrix is a general 3x3 whose determinant is not always +-1.
- The bounding tree is a strict tree of fan-out 1 to 5 and depth at most
  10, covering every instance exactly once, whose node boxes contain their
  children with 0 or 1 unit of slack.
- Instance translations, matrices and boxes use the same raw slot order as
  the 24-byte vertex; slot 2 (`py`) is vertical.

## Inferred

- The six mesh hashes are a level-of-detail chain for slots 0 to 3, from
  the `_1A_`, `_1B_`, `_1C_` and `_1Z_` suffixes of the names they resolve
  to and from the order the loader reads them in.
- `0x00034102 +68` is a bounding radius: it is finite and positive
  everywhere, and 2295 objects hold exactly 10000.0.
- The 61 instances whose box is too small by more than a unit are stale
  exporter output, the same way four `0x00134B02` bound records are.
- The instance box is the transformed mesh bounds rounded outward, and the
  node box is the union of the boxes below it rounded outward again.

## Unknown

- Every bit of `0x00034102 +24` and `+28` as flag words. The draw path
  reads those eight bytes as four int16 and compares each against a
  screen-size level to pick a mesh slot (0x00445FC0), so this note keeps
  them raw and [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) carries that
  reading and its census.
- Every bit of the instance flags at `+14` except bit 0x10, which the
  loader sets and clears, and the low byte, which 0x00445EE6 exclusive-ORs
  with 0x60 and masks against a render byte to skip an instance.
- `0x00034102 +60`, which is 1 in eight of 9681 records and zero in the
  rest.
- What the four int16 at `0x00034102 +24` and the two at `+32` mean
  exactly. 0x00445FC0 compares each against the screen-size level of
  0x00445E10 and takes the first slot that passes, so they behave like
  per-slot thresholds; the projection scale that level depends on was not
  recovered.
- What `0x72923C81` names. It is skipped by the loader beside the
  `SKYDOME` hash but matches no mesh in the installation.
- The exporter's exact rounding rule for a node box, which leaves 0 or 1
  unit of slack over the boxes below it.
- Everything about streaming: which sections are resident together, and
  what puts a section into the global table at 0x0072A008 in the first
  place. The draw path indexes that table by section number
  (0x004465E9) and always draws numbers 2600 to 2699, which are the `Z`
  sections the loader links at the head of the list; see
  [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md).
- Collision. Visibility is decoded in
  [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md), which reads the bounding
  tree this note decodes.
