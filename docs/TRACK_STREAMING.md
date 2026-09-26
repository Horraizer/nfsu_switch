# Streaming along a path: which sections are resident where

Clean-room notes on how Need for Speed: Underground (PC) keeps a track's
sections resident while a car moves, and what that costs this project along
the first circuit of the game. Everything here comes from reading a
user-provided retail installation and its executable read-only. No game data
is stored in this repository: this note holds addresses, counts and hashes
only, and no frame is committed.

[`TRACK_WHOLE.md`](TRACK_WHOLE.md) recovered which sections a race *starts*
with and traced the loader's request, its pool and its eviction far enough
to say that a race on track 1003 probably never evicts anything. It left the
streamer's per-frame behaviour - which zone a car stands in, when a change
of zone counts, how many sections a frame may place, what happens when one
does not fit - as the edge of what it traced. This note traces that edge and
turns it into a deterministic model for issue #157.

The answer first:

- **The streamer is traced end to end** (below): the two tracked slots are
  filled every frame from player records 1 and 2 - consistent with two local
  players; a slot's zone comes from its position and two look-ahead points
  along its velocity; a new zone counts only once it has been sampled three
  frames in a row; a rebuild clears the old request and lists the new one;
  a pass places at most two sections, each only if its byte count plus
  0x400 fits the pool's largest free block less 0x58; when one does not
  fit, loaded sections no request names are released least recently
  requested first, and a call stops releasing once it has released more
  than 0x9C3C00 bytes - a test made before each release, so one call can
  release that much plus one more record of any size; and when nothing is
  left to release the section is given up, not crashed on. `0x00444830`,
  which the previous note could not name, is `xor eax, eax` / `ret 4`: it
  frees nothing.
- **`nfsu::simulate_track_residency`** (`include/nfsu/track_residency.h`,
  `src/track_residency.cpp`) is that streamer as a function of the
  streaming table, the zone report, a cost per record and one position per
  60 Hz step. The same path gives the same loads, releases and per-step
  bytes **in every build** - the step and the path sampling call no library
  mathematics, and their square roots are correctly rounded in integer
  arithmetic ("The same in every build", below) - and a SHA-256 over every
  step pins it. What it does that retail does not is listed rule by rule
  below.
- **`nfsu_probe track-stream-path`** runs it along a path file or along a
  **hand-given** polyline around the circuit of race 1 (event 1, track
  1003, stream `L1RA`), measures this project's own bytes for every
  distinct resident set against the desktop and Switch budgets of #52, and
  draws frames from only the resident sections.
- **Along the 1003 lap the retail rule keeps the 78-section start set the
  whole way**: 15 rebuilds - the forced one that lists the start set and
  one for each of the 14 zones the path commits - 78 loads, 0 releases. It
  holds 14760060 of the table's bytes in a 21808648-byte pool, and costs
  this project **114238870 resident bytes** - the #156 figure, measured
  again here set by set - which is **over the Switch plan's 16 MiB region by
  97461654 and its 48 MiB streaming arena by 63907222**.
- **Keeping only what the current zone asks for does not fit either.** The
  `zone-only` bound (not retail) peaks at 98329802 bytes, 21 sections in
  zone 1001, still over the arena by 47998154; 91020800 of those bytes are
  RGBA8 texture. What would fit is to stop keeping decoded textures in the
  CPU streaming arena: the permanent set's geometry and instances are
  13674390 bytes, inside one 16 MiB region, and its GPU requirement,
  115492784 bytes, is inside the 256 MiB GPU placeholder. That is a finding
  for #52, not a failure of this issue.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |
| `Tracks/TEX1003TRACK.BIN` | 1086216 | `081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5` |
| `Global/INGAMEB.BUN` | 1670080 | `044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |

`Global/GLOBALB.BUN` is the retail image of `GLOBALB.LZC`; this
installation's `Global/GlobalB.lzc` and `Global/GLOBALA.BUN` are 2025
modifications and were not read. Executable addresses are `speed.exe`
virtual addresses; `.text`, `.rdata` and `.data` all map with
`file_offset = VA - 0x400000`, checked against the PE section table
(`.text` at 0x401000 raw 0x1000, `.rdata` 0x697000 raw 0x297000, `.data`
0x6D4000 raw 0x2D4000).

## The streamer, frame by frame

The object is the one [`TRACK_WHOLE.md`](TRACK_WHOLE.md) calls the streamer,
at 0x0077A980. The main loop calls 0x00445050 on it once a frame
(0x00447A01..0x00447A06), and 0x00445050 calls the update 0x00445180
(0x00445071).

### The two slots: 0x0048E985..0x0048E9FE

Two 0x44-byte slot records sit at `+0xD4` and `+0x118`. Every frame, before
the streamer runs (0x004479BE calls 0x0048E8B0), the game copies into them
from the two player records at 0x00740504 and 0x00740564:

```text
0048E985  mov  ecx, 0x77aa58                 ; slot 0 + 4
0048E990  cmp  byte ptr [edx - 0x3c], bl     ; this player is in the race?
0048E9CE  mov  eax, dword ptr [edx - 4]      ; its car
0048E9D1  fld  dword ptr [eax + 0x44]        ; slot 1 of the position
0048E9D4  mov  esi, dword ptr [eax + 0x40]   ; slot 0 of the position
0048E9DC  fld  dword ptr [eax + 0x1e4]       ; the velocity, same two slots
0048E9E2  mov  eax, dword ptr [eax + 0x1e0]
0048E9EE  mov  byte ptr [ecx + 0xc], 1       ; the slot is live
0048E9F5  add  ecx, 0x44                     ; the next slot
```

So the two slots are filled from player records 1 and 2 of the 0x60-byte
table at 0x00740460. 0x00408370 sets 0x007364A0 to 3 when both records have
a car, and 0x0042E0AE and 0x0047E6B7 branch on it; that is consistent with
two local players, but what fills the records was not traced. A
single-player race has one live slot; the other is never live and reads key
-1. The slot holds the
position at `+0` and `+4`, the velocity at `+8` and `+0xC`, the live byte
at `+0x10`, the committed key at `+0x12`, the last sampled key at `+0x14`
and a count at `+0x18`. 0x00444840, which the race setup calls for slot 0
(0x00422F44, 0x00422F6F), places a car: position, zero velocity, live,
count -1, last key 0.

### The zone key: 0x00444870

```text
00444877  fld  dword ptr [ecx + 8]           ; speed = sqrt(vx*vx + vz*vz)
00444891  fcomp dword ptr [0x6ccca4]         ; 178.8109130859375
0044489C  jne  0x4448a5                      ; not above: look ahead
004448A5  fld  dword ptr [0x6b781c]          ; 1.5 * speed
004448AF  fcomp dword ptr [0x6b7820]         ;   against 100
004448BC  fld  dword ptr [0x6b7820]          ; above: scale = 100 / speed
004448D5  fld  dword ptr [0x6b781c]          ; else scale = 1.5
00444910  fild dword ptr [esp + 0x10]        ; point i = pos + (i * 0.5) * offset
0044491B  fmul dword ptr [0x6cc7dc]          ;   0.5
0044493D  fcomp dword ptr [0x6ccca0]         ; above 17.88109: ask the path
0044495C  call 0x4436d0                      ;   structure at 0x007372B0 first
00444A17  call 0x446fd0                      ; then the zone polygons
00444A20  test ebp, ebp                      ; point 0 accepts any zone
00444A28  mov  edx, dword ptr [ecx + 0x598]  ; points 1 and 2 only a zone
00444A42  cmp  word ptr [edx], bp            ;   the 0x00034153 record names
00444A84  cmp  word ptr [esp + eax*2 + 8], dx ; the answer: the farthest point
00444A9C  mov  ax, word ptr [esp + eax*2 + 6] ;   before the first with no key
```

The look-ahead offset is 1.5 seconds of the velocity, clamped to 100 units,
and zero above a speed of 178.8109 (0x4332CF98), which reads as 400 mph in
metres per second. Three points are sampled, at 0, 0.5 and 1.0 of the
offset. The result is point 2's key if points 1 and 2 both have one, point
1's if only point 1 does, and point 0's otherwise - so a car turns the zone
ahead of it on as soon as its look-ahead reaches a zone the route allows,
and a zone off the route counts only once the car itself stands in it.

### When a new zone counts: 0x00444FB0

```text
00444FD0  mov  cl, byte ptr [esi - 8]        ; live?
00444FD3  or   eax, 0xffffffff               ;   no: key -1
00444FDF  call 0x444870                      ;   yes: sample it
00444FE4  cmp  ax, word ptr [esi - 4]        ; the same as last time?
00444FEC  inc  ecx                           ;   yes: count it
00444FF1  cmp  ecx, -1                       ;   no: a count of -1 ...
00444FFA  mov  dword ptr [esi], 0x3e8        ;   ... becomes 1000,
00445002  mov  dword ptr [esi], 1            ;   anything else 1
00445013  cmp  dword ptr [esi], 3            ; differs from the committed key
00445016  jge  0x44501a                      ;   and seen fewer than 3 times:
00445018  mov  eax, ecx                      ;   keep the committed one
0044501F  mov  byte ptr [esp + 0x18], 1      ; a change
00445032  mov  al, byte ptr [ebx + 0x179]    ; or +0x179 forces one
```

A new key therefore counts on its third consecutive sample, except the
first one after a count of -1, which counts at once. The count is -1 after
0x00444840 places a car and after the zone container loads: 0x00446F9E
stores 1 at `+0x179` and -1 in both counts. That is the forced rebuild that
lists the start set. 0x0044658B also stores 1 there, and `edi` in both
counts, when 0x00736138 is set, which was not traced further; 0x0047BA5C,
0x0047BACF, 0x0047BFF5 and 0x0047F296 store it too, the first two inside
the front-end code [`TRACK_WHOLE.md`](TRACK_WHOLE.md) names and the other
two not traced.

### The update: 0x00445180

```text
00445180  mov  eax, dword ptr [edi + 0x580]  ; no pool yet: nothing to do
00445193  call 0x444fb0                      ; sample both slots
00445198  mov  cl, byte ptr [edi + 0x15d]    ; loaded and no change: done
004451AF  cmp  ecx, 3                        ; three reads in flight: wait
004451C6  jg   0x44531a                      ; a change, reads in flight: wait
004451D4  call 0x444e90                      ; a change: free what is placed
004451DB  call 0x4443e0                      ;   and not read, clear the old
0044522F  mov  word ptr [ecx + 0x12], ax     ;   request, commit the keys
0044526F  call 0x444b50                      ;   and list the new request
00445280  call 0x444ee0                      ; +0x15E clear: a placement pass
004452F6  call 0x4446e0                      ; start reads, at most 3 in flight
00445307  jg   0x44531a                      ; reads in flight: not done
00445313  mov  byte ptr [edi + 0x15e], 0     ; the pass stopped early: again
00445323  mov  byte ptr [edi + 0x15d], 1     ; else the request is loaded
0044533B  mov  byte ptr [edi + 0x178], 1     ;   and the track byte is done
```

0x004443E0, the rebuild's first half, stores 0 in `+0xB` of every listed
record, empties the list, sets both committed keys to -1 and clears
`+0x15C..+0x161`. 0x00444B50 is the request [`TRACK_WHOLE.md`](TRACK_WHOLE.md)
traces; its zone part (0x00444C16..0x00444C92) skips a slot whose committed
key is 0 or less and otherwise appends the whole section list of the zone
keyed on it. The counter it stamps a request with, 0x006F0888, is advanced
by 0x00585D70 by the frame time times 60.0 (0x006CC8B0), and by at least
one, every frame: a 60 Hz tick.

### Placing: 0x00444EE0, 0x00444D80, 0x00444780

```text
00444DAB  mov  eax, dword ptr [esi + 0xc]    ; skip what is placed or loaded
00444DC4  call 0x565810                      ; the pool's largest free block
00444DC9  sub  eax, 0x58                     ;   less 0x58, at least 0
00444DD7  lea  edi, [ecx + 0x400]            ; byte count + 0x400
00444DDF  jg   0x444e40                      ; does not fit: add its byte
00444E40  add  dword ptr [esp + 0x10], ecx   ;   count to the unplaced sum
00444DEB  call 0x565b60                      ; fits: allocate the byte count
00444DFB  mov  dword ptr [esi + 0xc], 1      ;   placed
00444E31  cmp  eax, 1                        ; the second placement ends the
00444E72  mov  byte ptr [eax + 0x15f], 1     ;   pass, marked deferred
```

0x00565810 walks the pool's free list for its largest block (0x005657D0
next to it sums the list instead). 0x00444EE0 runs that pass and, while it
reports an unplaced sum, loops:

```text
00444F00  cmp  ebp, 0x9c3c00                 ; released more than 10240000
00444F06  jg   0x444f42                      ;   bytes this call: give the
00444F42  xor  al, al                        ;   frame up
00444F11  call 0x444e90                      ; free what the pass placed
00444F17  call 0x444780                      ; release one
00444F20  jg   0x444f2e                      ; freed something: pass again
00444F23  call 0x444830                      ; else: xor eax, eax / ret 4
00444F49  mov  byte ptr [esi + 0x160], 1     ; nothing to release: give up,
00444F50  call 0x444d80                      ;   place what fits, and go on
```

The limit test at 0x00444F00 comes before each release, not after it: a
call that has released anything up to 0x9C3C00 bytes goes on to release one
more record whatever its size, so one call can release 0x9C3C00 bytes plus
its largest record - not `Y0`, which is one of the four fixed numbers and so
is always requested and never released, but on 1003 the largest record
that is ever released, `401` at 1730080 bytes, released at 10239999 bytes
makes 11970079 in one call - and the next test gives the frame up.

0x00444780 walks the table and releases the record in state 3 whose `+0xB`
is 0 - loaded and named by no current request - with the smallest `+0x24`
stamp, keeping the first found among equals (0x004447B5 `jge`), and returns
its byte count (+24). Reads start in 0x004452A8..0x004452F6, the placed
record of more than 1000000 bytes first and otherwise the highest number,
while fewer than three are in flight; 0x00444740 marks a finished read
state 3.

## The model, and what in it is not retail

| Rule | Retail evidence | In the model |
| --- | --- | --- |
| one step is one frame and one 60 Hz tick | 0x00447A06, 0x00585D70 | NOT retail in timing: retail frames vary; the three-sample rule counts frames and the stamp counts ticks, and the model makes them one |
| two slots, the players' cars | 0x0048E985..0x0048E9FE | slot 0 is the path's car; slot 1 is never live (single player) |
| the zone key with look-ahead | 0x00444870 | as traced for the polygons, in float32 with contraction off where retail uses x87, and the speed's root correctly rounded (`track_residency_sqrtf`) where retail uses fsqrt |
| the path structure first above 17.88 | 0x0044493D..0x004449FA | NOT reproduced: 0x007372B0 is not decoded (#61). Every sample of the 1003 runs is above that speed, so every key there is the polygons' answer, not certainly retail's |
| a new key on its third sample, -1 on its first | 0x00444FB0 | as traced |
| the start: zones load, a rebuild is forced, no car yet | 0x00446F9E | steps with no live slot until the start set is loaded, then the car placed as 0x00444840 places it. Whether the car's slot is live during the loading screen was not traced |
| the request | 0x00444B50, 0x00444D0E | as traced, with its 256-record stop; the two-player shift, the forced list and the debug section are not applied (not in a single-player race) |
| the track byte until the first load | 0x00444C94, 0x0044533B | as traced |
| a rebuild clears only the listed records | 0x004443E0 | as traced: a record marked past the 256th stays marked for good, as retail's would |
| at most two placements a pass, deferred | 0x00444E31 | as traced |
| fits: byte count + 0x400 within the largest free block less 0x58 | 0x00444DC4..0x00444DDF | NOT retail in two respects: the free room is the budget less what is held and in flight, with no fragmentation, so the model can place what a fragmented retail pool could not; and nothing is charged per allocation - 0x00565B60, called with flags 0x2011, is not traced, and the model subtracts 0x58 once from the free room rather than per block - so the byte at which a section fits, which the tests pin, is the model's, not retail's |
| what a loaded record holds | +28, kept bytes | NOT traced: that a texture-pack section shrinks to its kept bytes once loaded is inferred from the pool 0x00444AF0 sizes (kept sum, or one read plus kept) |
| release: loaded, unrequested, smallest stamp, first in table order | 0x00444780 | as traced |
| 0x00444830 frees nothing; give up and go on | 0x00444830, 0x00444F48 | as traced; the step is reported as over budget |
| a call stops releasing once it has released more than 0x9C3C00 bytes, tested before each release | 0x00444F00, 0x00444F06 `jg` | as traced: one call can release 0x9C3C00 bytes plus one record, and a limit of 0 still releases one |
| reads: at most 3 in flight, large first | 0x004452A8..0x004452F6 | NOT retail in timing: a read completes before the next step, and reads start in placement order. A pass places at most two, so both start at once and the order changes nothing |
| `zone-only` | none | NOT retail: never the track byte, and every rebuild releases what the new request does not name. The least a zone-driven streamer can keep |
| the path: lengths, samples and directions | none | this project's, in double with contraction off; every root correctly rounded (`track_residency_sqrt`) and the ceiling taken without a library call |

The currency the budget is counted in is the caller's. The table's own
(`--cost table`, the default) places a record by its byte count and holds
its kept bytes, against the retail pool. This project's own
(`--cost prepared`, NOT retail) charges each record **an upper bound on
what it can add to any resident set**: the vertex and index bytes of its
valid meshes, the instance bytes of all its scenery instances whether or
not they place, and the RGBA8 bytes of every texture its own packs decode
that the geometry of the sections any zone lists wants - decoded from that
section alone, so a texture another pack shadows in a larger scene is still
charged to every pack that holds it. The three texture files are charged
the same way, once, off the top of the budget. A resident set's measured
bytes are then at most the sum of its shares plus the files', so a set the
streamer admits never measures over the budget; and the command checks
that at every step and prints a `measured:` line with the largest set and
the steps over.

The first version of this note charged each record its share of that one
scene instead, and said a smaller set could only cost less. It could cost
more (review comment 3630, F4): a texture the scene took from one pack came
from another pack or a texture file in a smaller set, and nobody's share
held it. 26 of the 80 sets of the pinned run measured more than their
shares, by up to 1105920 bytes, and under `--budget 12000000` 2580 steps
measured over the budget with nothing reported, the first at step 96: held
10327232, measured 12027328. With the upper bound the same run has 0 such
steps (its largest set measures 11878372), and the new check, run against
the old shares as a deliberate break, reports all 2580, the first `MEASURED
OVER the budget by 27328`. Over the pinned arena run, `--budget 12000000`,
the Switch region, 30000000 and zone-only under two budgets - 355 distinct
sets in all - the smallest margin of shares plus files over the measured
bytes is 98592.

## The same in every build

The first version of this library was not: a path of one segment, 1000 by
69, sampled at 150.35665100021347 units a second over the 1003 files, gave
401 samples, 480 steps and sequence `1D059F93...` in the default and Debug
builds and 402 samples, 481 steps and `183532D4...` in Release (review
comment 3630, F1). The segment's length is `sqrt(1004761)`, and GCC at
`-O0` took it from MinGW's libm, whose `sqrt` is not correctly rounded -
1002.3776733347565 - while `-O3` inlined `sqrtsd` - 1002.3776733347567,
the correctly rounded value - and at that speed the one ulp decided the
ceiling of the sample count. The pinned 1003 runs agreed only because none
of their segment lengths is such a case.

Now nothing in the step or the path sampling calls library mathematics:

- **`nfsu::track_residency_sqrt`** is the square root rounded to nearest,
  ties to even, in integer arithmetic only - the digit-by-digit root of the
  significand widened to 108 bits, one result bit a turn, then rounded on
  the 54th bit and the remainder. Every root of the path goes through it,
  and `track_residency_sqrtf` (the same, narrowed; rounding twice is
  harmless because 53 >= 2 x 24 + 2) takes the speed's root in the
  look-ahead, where retail uses `fsqrt`.
- The sample count's ceiling is a truncation and one exact comparison, and
  the two finiteness tests are `v - v == 0`.
- `objdump -dr` of the library's object in the default, Debug and Release
  builds shows no reference to `sqrt`, `sqrtf`, `ceil` or any other libm
  function, and no `sqrtss`, `sqrtsd` or `sqrtpd` instruction.

The review's path now gives 402 samples, 481 steps, sequence
`183532D491198D4FD7F2D5CDFFA73E6F35A3B07924FF91BFF89080FD45B7CC65` and
costs `7E1A33BE10701FB1BF490BA8A26CB324DAB5C817D47B99B8DFF7FD037F65D034` in
all three builds, and it is a unit test (`402 samples in every build`, with
the length's bit pattern `0x408F53057998EACF`) and a guard case. The root
itself is checked exactly: 200000 values across 400 binary exponents, each
tested with 128-bit integer arithmetic to lie strictly between the squares
of the two midpoints around its root, and the review's `1004761` with its
libm answer as the counter-example. None of the pinned 1003 hashes moved:
every root they take was already correctly rounded in both builds.

The library is also compiled with floating-point contraction disabled
(`-ffp-contract=off -fno-fast-math`), as `nfsu_core` is (issue #91), and
`cmake/residency_libraries.cmake` declares it for both configures, so the
console compiles it from the same source and options. The Switch tree runs
`nfsu_switch_track_residency_fma_census` (`tests/track_residency_fma_census.cmake`)
over its object: 18 `fmul`, 0 fused. With the option changed to
`-ffp-contract=fast` as a deliberate break, the same object holds 8 `fmul`
and 10 fused multiply-adds and the census fails the tree's `ctest`. Nothing
on the console links the library yet: it calls the table lookup of
`nfsu_track_stream` and the zone test of `nfsu_track_zones`, which the
console does not build.

Two library calls are left, both outside the step. A path file's decimals
are read with `std::istringstream`, the C++ library's conversion, which is
a call in every build. And the frames' camera yaw is `std::atan2` of the
direction of travel, also a call in every build here (`atan2` is a
relocation of `main.cpp`'s object at `-O0` and at `-O3`), so all three
builds use the same function and the frame pins below agree; a different C
library could move that camera by an ulp. The frames are desktop evidence
and nothing else reads them.

## The path

A path is a text file, one vertex per line: slot 0, slot 1 and slot 2 in the
raw order of [`FORMATS.md`](FORMATS.md) - slot 2 is the height - separated
by spaces, tabs or carriage returns; `#` starts a comment line. It is
sampled at a constant horizontal speed (`--speed`, 30 units a second by
default), one sample per 60 Hz step, and each sample's velocity is that
speed along its segment.

**`--hand-path 1003` is HAND-GIVEN.** It is not the game's route, which is
issue #61's. It was drawn over top-down CPU renders of the permanent set of
1003 with the zone polygons and a coordinate grid overlaid, along the
streets with the yellow centre markings, through the 13 zones of the
0x00034153 record keyed on 1003 in that record's order - 1003, 1002, 1001,
1104, 1105, 1106, 1103, 1201, 1202, 1203, 1204, 1205, 1206 and back to
1003 - and closes on its first vertex. It has 68 vertices and is 3411.31
units long; at 30 units a second that is 6824 samples, 113.7 seconds. Every
height is the static collision's under the vertex (`nfsu_probe
track-collision`), or, where that column holds no polygon or meets a deck
over the road, the heights on either side interpolated; vertices were added
where the road dips under a bridge. At the western end the path crosses a
junction north of a triangular building rather than following the race
barriers, which a hand-drawn line need not.

## `nfsu_probe track-stream-path`

```text
nfsu_probe track-stream-path <STREAM.BUN> <TRACKB####.lzc> <track-number>
    (--path <FILE> | --hand-path 1003) [--speed <units/s>]
    [--policy <retail|zone-only>] [--cost <table|prepared>]
    [--budget <pool|switch-region|switch-arena|BYTES>]
    [--pack <STREAM_NNNN.BUN>] [--texture <file>]...
    [--list <events|all|none>] [--frame-every <N> --out <PREFIX>]
    [--width <N>] [--height <N>]
```

It prints the zone and table summary, the path, the currency and budget,
then every step that rebuilt, loaded, released, could not place, was given
up, drew a frame or measured over the budget - `--list all` prints every
step, `--list none` none - as `nfsu::track_residency_step_line` spells it
followed by the resident set and its measured bytes, and `MEASURED OVER the
budget by N` when they pass it. Each distinct resident set is loaded,
exactly its byte ranges and nothing else, built as
[`TRACK_WHOLE.md`](TRACK_WHOLE.md) builds its set, and printed once with the
twelve budget rows of `track_whole_budget_rows`; an issue a load raises is
printed once per section, naming it (`section Y0 (2500) issue at ...`), as
`track-whole` prints its own. It ends with the counts, the peaks, the rows
of the peak set, a `measured:` line - in the prepared currency the largest
measured set and the steps over the budget, in the table currency a note
that the budget is in other bytes - `sequence: sha256` over every step line
and `costs: sha256` over every step's measured bytes. With `--frame-every N --out PREFIX` it
draws `PREFIX-NNNNNN.tga` every N samples from the resident scene, from a
camera that stands two units over the path point looking ten units ahead;
it is not the game's camera (#59), and a file that exists is never
replaced. Exit 0 on a complete run, whatever the budgets say; 1 for an
argument refused; 2 for a path, file or build failure; 3 when the zone file,
the table or a loaded section raised an issue.

## The 1003 lap

`nfsu_probe track-stream-path Tracks/STREAML1RA.BUN Tracks/TRACKB1003.lzc
1003 --hand-path 1003 --texture Tracks/TEX1003TRACK.BIN --texture
Global/INGAMEB.BUN --texture Global/GLOBALB.BUN`, and the same with
`--policy zone-only` and with `--cost prepared`:

| | retail | zone-only (not retail) | retail, prepared currency (not retail) |
| --- | ---: | ---: | ---: |
| budget | pool 21808648 | pool 21808648 | Switch arena 50331648, 48942848 for the streamer |
| start steps | 79 | 6 | 78 |
| steps | 6903 | 6830 | 6902 |
| rebuilds | 15 | 15 | 15 |
| loads, releases | 78, 0 | 91, 73 | 150, 133 |
| steps over budget | 1 | 1 | 86 |
| steps whose release call reached the limit | 0 | 0 | 1 |
| distinct resident sets | 39 | 49 | 80 |
| peak held, streamer's currency | 14760060 | 8916812 | 25934550 |
| steps measuring over the budget | not compared | not compared | 0 |
| peak resident bytes, measured | **114238870**, 78 sections | **98329802**, 21 sections | 24948886, 77 sections |
| Switch `streaming.region` 16777216 | over by 97461654 | over by 81552586 | over by 8171670 |
| Switch `streaming.arena` 50331648 | over by 63907222 | over by 47998154 | within |

The committed zones along the lap, retail, by path sample: 1003 at 0, 1002
at 67, 1001 at 571, 1104 at 1137, 1105 at 1666, 1106 at 2169, 1103 at 2436,
1201 at 3086, 1202 at 3525, 1203 at 4109, 1204 at 4507, 1205 at 5096, 1206
at 5691, and 1003 again at 6198. Every rebuild on the path finds its whole
request resident, loads nothing and settles in the same step.

What the three runs say:

- **Retail keeps the whole permanent set for the whole lap**, which is what
  [`TRACK_WHOLE.md`](TRACK_WHOLE.md) inferred and this run shows along a
  path: no route zone asks for a section outside it. The start set loads
  two sections a pass over 79 steps, and **its first pass reports one
  over-budget step by retail's own rule**: `Y0` is placed first and reads
  19561872 bytes, so `X0`'s 6338100 do not fit beside it in the
  21808648-byte pool, nothing is loaded yet to release, and `X0` waits for
  the next pass, when `Y0` holds only its 149624 kept bytes. That is why the
  pool is sized for `Y0`'s read plus its kept bytes. The 39 intermediate
  sets end at set 38, the 78 sections of #156, measured again at
  114238870 bytes.
- **zone-only** is the smallest resident set a zone-driven streamer can
  hold here, and it is still 90025992 to 98329802 bytes at every step of
  the path, because `Y0` alone is 89289984 bytes of RGBA8 texture and is
  always requested.
- **The prepared currency against the Switch arena** shows the retail rule
  under the #52 budget: `Y0`'s share, 100920576 bytes, never fits, every
  other section does (24948886 bytes measured at the peak, against 25934550
  held by the shares), and because `Y0` is requested at every rebuild and
  nothing can make room for it, **each rebuild releases every unrequested
  section before giving `Y0` up** - 133 releases and 150 loads where
  retail's own currency has none. The rule is retail's; the thrashing is
  what it does with a section it can never place. The upper-bound shares of
  review comment 3630 admit exactly the same 80 sets as the first version's
  shares did here - the `costs:` hash is unchanged - and only the held
  figures, and so the sequence hash, differ.

**So the lap does not fit the Switch streaming budget, by 63907222 bytes
against the arena and 97461654 against one region, and no residency rule
closes that alone.** Nine tenths of the resident bytes are decoded RGBA8
texture that retail uploads and drops (it keeps 14760060 bytes for the same
78 sections). Keeping the textures in GPU memory - the permanent set's
upload table needs 115492784 bytes of the 256 MiB GPU placeholder - leaves
13674390 bytes of geometry and instances on the CPU, which fits one 16 MiB
region, and the retail rule releases nothing on this lap in either case.
That is #52's decision, and this note measures it.

## Pins

Every command below only reads the installation. The frames are derived
game data and are not committed. Paths use the `X:/Games/NFSU` placeholder
of the other notes; the listings name files without their directories, so
their hashes do not depend on where the installation lives.

```text
build/nfsu_probe track-stream-path X:/Games/NFSU/Tracks/STREAML1RA.BUN \
    X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --hand-path 1003 \
    --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN \
    --texture X:/Games/NFSU/Global/INGAMEB.BUN \
    --texture X:/Games/NFSU/Global/GLOBALB.BUN

build/nfsu_probe track-stream-path X:/Games/NFSU/Tracks/STREAML1RA.BUN \
    X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --hand-path 1003 \
    --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN \
    --texture X:/Games/NFSU/Global/INGAMEB.BUN \
    --texture X:/Games/NFSU/Global/GLOBALB.BUN \
    --list none --frame-every 600 --out lap
```

The first, and the same with `--policy zone-only` and with
`--cost prepared`, each exit 0 and write nothing to standard error; with
CR LF folded to LF (`... | tr -d '\r' | sha256sum` in Git Bash):

| Run | Lines | Bytes | Listing SHA-256 |
| --- | ---: | ---: | --- |
| retail | 594 | 47010 | `9493D2EE50E38CB08F666305786A42F322C8BB1066F8DCFA7ED6381DCFC04F0C` |
| `--policy zone-only` | 721 | 55864 | `936C0D34F793C52A4FB8F50F9C787AC2E04A881AE5B9F9CFC6793DBC4B226649` |
| `--cost prepared` | 1162 | 89907 | `88EF6116A350A4D5F39924AD508476B60401EE70F693916C7A469115F73088B5` |
| the frames run | 85 | 9777 | `2D31DABED2DBE299D360840A20CA9BB7FEEDBCE6A95218B3C14B1C8706DFD44B` |

| Run | `sequence: sha256` | `costs: sha256` |
| --- | --- | --- |
| retail, 6903 steps | `F64185A1E34258C03126E86EC80DC021716D3F0FE509933F6A8B0BD4D18CB643` | `A902BC437FC931D355829DF7D28982A84C3C10FEF40B897C13AE02E4F378C681` |
| zone-only, 6830 steps | `58F66006884DA6C39AEF908EAA1FBFF64D5121CC119B302166583E97990068C4` | `C943E73DBC12309086FF898AAB2051EF246E6E475C11ACBDDD7164CA86329CAE` |
| prepared, 6902 steps | `640311156722181A8D8EB15FE06EB239C637D758596D88FFFA4F23F8BD3EE3F6` | `E0148B403448531E0C93F7B0704FB0B445C822F4061C67F07B4D8C4744F57AD9` |

All four listings and all six hashes are identical in the default
configuration, an explicit Debug build and an explicit Release build, each
run twice.

Re-pinned by the fix round for review comment 3630, and why: every listing
gained its `measured:` line (F4), and the prepared listing its new `cost:`
lines; the prepared sequence hash moved because each record's held bytes
are now the upper-bound share; the retail and zone-only sequence and costs
hashes, the prepared costs hash and every frame are exactly the first
version's. The first version's listings were `75750078...` (593 lines),
`FF95B73C...` (720), `9043623D...` (1161) and `EC3205F3...` (84), and its
prepared sequence `0B9FDB3A...`.

The second writes twelve 1280x720 frames of 3686418 bytes, at samples 0,
600, ..., 6600 - every 10 seconds of the lap - all from set 38, the 78
sections:

| Frame | Where | default and Debug (`-O0`) | Release (`-O3`), where it differs |
| --- | --- | --- | --- |
| `lap-000000` | zone 1003, the start | `FAC9DFD0DCDFEAB6C319061398840005BA7B0A5261164A0D965314936346C3EC` | `D2419529C69702CCD202F38DFF8F712B9AD3712557D9C9BBF78DD09ECA3A9F86` |
| `lap-000600` | under the bridge, 1001 | `28168F3E33EF36DB93DE8934D396BF408E14678ED95D66EB56D83752D2F0826D` | `3FC42D6C17AC58793EEA70F5BE97898F7A16F8458D8A45E3FEB9861D4FB5C603` |
| `lap-001200` | north, 1104 | `06F93C9ADC207369E59B6C0BBC70A8CE825E27BCB09197CC89B6831186C0F197` | same |
| `lap-001800` | the boulevard, 1105 | `E0A17F9F4561E8E78825C86EC293D73A03CE4955C5090FDF2DD68FEA091DD2B2` | same |
| `lap-002400` | north, 1106 | `F09E7434A0694C5025FA48264E75596299C89A715991D60589FC6E70B942F160` | `690C48053F4593F9D0769E24E315F1E124AAB8CE092F5420CBADEC2B0D9B2E44` |
| `lap-003000` | north, 1103 | `061D9C66AE8A17C7B1490BDD2609E4DE376AC22B9E9B06E6A28A1A9C3F121B7B` | `14F9E2D4F2D9BF43B7C3AB52901AAAC9CEC146CD21C20639ADA21D886CD0B980` |
| `lap-003600` | the arc, 1202 | `109244974D2932FC5F91E66EEF4CCBC8506F420FF162E6C4DAB177DE19595DC6` | same |
| `lap-004200` | the diagonal, 1203 | `D92062E1C1B24767A690F5E0B5F20F29515576D48233C8A68157796254F4EB13` | same |
| `lap-004800` | the avenue, 1204 | `43DBCFABC97E6E99A123E7938D4DB0E38664E6F952D9E1488DD0A96D87739B4A` | same |
| `lap-005400` | the western junction, 1205 | `3A607CEBB80BAF7F1D8A79B56F5C2EFF46B4ECBFDB33BE3ADD2CC37E123EA8F6` | same |
| `lap-006000` | south, 1206 | `D38E34D69ACB9D3E770E6D03D29623538DA5E24728F20A9122B782C9C7F51686` | same |
| `lap-006600` | the curve into 1003 | `15D252B0C00CBA53B81489702F431809D5EEB8B2F43ED45F0462DB4D82D70909` | same |

Each configuration ran every pin twice in separate processes, with the
outputs deleted by exact name first; every pair was byte-identical and no
run wrote to standard error. **Four frames differ in Release**, by few
pixels and within the bounds shown, which is the `-O0` against `-O3`
divergence [`TRACK_RENDER.md`](TRACK_RENDER.md) traces to `src/render.cpp`
(issue #125): `lap-000000` 61 pixels of 921600, at most 11 in any channel,
inside `x 66..738, y 321..400`; `lap-000600` 8 pixels, at most 21, in
`x 394..1172, y 259..263`; `lap-002400` 1 pixel, by 1; `lap-003000` 6
pixels, at most 19, in `x 180..227, y 261..271`. The frames run's own
listing - including every frame's triangle, pixel and fallback counts - is
the same in all three.

### What the frames show

Looked at, converted to PNG outside the repository: every one is a city
street at night from two units above the road. The start is a multi-lane
street with yellow centre hatching, a shopfront row on the right (`PORK
PRODUCTS`) and a stone building ahead; sample 600 is the road dipping under
a stone bridge with its piers either side and yellow markings; 1200 is a
wide street with chain barriers along the sidewalks and towers and a spire
ahead, 1800 one with construction fencing and traffic cones; 2400 is a
street with yellow hatching, a statue and race chevrons ahead where it
turns; 3000 and 3600 run under overhanging trees along the curve by the
circular plaza; 4200 is the diagonal avenue with its yellow hatching; 4800 the sunken avenue between retaining walls; 5400 the
open junction by the triangular building; 6000 the southern street with
red-and-white race barriers; 6600 the curve back into zone 1003 with its
yellow markings. **There are no holes and no garbage**: every road, kerb and
facade is continuous, as it should be when all 78 sections are resident. The
only non-textured pixels are the renderer's fallback checker - 0.03 to 18
percent of a frame, most of it at 4200 - on tree sprites and a few signs
whose texture is the P8 one the decoder refuses or one no loaded pack
carries, the 417 unusable and 2 unresolved draw-list entries
[`TRACK_WHOLE.md`](TRACK_WHOLE.md) lists.

## Nothing earlier moved

`src/track.cpp`, `src/track_objects.cpp`, `src/track_scene.cpp`,
`src/track_draw_list.cpp`, `src/render.cpp`, `src/track_render.cpp`,
`src/track_stream.cpp` and `src/track_zones.cpp` are not modified. Every
earlier pin was re-run on this branch, twice in each of the default
configuration, an explicit Debug build and an explicit Release build:

| Pin | default | Debug | Release |
| --- | --- | --- | --- |
| region render `D17D632B...` | same | same | same |
| street render `F691A28D...` (`-O0`) / `20CCC85C...` (`-O3`) | `F691A28D` | `F691A28D` | `20CCC85C` |
| track-probe default framing `B5C806B2...`, 4916 bytes | same | same | same |
| track-probe street `CFF94B4A...`, 4523 bytes | same | same | same |
| track-probe `--mesh` `D234ECD0...`, 5143 bytes | same | same | same |
| track-probe turned and flown `E666BC2F...`, 3139 bytes | same | same | same |
| track-budget `--no-time`: counts identical, `budget: limits 0 exceeded 0`, region bytes 182436, resident 3173846, draw calls 46, pixels 33449, frame 15667200 | same | same | same |
| the first [`TRACK.md`](TRACK.md) acceptance command: its `meshes:`, `cost:`, `textures:` and `draws:` lines | same | same | same |
| track-whole listing `8B6C2D59...`, 319 lines, 29920 bytes | same | same | same |
| track-whole overview `DD32C4BF...` | same | same | same |
| track-whole street `009D20F2...` (`-O0`) / `58140093...` (`-O3`) | `009D20F2` | `009D20F2` | `58140093` |

The track-probe hashes are of the raw standard output, CR LF and all, as
[`TRACK_PROBE.md`](TRACK_PROBE.md) pins them.

## Tests

`tests/track_residency_tests.cpp` builds its fixture as bytes and parses it
with the real parsers: a streaming table of 15 records shaped like retail's
- `--`, the fixed four with a texture-pack `Y0` that keeps a fifth of what
it reads, `Z3` for the extra list, three records with the track byte and
seven that only zones name - and a corridor of five zones along slot 0, the
fourth off the route and the fifth past a 20-unit gap. It covers, among
other things: every retail constant by value and float bit pattern; each
look-ahead point, the clamp at 100 and the speed cap on both sides, the
route refusing a look-ahead point and accepting the car's own, the gap
stopping the look-ahead, a car outside every zone looking into one, and no
route record; the start set in request order, two a pass, the deferred pass
that finds nothing, three and four a pass and the three-read limit; the
three-sample commit and the at-once first sample; **entering, leaving and
revisiting** a zone under both policies; **the release order** under a
tight budget, stamp first and table order among equals; **a step that
would exceed the budget**, which releases all it can, is reported and does
not stop the run; the release limit on both sides of its boundary; the
allocator's 0x400 and 0x58 at the byte; the 256-record stop; every refusal;
the step line and the pinned sequence hash of one corridor run; the
path format, its sampling and the hand-given path; and, from the review of
this issue, **the release order by stamp where the candidates' stamps
differ** and one release is enough (111, requested when A was entered, goes
before 101, requested when B was), **a zero-byte release** that ends the
call and gives the request up although another release would have made
room, a call releasing more than its limit, a limit of 0, the root's
exactness and the review's path. 289 checks.

`tests/track_stream_path_guards.cmake` drives the command in 63 cases:
every refusal and both sides of every bound with no input at all, then runs
over the synthetic stream `nfsu_track_stream_tests --write-fixture` writes,
out of its zone 1003 into 1009 and back: the retail listing and its hashes,
a second run identical to the first, zone-only, a budget the start set does
not fit, the prepared currency with its `measured:` line under two budgets,
the table currency's, the review's path at 402 samples, a wrong stream file
whose every section's issue is named once, `--list all` and `--list none`,
three frames, a frame that exists and a frame that names an input.

**Deliberate breaks, each applied alone to `src/track_residency.cpp`,
rebuilt, run, then restored byte for byte and touched** - 24 of them, a run
classified by what it printed (`CAUGHT` needs `FAIL:` lines and the failure
count, `SURVIVED` the pass line):

| Break | Result |
| --- | --- |
| release ties go to the last record in table order | caught, 10 checks, the first `the release order under a 2600-byte budget` |
| **the most recently requested is released first** | caught, 5, the first `the least recently requested goes, not the first in table order` - the review's first survivor |
| **a zero-byte release counts as a release** | caught, 3, `the zero-byte release ends the call, and 104 is given up` - the review's second survivor |
| a key commits on its fourth sample | caught, 5 |
| a placed car's first key waits three samples | caught, 5 |
| the track byte is requested after the first load | caught, 19 |
| a pass places three | caught, 16 |
| the car's own point needs the route too | caught, 12 |
| the look-ahead reaches across a gap | caught, 10 |
| zone-only releases nothing | caught, 2 |
| the 0x58 header is not subtracted | caught, 1 |
| the extra list is read to its capacity | caught, 29 |
| the speed cap is inclusive | caught, 1 |
| a pass that walks to the end is still deferred | caught, 5 |
| the release limit is inclusive | caught, 4 |
| the request stops at 257 | caught, 3 |
| the root truncates instead of rounding | caught, 6, the first the review's length by its bit pattern |
| the root skips evening an odd exponent | caught, 18 |
| the float root is scaled by one part in ten million | caught, 5 |
| the sample count's ceiling is off by one | caught, 1, `402 samples in every build` |
| the distance clamp is inclusive | survives, equivalent: the only float32 speed with 1.5 x speed equal to 100 is 66.666664 (0x42855555), and there 100 / speed is exactly 1.5, so both branches give the same point |
| a stamp is written only for a new request | survives, equivalent: a record requested more than once in one build gets the same tick either way, and one marked past the 256th stays requested for good, so it is never a release candidate and its stamp is never read |
| placed records are not freed before a release | survives, equivalent: 0x00444780 chooses among loaded records only, so placed ones cannot change its choice or what it frees, and the model frees them before the next pass anyway |
| the root rounds up on every half, ignoring ties to even | survives, equivalent: a root is never exactly halfway between two doubles - the remainder is zero only when the 54-bit root is exact, and then its lowest bit is zero, because `m * 2^54` with `m` below `2^54` has an even number of trailing zero bits and at least 54 of them |

The last four are kept as retail, or the standard, spells them rather than
tidied. The suite passed after every restore. The review's own sweep of 36
mutants found the same three equivalent survivors, two more it judged
equivalent (a zone keyed 0, which no retail zone is, and the rebuild's own
unplace, which the pass repeats), and the two this round closed.

Two more breaks, outside that file: the console census fails when
`cmake/residency_libraries.cmake` asks for `-ffp-contract=fast` (10 fused
instructions, above), and the probe's `measured:` check reports all 2580
over-budget steps of `--budget 12000000` when the shares are put back to
the first version's (F4, above). Each file was restored byte for byte,
compared with `cmp`, and touched.

## Observed

- The streamer's two slots are filled every frame from player records 1
  and 2 (0x0048E985..0x0048E9FE); a single-player race has one live slot.
- 0x00444870 samples the car and two look-ahead points at 0.5 and 1.0 of
  1.5 seconds of its velocity, clamped to 100 units and zero above 178.8109;
  above 17.88109 it asks the path structure at 0x007372B0 first.
- 0x00444FB0 commits a new key on its third consecutive sample, at once
  after a count of -1; 0x00446F9E forces a rebuild and resets both counts
  when the zones load.
- 0x00444D80 places at most two records a pass, each when its byte count
  plus 0x400 fits the largest free block less 0x58; 0x00444EE0 releases
  loaded unrequested records least recently requested first, first in table
  order among equals, stops once a call has released more than 0x9C3C00
  bytes - tested before each release, so one call can release that plus one
  record - and gives a record up when nothing is left, or when a release
  frees no bytes: 0x00444830 is `xor eax, eax` / `ret 4`.
- The request stamp 0x006F0888 is a 60 Hz tick.
- Along the hand-given 1003 path, the retail rule rebuilds 15 times - the
  forced start and 14 zone commits - and loads and releases nothing after
  the 78-section start set, whose first pass cannot place `X0` beside
  `Y0`'s read.

## Inferred

- A loaded section occupies its kept bytes, not its read bytes, from the
  pool 0x00444AF0 sizes; the code that shrinks it was not traced.
- The 1.5 and 100 are seconds and metres, and 178.8109 and 17.88109 are 400
  and 40 mph, from their values.

## Unknown

- The path structure 0x00444870 asks first at race speed, and so whether
  retail's key ever differs from the polygons' along the real route.
- The real route itself (#61); the path here is hand-given.
- How long a read takes, which decides how many frames a zone change takes
  to load and whether a rebuild ever waits on reads in flight.
- Whether the car's slot is live while the start set loads.
- What sets 0x00736138, which also forces a rebuild (0x0044658B).
- Where the loader shrinks a texture-pack section to its kept bytes.
- The frame rate, which ties the three-sample rule to time.
- What fills player records 1 and 2, and so who the second slot tracks.
- How 0x00565B60 allocates (flags 0x2011): its per-block overhead and
  alignment, which the model does not charge.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`.
