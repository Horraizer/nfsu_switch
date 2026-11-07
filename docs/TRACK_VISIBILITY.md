# Track visibility: the section list, the frustum test and the bounding-tree walk

Clean-room notes on what Need for Speed: Underground (PC) draws from a
camera position. Everything here comes from reading a user-provided retail
installation and its executable read-only. No game data is stored in this
repository.

[`TRACK.md`](TRACK.md) decodes the geometry a section draws.
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry goes.
This note decodes what decides whether it is drawn at all:

1. which sections of the track are drawn, and the visibility zones that
   decide it,
2. the six-plane box test the engine applies to every candidate,
3. the walk over the `0x00034104` bounding tree that drives it, and
4. the three per-instance gates that follow.

It is written from the executable. Every rule below names the instruction
it came from, and the three that could **not** be recovered - the zone
data, a render mask byte and a projection scale - are called out, together
with what this project does instead.

Reference samples for this document:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Tracks/STREAML1RB.BUN` | 30675616 | `2C960E19F2EFE9DD260C2E4AFFEF53205B49F666DEF0D0E9BC98DA373065FCC4` |
| `Tracks/TRACKB1301.lzc` | 66720 | `6DEF53D7FF9D9246ADDCA8DFE266CF5A42A08B083232A3BC2B8ECBB5C29B5F19` |

The installation holds 476 files. All five `STREAML1R*.BUN` containers and
their 593 `0x80034100` sections are the corpus every count below is
measured over, exactly as in `TRACK_OBJECTS.md`.

Executable addresses are `speed.exe` virtual addresses; the image base is
0x00400000 and all three sections map with `file_offset = VA - 0x400000`.

## The path from a camera to a draw

```text
0x004465B0   the scenery draw entry
  0x00446400   pick the section numbers to draw   -> the zone layer
  0x00446140   walk one section's bounding tree   -> the frustum prune
    0x0040DB20   classify one box against six planes
    0x00445EC0   one instance: mask, box, size, mesh slot
      0x0040DB20   the same box test again
      0x00445E10   the screen-size level
```

0x004465B0 reserves a 0x100-byte buffer and passes 0x80 as its cap
(0x004465C1 `push 0x80`), so at most 128 section numbers are drawn in one
pass. It then walks them:

```text
004465E0  movsx eax, word ptr [esp + esi*2 + 0xc]   ; a section number
004465E5  test eax, eax
004465E7  jl   0x446602                             ; negative: skip
004465E9  mov  eax, dword ptr [eax*4 + 0x72a008]    ; the global section table
004465F0  test eax, eax
004465F2  je   0x446602                             ; not resident: skip
004465F4  mov  ecx, dword ptr [eax + 0x28]          ; the bounding tree
004465F7  test ecx, ecx
004465F9  je   0x446602                             ; no tree: skip
004465FD  call 0x446140                             ; walk it
```

`0x0072A008` is the global section table `TRACK_OBJECTS.md` records the
loader writing at 0x00445807, and `+0x28` is the bounding-tree pointer the
loader writes at 0x004458AD. So the whole of scenery visibility is: pick
section numbers, then walk each resident section's tree.

## The section list, and the visibility zones

0x00446400 fills that buffer. It has two paths.

```text
00446405  mov  ecx, dword ptr [eax + 0x80]   ; the render context
0044640B  mov  eax, dword ptr [ecx + 0x40]
00446415  add  eax, 0x40                     ; a world point
00446419  push eax
0044641A  mov  eax, 0x77a930                 ; the zone list
0044641F  call 0x446fd0                      ; which zone holds the point?
00446424  mov  ebp, eax
0044642A  jne  0x446473                      ; a zone: take its list
```

0x00446FD0 is handed 0x0077A930 and reads `[eax + 0x10]`, so the list it
walks is the one whose head is at **0x0077A940**, which
[`TRACK_ZONES.md`](TRACK_ZONES.md) shows the loader filling with
`0x00034152` records. An earlier draft of this note said 0x0077A930; the
`add edi, 0x10` at 0x00446FDC is what settles it. For each entry it reads
a record at `+0xc`, a count at `record +0xa` and a vertex array at
`record +0xc`, and calls 0x005679B0:

```text
00446FE3  mov   eax, dword ptr [esi + 0xc]
00446FE6  movsx ecx, word ptr [eax + 0xa]    ; vertex count
00446FEA  add   eax, 0xc                     ; the vertices
00446FED  push  ecx
00446FEE  push  eax
00446FEF  mov   eax, ebx                     ; the point
00446FF1  call  0x5679b0
```

0x005679B0 is a two-dimensional crossing test: it takes two of the point's
three floats at `+0` and `+4`, strides the vertex array by 8 bytes
(0x005679E3 `lea edx, [eax*8]`) and compares and interpolates a pair of
floats per vertex (0x005679FD to 0x00567A52). **A zone is a polygon in the
ground plane, not a box and not a portal.** A hit moves the entry to the
front of the list (0x0044700B to 0x0044701F), so the list is
most-recently-used ordered and the test is a linear scan.

**With a zone**, the drawn list is the 26xx band plus the zone's own
sections:

```text
00446473  mov eax, 0xa28                          ; 2600
00446480  mov ecx, dword ptr [eax*4 + 0x72a008]
00446491  mov word ptr [ebx + esi*2], ax          ; append if resident
00446497  cmp eax, 0xa8c                          ; 2700
0044649C  jl  0x446480
0044649E  mov eax, dword ptr [ebp + 0x10]         ; the zone's entry count
004464A7  lea ecx, [ebp + 0x14]                   ; its int16 section numbers
004464B0  movsx eax, word ptr [ecx]
004464B5  jl  0x4464cd                            ; negative: skip
004464B7  mov edi, dword ptr [eax*4 + 0x72a008]
004464C0  je  0x4464cd                            ; not resident: skip
004464C8  mov word ptr [ebx + esi*2], ax
```

Sections 2600 to 2699 are always drawn when a zone is active. Those are
exactly the letter `Z` sections of `TRACK_OBJECTS.md` - `0x40 + 2600/100`
is `Z` - and they are the same ones the loader links at the *head* of the
section list at 0x00445830 rather than the tail. The head link and the
unconditional band are two halves of one decision: the 26xx sections are
the always-resident set.

**With no zone**, the drawn list is every loaded section whose number
modulo 100 is below 10. The numbers come from the loaded-section list at
0x0077A978 (0x0044642C `mov edi, dword ptr [0x77a978]`), which is the list
`TRACK_OBJECTS.md` records the loader linking at 0x00445831, and not from
the number table the zone arm uses:

```text
00446448  mov  ecx, dword ptr [edi + 0xc]    ; the section number
0044644E  mov  ebx, 0x64
00446453  idiv ebx
00446459  cmp  edx, 0xa                      ; number % 100 >= 10 ?
0044645C  jge  0x446469                      ; then drop it
00446464  mov  word ptr [ebx + esi*2], cx
```

Two other places read a section number the same way and confirm the
split: 0x00446BE7 and 0x00446CA8 both divide by 100, require the letter to
be `A` to `T` (0x00446BF0 `cmp al, 0x41` and 0x00446BF4 `cmp al, 0x54`),
and treat `number % 100` below 10 and at or above 10 as two different
classes. A zone record holds at most 64 numbers (0x00446C10
`cmp edx, 0x40`).

### Where the zone data is, and why the fallback exists

The zone list is built by the chunk handler at 0x00446AB0:

```text
00446AB4  cmp dword ptr [eax], 0x80034150
00446ABA  jne 0x446b0d                       ; not ours
00446ABC  mov eax, 0x77a930                  ; reset the zone list heads
```

**`0x80034150` is the visibility-zone chunk id.** It does not appear at
the top level of any file in the installation. Scanning all 476 files: 19
are zero bytes, 207 of the other 457 walk as `(u32 id, u32 payload size)`
chunks from offset 0 to end of file - the same 207 `TRACK_OBJECTS.md`
counts - and none of the 207 carries a `0x80034150` at its top level. A
raw 32-bit `0x80034150` occurs 21 times in 19 files, and 17 of those files
are `Tracks/TRACKB*.lzc`; the other two files are `speed.exe`, whose two
occurrences are both instruction immediates - 0x00446AB4
`cmp dword ptr [eax], 0x80034150` and 0x00446DB5 `cmp ecx, 0x80034150` -
and a file under `Movies/`.

Those files are compressed. 33 of the 34 `.lzc` files in the installation
begin with the four bytes `4A 44 4C 5A`, that is `JDLZ`, and
`Tracks/TRACKB1301.lzc` reads

```text
00000000  4A 44 4C 5A 02 10 00 00 90 30 04 00 A0 04 01 00
```

which is not a chunk tree as it lies on disk: read as `(id, size)` the
second chunk claims 2936711967 bytes. The one file that does not begin
with `JDLZ` is `Global/GlobalB.lzc`, which is not a track file.

**The container is not the obstacle.** This project already decodes JDLZ:
`nfsu::jdlz_decompress`, declared in `include/nfsu/texture.h`, with the
16-byte header and the two flag registers written out in
[`FORMATS.md`](FORMATS.md). Run over the track containers, that algorithm
expands all 31 `Tracks/*.lzc` files to a whole number of top-level chunks
ending exactly at end of file, and **every one of the 31 carries exactly
one top-level `0x80034150`**, from 1216 to 29424 bytes, holding the same
four children in the same order in all 31:

| Chunk | Size in `TRACKB1301.lzc` |
| --- | ---: |
| `0x00034151` | 5880 |
| `0x00034152` | 6216 |
| `0x00034153` | 272 |
| `0x00034154` | 32 |

So the census above reports none only because it walks files as they lie
on disk and does not decompress them.

**The layout inside that chunk is now decoded**, in
[`TRACK_ZONES.md`](TRACK_ZONES.md) and
`include/nfsu/track_zones.h`. `0x00034151` is the polygon list, 140 bytes
a record, and `0x00034152` is the zone list, 148 bytes a record, carrying
the `+0x10` count and the `+0x14` int16 array this note traced the reads
of; the other two are further section lists keyed on the track number.
Each record size is the stride of the loader's own loop at 0x00446DB0.
`nfsu::track_zone_residency` fills a `TrackResidency` from one decoded
zone, so the rule below is now driven by the game's data rather than by a
caller's guess.

None of that deletes the fallback, and it was never a consolation. It is
the correct answer for a caller that has not read a zone file, and it says
so in every report it produces instead of passing itself off as the retail
set. `nfsu::TrackResidency` offers three rules, and the default is still
the explicit fallback:

| `TrackResidencyRule` | What it is |
| --- | --- |
| `every_section` | **Not a retail rule.** Every valid section of the named region is drawn. The default, and the safe answer when no zone file has been read. |
| `no_zone` | 0x00446459 exactly: `number % 100 < 10`. What retail does when the camera is inside no zone polygon, which over the corpus is 39054 of 251100 sampled points. |
| `zone_list` | 0x00446473 to 0x004464D6 exactly: sections 2600 to 2699 first, then a supplied list. `nfsu::track_zone_residency` supplies it from a decoded `0x80034150`; a caller may still supply its own. |

Asking for `zone_list` with an empty list raises an issue that names
`0x80034150` and the call that fills it, so a run can never quietly look
like it consulted zone data it does not have.

## The box test (0x0040DB20)

Every visibility decision in the scenery path goes through one function.
It takes a minimum in `edx`, a maximum in `ecx`, a transform flag in `eax`
and a render context on the stack, and returns 0, 1 or 2.

```text
0040DB68  fld  dword ptr [esp + 0x10]      ; max
0040DB6F  fadd dword ptr [esp + 0x30]      ;   + min
0040DB7C  fmul dword ptr [0x6cc7dc]        ;   * 0.5        -> centre
0040DBB0  fld  dword ptr [esp + 0x10]      ; max
0040DBB4  fsub dword ptr [esp + 0x20]      ;   - centre     -> extent
0040DB82  lea  ecx, [eax + 0xc4]           ; the plane array
0040DBD4  fld  dword ptr [ecx + 4]         ; per plane: |n| . extent
0040DBD7  fabs
0040DBD9  fmul dword ptr [esp + 0x38]
0040DBF2  fld  dword ptr [esp + 0x28]      ;            n . centre
0040DC0A  fadd dword ptr [ecx + 8]         ;            + the constant
0040DC0D  fld  st(0)
0040DC0F  fadd st(2)                       ; distance + radius
0040DC11  fcomp dword ptr [0x6cc7a4]       ;   against 0.0
0040DC1C  jnp  0x40dc4c                    ;   below: return 0
0040DC1E  fsub st(1)                       ; distance - radius
0040DC20  fcomp dword ptr [0x6cc7a4]
0040DC2D  jp   0x40dc34
0040DC2F  mov  esi, 1                      ;   below: crossing
0040DC34  inc  edx
0040DC35  add  ecx, 0x10                   ; 16 bytes per plane
0040DC38  cmp  edx, 6                      ; six planes
0040DC3D  xor  eax, eax
0040DC3F  test esi, esi
0040DC41  sete al
0040DC44  inc  eax                         ; 2 when no plane crossed, else 1
```

`0x006CC7DC` holds `0.5` and `0x006CC7A4` holds `0.0`; both were read from
the image. `edx` starts at 1 and the loop runs while it is at or below 6,
with the plane pointer advancing 16 bytes each turn, so there are exactly
six planes of four floats: three at `+0`, `+4`, `+8` and a constant at
`+12`. **The plane array starts at `[[context + 0x80] + 0xC0]`, not at
`+0xC4`.** 0x0040DB82 loads `+0xC4`, but the loop body reads `[ecx - 4]`
first (0x0040DBDD and 0x0040DBFD), so `ecx` points at each plane's second
float and plane 0 occupies `+0xC0` to `+0xCF`. The six span `+0xC0` to
`+0x11F`. A reimplementation that took `+0xC4` as plane 0's first field
would read `n1`, `n2`, `d` and the next plane's `n0` as one plane. The
scale the screen-size gate reads at context `+0xC0` (below) is a different
structure: that one is `[context + 0xC0]`, not `[[context + 0x80] + 0xC0]`.

So the classification is the standard centre-and-extent test:

- `distance + radius < 0` on any plane: **0, outside**;
- `distance - radius < 0` on any plane: **1, crossing**;
- otherwise **2, inside all six**.

The tree walker calls it with `eax` zero (0x004461E0 `xor eax, eax`), which
skips the transform at 0x0040DA30, so the stored boxes are used in world
space unchanged. `nfsu::track_classify_box` reproduces this path and only
this path, in the same summation order, because the result of each sum
decides a strict comparison against zero.

## The tree walk (0x00446140)

```text
0044614C  mov  eax, dword ptr [ebp + 8]     ; the section
0044614F  mov  ecx, dword ptr [eax + 0x28]  ; its bounding tree
00446155  mov  dword ptr [esp + 0x80], ecx  ; node 0 on the node stack
0044615C  lea  edi, [esp + 0x84]
00446163  mov  byte ptr [esp + 0x40], 1     ; code 1 on the code stack
00446168  lea  edx, [esp + 0x41]
00446170  mov  bl, byte ptr [edx - 1]       ; pop the code
00446173  mov  esi, dword ptr [edi - 4]     ; pop the node
0044617A  cmp  bl, 1
00446181  jne  0x446201                     ; code 2: do not test at all
00446183  movsx edx, word ptr [esi]         ; the six int16 of the box
004461F6  call 0x40db20
004461FF  mov  bl, al
00446201  test bl, bl
00446203  je   0x44627b                     ; code 0: skip the whole subtree
00446205  cmp  word ptr [esi + 0xc], 0      ; the entry count
00446214  lea  eax, [esi + 0xe]             ; the entries
00446224  mov  ax, word ptr [ecx]
0044622D  jl   0x446245                     ; negative: a child
00446232  movzx edx, bl                     ; the code
0044623A  call 0x445ec0                     ;   non-negative: an instance
00446245  mov  ecx, dword ptr [ebp + 8]
00446248  mov  ecx, dword ptr [ecx + 0x28]
0044624B  lea  eax, [eax + eax*2]
0044624E  shl  eax, 3                       ; entry * 24
00446251  sub  ecx, eax                     ; base - entry*24 = base + n*24
00446253  mov  dword ptr [edi], ecx         ; push the child
00446258  mov  byte ptr [edx], bl           ; push the parent's code
00446282  cmp  edi, ecx
00446284  jne  0x446170                     ; until the stack is empty
```

Four facts fall out of that, and each one changes what a reimplementation
must do:

1. **The traversal is an explicit last-in-first-out stack, not recursion.**
   A node's own instance entries are emitted where they stand, in entry
   order, and its children are pushed, so the children come back off in
   *reverse* entry order. The visible set has a defined order and
   `nfsu::cull_track_region` reproduces it.
2. **A node reached with code 2 is not tested.** 0x00446181 takes the code
   the parent stored and skips straight past the box test. This is the
   containment assumption: retail trusts that a child of a fully-inside
   node is itself fully inside. It is safe in the widening direction - the
   worst it can do is draw something that is outside.
3. **A node that tests 0 takes its whole subtree with it.** This is the
   direction where containment matters: a child that escapes its parent's
   box can be dropped while it is visible.
4. **Node 0 is the root and a child entry `-n` is `base + n * 24`.** The
   subtraction at 0x00446251 is of a negative number, so it is an addition;
   it is also the third independent confirmation of the 24-byte node
   stride.

The retail stack lives between `[esp + 0x40]` and the top of the frame
0x00446146 reserves. That frame is 0x174 bytes and the three pushes at
0x00446152, 0x00446153 and 0x00446154 come after it, and every `[esp + N]`
in the function is relative to the post-push `esp`, so the top is at
`+0x180`: the codes run `+0x40` to `+0x80` and the node pointers `+0x80` to
`+0x180`, which is **64 of each**. `TrackCullLimits::max_pending_nodes`
defaults to 64 to match. The cap is inferred from that frame rather than
read from an instruction.

### Does containment actually hold?

It has to, or rule 3 above silently drops geometry. Measured over the
whole corpus, independently of this repository's C++ decoder, by walking
the 593 sections' bytes directly: **7588 nodes, 29715 instances, 36710
parent-to-entry relations, 220260 axis comparisons, and not one
overshoot.** Slack here is room to spare, measured the way the code
measures it in `track_check_tree_containment`: `child_min - node_min` and
`node_max - child_max`, so a non-negative value means contained and a
negative one is an overshoot. On that convention the minimum is 0,
occurring 42339 times, the next value is +1, occurring 38468 times, the
maximum is 8939, and **no comparison is negative**. The two counts that
matter for a culler are the small ones: a sixth of the corpus sits exactly
one unit from failing.

**That is a property of this retail data, not a guarantee of the format.**
Nothing in the 24-byte node record requires a parent box to contain its
entries, no field records that it does, and no instruction in the retail
loader checks it; the exporter simply always produced it. A different
track, a modified file or a generated section could break it, and the
traversal at 0x00446140 would prune a visible subtree without noticing.
Retail can assume it because retail ships the data it was measured on.

So on retail data the hierarchical prune is exact, and the design decision
this measurement forces is the narrow one: **check containment, then
prune**. `nfsu::cull_track_region` verifies it per section before it uses
the tree, and a section that fails falls back to testing every instance
box directly, with an issue naming the node, the entry, the axis and the
overshoot in units. `parse_track_objects` already refuses such a section
(`TRACK_OBJECTS.md` parser rule 9), so the check is there for a caller
that built a section itself, exactly as `BindingKind::invalid_slot` is
handled in `track_scene.h`.

The check is not decorative. With a fixture whose node box is shrunk by
100 units on one axis while still naming the instance it no longer covers,
one camera gives an empty visible set with the check off and the correct
one-instance set with it on; `tests/track_visibility_tests.cpp` asserts
both.

This is a different question from whether an instance box contains the
mesh it places. It does not always: `TRACK_OBJECTS.md` measures 327 of
29674 checkable instances whose stored box misses part of their slot-0
mesh, by up to 48.7 units. The engine culls on the stored box, so those
are the game's own conservative bounds and its own error, not this
decoder's.

## The three per-instance gates (0x00445EC0)

```text
00445ECA  lea   ebx, [eax + eax*2]
00445ED2  shl   ebx, 4                        ; index * 48
00445ECF  mov   edi, dword ptr [ecx + 0x20]   ; the instance array
00445ED5  add   ebx, edi
00445ED7  mov   edi, dword ptr [ebp + 8]      ; the render context
00445EDA  mov   dl, byte ptr [edi + 0x84]     ; a render mask byte
00445EE2  mov   ax, word ptr [ebx + 0xe]      ; the instance flags
00445EE6  xor   eax, 0x60
00445EE9  test  dl, al
00445EEB  jne   0x446133                      ; gate 1: masked out

00445EF1  movsx eax, word ptr [ebx + 0xc]     ; the object index
00445EF5  lea   edx, [eax + eax*8]
00445EFB  lea   esi, [eax + edx*8]            ; * 72, the object record
00445EFE  cmp   dword ptr [ebp + 0xc], 1      ; the node's code
00445F02  jne   0x445f34                      ;   2: skip the box test
00445F0E  call  0x445520                      ; widen the int16 box
00445F24  call  0x40db20
00445F2E  je    0x446133                      ; gate 2: outside

00445F34  fld   dword ptr [0x6b780c]          ; 4.0
00445F3B  fadd  dword ptr [esi + 0x44]        ; + object record +68
00445F3E  lea   eax, [ebx + 0x10]             ; the instance position
00445F46  call  0x445e10                      ; the screen-size level
00445F4E  cmp   eax, 1
00445F55  jle   0x446133                      ; gate 3: too small to draw
```

**Gate 1, the flags mask.** The instance flags word at `+14` is exclusive-
ORed with 0x60 and its low byte ANDed with a byte of the render context;
a non-zero result skips the instance. This is a second traced use of that
field, beside the bit 0x10 the loader sets and clears at 0x00445B23 and
0x00445B36. The context byte itself is not recovered, so
`TrackCullOptions::instance_mask` defaults to 0, which masks nothing, and
the resulting set is a superset of the retail one on this gate.

**Gate 2, the instance box.** The same six-plane test, on the instance's
own stored int16 box, widened by 0x00445520 with no inflation of any kind.
Skipped when the node was already inside, which is the same containment
shortcut again, one level down.

**Gate 3, the screen size.** 0x00445E10:

```text
00445E19  fld  dword ptr [eax]               ; the instance position
00445E1B  fsub dword ptr [ecx + 0xa0]        ;   - the eye at +0xA0
00445E35  fmul dword ptr [ecx + 0xb8]        ; . the context axis at +0xB0
00445E4F  fld  dword ptr [ebp + 8]           ; the radius
00445E52  fchs
00445E54  fcompp                             ; behind by more than it?
00445E5F  xor  eax, eax                      ;   then level 0
00445E7B  fsqrt                              ; the length
00445E81  fsub dword ptr [ebp + 8]           ;   - radius            -> d
00445E88  fld  dword ptr [ecx + 0xc0]        ; a scale
00445E92  fcomp dword ptr [ebp + 8]          ; d <= radius?
00445E9A  jne  0x445eab                      ;   then the scale itself
00445E9E  fld  dword ptr [ecx + 0xc0]
00445EA4  fdiv dword ptr [esp + 0xc]         ;   else (scale / d)
00445EA8  fmul dword ptr [ebp + 8]           ;        * radius
00445EAB  call 0x674898                      ; truncate to int
```

The radius is object record `+68` plus the 4.0 at `0x006B780C`. The result
is a projected-size number, and 0x00445F4E drops the instance when it is 1
or less.

**The forward axis must be unit length.** The behind-the-camera reject
compares a dot product against `-radius`, which only means "behind by more
than the radius" for a unit vector; retail's is a camera basis axis at
context `+0xB0` and is unit by construction. `cull_track_region` normalizes
whatever a caller hands it, once, before any section is walked, so a
parameter that names only a direction cannot change the answer by its
magnitude. An axis that cannot be normalized disables the gate and raises
an issue rather than silently skewing the set. **This is where `TRACK_OBJECTS.md`'s `radius` name is settled:**
that note recorded that no instruction reading `+68` had been traced, and
0x00445F3B is the instruction. The scale at context `+0xC0` is not
recovered, so `TrackCullOptions::use_screen_size` is off by default and
the set is again a superset of the retail one.

### What is drawn once an instance survives

Past the three gates, 0x00445F5B picks a mesh slot, and doing so reads the
two words `TRACK_OBJECTS.md` records as unnamed flags:

```text
00445F5B  mov   ecx, dword ptr [edi + 0x84]   ; the render mask word
00445F61  test  ch, 8                         ; bit 0x0800
00445F76  movsx ecx, word ptr [esi + 0x20]    ; object +32
00445F7E  mov   eax, dword ptr [esi + 0x34]   ;   with slot 4's solid
00445F93  test  ch, 0x10                      ; bit 0x1000
00445F98  movsx edx, word ptr [esi + 0x22]    ; object +34
00445FA0  mov   eax, dword ptr [esi + 0x38]   ;   with slot 5's solid
00445FB7  lea   edi, [esi + 0x24]             ; else: slots 0 to 3
00445FBA  lea   edx, [esi + 0x18]             ;       object +24
00445FC0  movsx eax, word ptr [edx]
00445FC3  cmp   dword ptr [esp + 0x1c], eax   ; level >= this word?
00445FC7  jl    0x445fce
00445FC9  cmp   dword ptr [edi], 0            ; and the slot resolved?
00445FCC  jne   0x445fdc                      ;   take it
00445FCF  add   edx, 2
00445FD2  add   edi, 4
00445FD5  cmp   ecx, 4
```

So the object record's bytes `+24` to `+31` are read as **four int16**,
one per chain slot, each compared against the screen-size level, and the
loop takes the first slot whose word the level reaches and whose mesh
resolved. `+32` and `+34` are two more of the same, for slots 4 and 5,
selected by bits 0x0800 and 0x1000 of the render mask word. That word's
low byte is the same byte gate 1 uses.

Census over all 9681 retail objects, reading `+24` to `+34` as six int16:

| Word | Record offset | Range | Most common values |
| ---: | ---: | --- | --- |
| 0 | +24 | 1 .. 500 | 16 (6718), 100 (672), 48 (419), 200 (297) |
| 1 | +26 | 0 .. 350 | 0 (9178), 16 (288), 100 (43) |
| 2 | +28 | 0 .. 27008 | 0 (7461), 16 (1283), 30 (237) |
| 3 | +30 | 0 .. 60 | 0 (8513), 16 (1111), 3 (27) |
| 4 | +32 | 0 .. 0 | 0 (9681) |
| 5 | +34 | 0 .. 0 | 0 (9681) |

None is negative. Word 2 exceeds 500 in exactly two objects, both in
`STREAML1RA.BUN` and both reading 27008. Words 4 and 5 are zero
everywhere, which is `TRACK_OBJECTS.md`'s observation that `+32` is zero in
all 9681 records, and it means the two extra passes accept any level.

The words are not monotone across slots 0 to 3: 2376 of 9681 objects have
a later word above an earlier one. That is not a contradiction, because
the loop takes the *first* slot that passes rather than a range, so a
higher later word simply makes that slot unreachable at some levels.

This note does not implement slot selection. It is recorded here because
it is what the traced function does next, and because it settles two of
`TRACK_OBJECTS.md`'s open questions.

## What this project implements

`include/nfsu/track_visibility.h` and `src/track_visibility.cpp`. It reads
no file: a caller decodes a region with `parse_track_objects` and passes
the report in.

- `track_classify_box` is 0x0040DB20's zero-transform path, in its own
  order of operations. The library disables floating-point contraction
  (`-ffp-contract=off`, or `/fp:precise`), because every decision it makes
  is a strict comparison against zero and a fused multiply-add would move
  a boundary case and the visible set with it.
- `track_frustum_look_at` builds six planes from an eye, a target, an up
  vector, a vertical field of view, an aspect, a near plane and a far
  plane. **This construction is not recovered**: retail is handed its
  planes by the render context. The camera is stated in the renderer's
  world frame of [`FORMATS.md`](FORMATS.md) (`x = px`, `y = py`,
  `z = -pz`, y up) and the planes come out in the file's raw slot order,
  which is the frame the stored boxes use. `track_world_from_raw` and
  `track_raw_from_world` are the same map, for callers that need to state
  a point in one frame and read it in the other.
- `cull_track_region` applies residency, then one of three policies per
  section, and returns the visible instances in the retail order with a
  per-section summary, per-stage counts and a list of issues.

| `TrackCullPolicy` | What it does |
| --- | --- |
| `retail_tree` | The recovered rule: verify containment, walk the tree, prune, then the instance gates. The default. |
| `draw_everything` | **The explicit fallback.** Nothing is consulted at all: not the frustum, not the flags mask, not the screen-size level. Every instance of every resident section is visible, in instance order, whatever `instance_mask` and `screen_scale` are set to. Not what the game does, and the report says so on every section it culled. |
| `flat_instance_test` | Test every instance box, prune nothing. Exact for the stored boxes whatever the tree looks like, and what `retail_tree` degrades into. |

A section degrades to `flat_instance_test`, with an issue, when

- a node box does not contain the box of an entry below it,
- an entry names a node or an instance that is out of range,
- a node is reached more than once, so the tree is not a tree,
- the tree names an instance more than once, or does not name every
  instance, or
- the traversal would need more pending nodes than the limit.

`TrackCullReport::policy_held` is false when any section degraded, and
`TrackSectionCull::policy_used` names what each section was actually
culled with, so a degraded run cannot be mistaken for a clean one.

Every policy satisfies `instances_visited + instances_pruned == resident
instances`, which is asserted over four cameras in the tests.

### Deterministic camera tests, and what breaks them

`tests/track_visibility_tests.cpp` builds a written `0x80034100` region at
a real absolute offset and parses it, so the fixture goes through the same
byte rules a retail section does. Eight instances sit in a row along world
+X at 100, 200 .. 800, each a cube of half extent 5; node 1 groups the
first four, node 2 the last four, and node 0 names both in that order.

Four cameras on the world origin, differing only in the far plane, pin
exact sets and exact counts:

| Far plane | Visible, in order | Node tests | Nodes culled | Box tests |
| ---: | --- | ---: | ---: | ---: |
| 1000 | 4, 5, 6, 7, 0, 1, 2, 3 | 1 | 0 | 0 |
| 450 | 0, 1, 2, 3 | 3 | 1 | 0 |
| 450, from x = 900 | 4, 5, 6, 7 | 3 | 1 | 0 |
| 150 | 0 | 3 | 1 | 4 |
| 100 | 0, itself crossing | 3 | 1 | 4 |

The first row is the containment shortcut: the root is inside, so nothing
else is tested, and the order is the reverse child order of the
last-in-first-out stack. The fourth row is the opposite: node 1 crosses
the far plane, so its four instances are tested one at a time. The fifth
exists because the fourth cannot tell the class an instance box tested as
from the class it inherited - at 150 the surviving instance is wholly
inside, and only at 100 does it straddle the plane itself.

Three mutation passes have now been run against this suite, and they are
counted separately because they measure different things. A pass written
by the author of a test suite is weak evidence about that suite: it tends
to mutate what the tests already name.

**Pass one, 18 mutations, written with the implementation.** Each one
changes the recovered rule itself. All 18 were caught from the start.

```text
children in entry order instead of reverse    never test an instance box
test every node, never inherit the inside     emit instances in reverse
never prune an outside node                     order under the flat policy
skip the containment check                    drop the 0x60 flags bias
reject on the near corner, not the far one    draw at screen level 1
round the screen level instead of truncating  drop the radius bias
keep the sign of pz in the world map          widen the 26xx band by one
use 100 for the no-zone sub-index             let the fallback see the
count a pruned subtree as one instance          frustum and the gates
let an unreachable instance through           accept far at the near plane
```

**Pass two, 36 mutations, written afterwards against the reporting
surface, the plane set and the branches pass one never reached. Eleven
survived.** Seven report fields were never read back
(`sections_considered`, `summary.visible`, `summary.instances`,
`summary.letter`, `has_letter`, `requested_policy`, `instance_class`); the
entry-count upper bound and the containment check's maximum side were
never violated; and two of the six frustum planes could be replaced by
another with nothing noticing. The fixture was symmetric about pz and py
and every camera looked along +X past a single off-axis box, so one side
plane was ever exercised and the near plane was never isolated from the
far one.

**Pass three, 27 mutations, written by an independent reviewer of this
branch who did not see the suite's own mutation lists.** That reviewer
wrote 101, discarded 3 as no-op or equivalent, ran 98, and reported these
27 as surviving. They fell into groups this suite had systematically
missed:

| Group | Count | What the suite could not see |
| --- | ---: | --- |
| frustum construction | 6 | the world-to-raw permutation, the aspect ratio, the halving of the field of view, the handedness |
| the box test | 4 | both strict comparisons, and the absolute value on two of the three radius terms |
| the containment check | 6 | pz, py, child-node entries, a one-unit overshoot, the entry-count guard, the axis-name table |
| ordering and limits | 4 | the gate order, the subtree fold on a tree deeper than two, the pending-node default |
| screen size | 2 | both boundaries, where the two branches meet |
| the report surface | 5 | `node_index` under a policy that walks no tree, three stable names, two constant-valued summary fields |

The band between the two half-angles is the clearest case. At the
fixture's camera the horizontal half-angle is 0.76980 and the vertical is
0.57735, and every off-axis box in the suite sat at 400, outside both. A
box at 65 is crossing on pz and outside on py, and that single pair of
boxes is what separates the two angles. The other structural gap was the
fixture's depth: a two-level tree cannot tell a subtree fold that
accumulates from the leaves up from one that does not, because the two
orders agree there.

What closed them: a test that writes out all 24 numbers of the six planes
for a reference camera and compares them per index, so a permuted normal,
a wrong aspect, an unhalved field of view and a handedness flip each move
a number and each name a different plane; two boxes in the band between
the half-angles; two boxes resting exactly on the two comparison
boundaries, which is also the first test that sits on the boundary the
`-ffp-contract=off` flag exists to protect; a box straddling the top
plane, the only plane whose raw normal is negative on slot 2; containment
overshoots on all three axes, above and below, at one unit rather than a
hundred, and on a child node rather than an instance; a three-level tree;
a three-section fixture whose sections differ in instance count and in
whether their number yields a letter at all; and direct calls to
`track_check_tree_containment` so that its own guard is not masked by
`prepare_tree` refusing the same node a moment later.

**Pass four, 1 mutation, from that reviewer's close-out.** It removed only
the three lines that write the normalized axis back into the options,
leaving the `normalize3` call and the degenerate-axis issue in place. The
suite stayed green at 370 checks while the retail numbers regressed to
exactly the defect the previous round had fixed: 37, 28 and 28 for the
same ray at three target distances. The regression test for that fix could
not fail on the property it named, because the axis is read in exactly one
place - the behind-the-camera reject - and every instance in the fixture
was in front of the eye, so the dot product was positive whatever the
axis's length. The screen-size eye now sits past the first instance of the
row, which leaves one instance behind it, and removing those three lines
now fails the suite.

That one is worth more than its count. It was not a gap in coverage of the
rule; it was a gap in the coverage of a fix, and the test guarding it read
as though it closed the hole. Eleven assertions of that family were found
in one round, one more in the next and this is the thirteenth.

The suite went from 199 checks to 373. **All 82 mutations - 18, 36, 27 and
1 - are caught, with no survivor in any pass.** The 28 written by someone
who had not read the first two lists are the interesting number.


### The retail differential

Over two regions of `Tracks/STREAML1RB.BUN` - the single section at
offset 23011344 size 4168 that the acceptance block below uses, and the
two-section region at offset 0 size 177232 that `TRACK_OBJECTS.md` decodes
- and 864 cameras - four eye positions along the
section, three depths, two heights, three far planes and six directions -
`retail_tree` and `flat_instance_test` selected exactly the same instances
every time: 0 mismatches and 0 failed runs. 382 of those cameras had a
non-empty visible set, of sizes from 1 to 188 across 41 distinct sizes;
the other 482 saw nothing, and an agreement on two empty sets proves
nothing, so both numbers are reported. Each side wrote a named file that
was deleted by exact name first, and a run that produced no file, printed
to standard error or exited non-zero was counted as a failure rather than
an agreement.

## Read-only retail acceptance

Every command below only reads. Nothing writes to the installation, and no
game data, screenshot or exported asset may be committed.

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23011344 4168 450 20 -80 900 20 -80 --far 60 --list 0

build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23011344 4168 450 20 -80 900 20 -80 --far 4000 --list 0

build/nfsu_probe track-visibility X:/Games/NFSU/Tracks/STREAML1RB.BUN \
    23011344 4168 450 20 -80 900 20 -80 --far 60 \
    --policy draw-everything --list 0
```

The eye and the target are in the world frame of
[`FORMATS.md`](FORMATS.md), so `450 20 -80` is `px 450, py 20, pz 80`.

Expected:

1. The first exits 0, writes nothing to standard error, and prints
   `section 0 number 303 letter C resident policy retail-tree containment
   holds instances 39 visible 9`, `nodes: visited 9 tested 9 culled 3`,
   `instances: visited 20 pruned 19 box tests 20`, `culled: by mask 0 by
   box 11 by size 0` and `visible: 9`.
2. The second exits 0 and prints `nodes: visited 10 tested 9 culled 0`,
   `instances: visited 39 pruned 0 box tests 20`, `culled: by mask 0 by
   box 2 by size 0` and `visible: 37`.
3. The third exits 0 and prints `visible: 39` with `nodes: visited 0
   tested 0 culled 0`: the fallback draws the whole section from the same
   camera that the first command drew nine instances from.
4. Running any of them twice gives byte-identical output.
5. The SHA-256 and size of every file named above are unchanged
   afterwards.

To pick another region, list the container first and take one
`0x80034100` line; the size to pass is its payload size plus the 8-byte
header:

```text
build/nfsu_probe chunks X:/Games/NFSU/Tracks/STREAML1RB.BUN 100000
```

## Observed

- The scenery draw path is 0x004465B0: pick at most 128 section numbers at
  0x00446400, then walk each resident section's `0x00034104` tree at
  0x00446140. A section with no tree pointer is skipped.
- A visibility zone is a polygon tested in two dimensions against a world
  point (0x005679B0, stride 8), reached through a most-recently-used list
  of `0x00034152` records at 0x0077A940, which is `0x0077A930 + 0x10`
  (0x00446FD9). The five list heads from 0x0077A930 are reset by the
  `0x80034150` callback at 0x00446AB4 and filled by the loader at
  0x00446DB0; [`TRACK_ZONES.md`](TRACK_ZONES.md) decodes the records.
- With a zone active the drawn list is sections 2600 to 2699 plus the
  zone's own int16 list of at most 64 numbers; with no zone it is every
  section of the loaded list at 0x0077A978 whose number modulo 100 is
  below 10. Both arms stop at the 128-entry cap 0x004465C1 passes in.
- No file carries a top-level `0x80034150` as it lies on disk: of the 207
  non-empty files that walk as chunks to end of file, none holds one. 33 of
  34 `.lzc` files begin with `JDLZ`, the exception being
  `Global/GlobalB.lzc`. Decompressed with the algorithm
  [`FORMATS.md`](FORMATS.md) documents and `nfsu::jdlz_decompress`
  implements, all 31 `Tracks/*.lzc` files walk as chunks to end of file and
  each carries exactly one `0x80034150`, of 1216 to 29424 bytes, holding
  `0x00034151`, `0x00034152`, `0x00034153` and `0x00034154` in that order
  in all 31.
- The box test is six planes of four floats at `[[context + 0x80] + 0xC0]`,
  centre-and-extent, returning 0 outside, 1 crossing and 2 inside, with
  0.5 at 0x006CC7DC and 0.0 at 0x006CC7A4.
- The tree walk is a last-in-first-out stack seeded with node 0 and code 1;
  a node with code 2 is not tested, a node testing 0 loses its whole
  subtree, a node's instances are emitted in entry order and its children
  are taken in reverse entry order.
- Over all 593 sections, 36710 parent-to-entry relations and 220260 axis
  comparisons, no node box fails to contain an entry below it.
- An instance is skipped when `(flags ^ 0x60) & context_byte` is non-zero
  (0x00445EE6), then box-tested unless the node was inside, then dropped
  when its screen-size level is 1 or less (0x00445F4E).
- The screen-size level is `trunc(d <= r ? scale : (scale / d) * r)` with
  `r` the object's `+68` plus 4.0 and `d` the distance to the instance
  position less `r`, and 0 when the instance is behind the camera axis by
  more than `r`.
- Object record bytes `+24` to `+34` are read as six int16 and compared
  against that level, one per mesh slot. None of the 58086 words is
  negative; `+32` and `+34` are zero in all 9681 objects.

## Inferred

- The six int16 at object `+24` to `+34` are per-slot level thresholds,
  from the shape of the loop at 0x00445FC0 and from the way the values
  cluster on 16 and on zero. `TRACK_OBJECTS.md` types `+24` and `+28` as
  two u32 flag words; the executable reads four int16 there, and that is
  the reading this note uses.
- Bits 0x0800 and 0x1000 of the render mask word at context `+0x84` are
  extra render passes, because each one substitutes a single fixed mesh
  slot for the whole chain.
- Retail's traversal stack holds at most 64 nodes and 64 codes, from the
  0x174-byte frame at 0x00446146, the three pushes after it and the two
  stack bases at `[esp+0x40]` and `[esp+0x80]`. No instruction bounds it.
- The 26xx band being both always drawn and linked at the head of the
  section list means the letter `Z` sections are the always-resident set.

## Unknown

- Every bit of the render mask byte at context `+0x84`, so gate 1 cannot
  be reproduced with a retail value.
- The scale at context `+0xC0`, so gate 3 cannot be reproduced with a
  retail value.
- How the six planes at `[[context + 0x80] + 0xC0]` are built, and whether
  they are ever anything but a symmetric perspective frustum.
- What the two objects whose `+28` second word reads 27008 are for.
- Whether the letter range `A` to `T` at 0x00446BF0 bounds the track
  sections generally or only the two list operations that check it.
  [`TRACK_ZONES.md`](TRACK_ZONES.md) narrows this: the 2600 to 2699 band
  is letter `Z`, is outside that range, and is drawn anyway, so the range
  is not a filter on what is drawn.
- Collision, and everything about streaming: what decides that a section
  is in the table at 0x0072A008, or in the loaded list at 0x0077A978, in
  the first place. The zone data says which sections a camera asks for; it
  says nothing about which are there to be asked for.
