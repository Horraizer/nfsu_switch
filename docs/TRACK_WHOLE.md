# The whole track: every section one race loads

Clean-room notes on which sections of a track's `STREAM*.BUN` Need for Speed:
Underground (PC) loads for a race, and what loading all of them costs.
Everything here comes from reading a user-provided retail installation and
its executable read-only. No game data is stored in this repository: this
note holds offsets, counts, hashes and field meanings only.

[`TRACK.md`](TRACK.md), [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) and the notes
after them decode one explicitly bounded region at a time, and every one of
them lists "which sections are resident together" as unknown. This note
closes that for issue #156 and the first-lap target of #159: race 1 of
Underground mode is event 1, a circuit on track 1003 in stream region `L1RA`
([`RACE_EVENTS.md`](RACE_EVENTS.md)).

The answer first:

- **The table is in the track's own `TRACKB####.LZC`.** Chunk `0x00034107`
  is the streaming table of the whole `STREAM*.BUN`: one 56-byte record per
  streaming section, with its name, section number, file, byte range, the
  bytes it keeps once loaded, and one byte that marks the sections this
  track keeps. `TRACKB1003.LZC` holds 401 records; 400 of them tile
  `STREAML1RA.BUN` exactly, from offset 0 to its last byte.
- **The loader's rule is recovered** (0x00444B50, below): the four fixed
  numbers 2500, 2400, 2600 and 2499, the `0x00034154` list keyed on the
  track, every record whose track byte is set, and the lists of the zones
  two tracked positions stand in. Before any position is known the zone
  part is empty, so a race starts with exactly the first three parts
  resident. For track 1003 that is **78 sections**.
- **The zones along the route add nothing to those 78.** The `0x00034153`
  record keyed on 1003 names the 13 zones the loader lets a car's look-ahead
  enter; their lists name 119 section numbers, 73 of which have a record, and
  **all 73 are in the set already**. Over every track of the installation
  that has such a record, the track bytes are exactly the records those
  zones name - 28 tracks of 28.
- **`STREAML1RA_1003.BUN` contributes nothing**: it is 0 bytes on disk, the
  table declares it as 0 bytes, and no record names it.
  **`TEX1003TRACK.BIN`** is loaded whole beside the stream and supplies the
  sky and the track map. **`TRACKB1003.LZC`** supplies the table, the zones,
  the two lists and the collision ([`TRACK_COLLISION.md`](TRACK_COLLISION.md)).
- **Totals for L1RA/1003** (the permanent set, loaded by `nfsu_probe
  track-whole`): 78 sections, 36471524 bytes read, 2095 meshes, 457564
  vertices, 308169 triangles, 5880 instances of which 5860 place a mesh,
  100564480 bytes of RGBA8 texture, and **114238870 resident bytes** against
  the Switch plan's 16777216-byte region placeholder and 50331648-byte
  streaming arena: over both, by 97461654 and 63907222 bytes. Every desktop
  ceiling of #52 holds.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |
| `Tracks/STREAML1RA_1003.BUN` | 0 | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |
| `Tracks/TEX1003TRACK.BIN` | 1086216 | `081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5` |
| `Global/INGAMEB.BUN` | 1670080 | `044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |

`TRACKB1003.lzc` is JDLZ; its image is 961328 bytes and is what every offset
into it below refers to. `Global/INGAMEB.BUN` is byte for byte the image of
`Global/InGameB.lzc`, the file the game loads (string 0x006C9C78
`GLOBAL\INGAMEB.LZC`, pushed at 0x0044933C); `Global/GLOBALB.BUN` is the
retail image of `GLOBALB.LZC` for the reason [`RACE_EVENTS.md`](RACE_EVENTS.md)
gives - this installation's `Global/GlobalB.lzc` is a 2025 modification and
is not used here.

Executable addresses are `speed.exe` virtual addresses; the image base is
0x00400000 and `.text`, `.rdata` and `.data` all map with
`file_offset = VA - 0x400000`.

## The streaming table: `0x00034107`

The chunk handler at 0x004442A0 recognises two chunks of the `TRACKB` image
and keeps them in the streamer object at 0x0077A980:

```text
004442A6  cmp  ecx, 0x34107
004442AE  lea  ecx, [eax + 8]                ; the payload
004442B1  mov  dword ptr [0x77a980], ecx     ;   the record array
004442BA  mov  eax, 0x24924925               ; size / 7 ...
004442C1  sub  ecx, edx
004442C3  shr  ecx, 1
004442C5  add  ecx, edx
004442C7  shr  ecx, 5                        ; ... / 8: size / 56
004442CA  mov  dword ptr [0x77a984], ecx     ;   the record count
004442D6  cmp  ecx, 0x34108
004442E1  mov  dword ptr [0x77a988], eax     ; three words, below
```

`0x24924925` and the shift pair are a division by 7, and the final shift of
5 divides by 8 more, so a record is **56 bytes**; the code fixes the size,
not the data. Every record the loader touches is then addressed at a stride
of 0x38 (0x004447B9, 0x00444B3F, 0x00444CC0, 0x00444D37).

| Offset | Type | Field | Evidence |
| --- | --- | --- | --- |
| +0 | char[8] | name, NUL-terminated: `Z17`, `Y0`, `J3` | read by no traced instruction; it spells the section's letter and number |
| +8 | s16 | section number | 0x00444D30 `cmp word ptr [eax + 8], dx` against every requested number |
| +10 | u8 | **the track byte** | 0x00444CB0 `mov dl, byte ptr [eax + 2]` (with `eax` at +8): a record whose byte is not zero is requested |
| +11 | u8 | requested, written by the loader | 0x00444D4A reads it, 0x00444D51 sets it |
| +12 | u32 | load state, written by the loader | 0x00444DFB writes 1, 0x004446FA 2, 0x00444747 3 |
| +16 | u32 | file index | 0x004446EA..0x004446F3: the file name at streamer `+0x14 + 64 * index` |
| +20 | u32 | byte offset in that file | 0x004446E6, pushed to the read request |
| +24 | u32 | byte count read | 0x004446E3, pushed to the read request |
| +28 | u32 | bytes kept once loaded | 0x00444B2B, summed for the pool below |
| +32 | u32 | unknown | read by no traced instruction |
| +36..+55 | | written by the loader | the request frame (0x00444D47), the read request (0x00444712), the memory (0x00444DF8), two words (0x00444E10, 0x00444E29) |

**The file index names one of three file names.** 0x004445C0 copies the
stream name `TRACKS\STREAM%s.BUN` into the streamer at `+0x54` and the pack
name `TRACKS\STREAM%s_%d.BUN` at `+0x94` (both formatted at 0x0044970D and
0x0044972C and passed in at 0x0044974D), so index 1 is the stream and
index 2 the per-track pack. What index 0 names was not traced.

**`0x00034108` is three file sizes.** It is 12 bytes, and over all 31
`TRACKB` files its first word equals the byte count of the table's file-0
record, its second the size of the track's `STREAM<region>.BUN` and its
third the size of its `STREAM<region>_<number>.BUN`: `TRACKB1003` holds
`79328 92394688 0`, `STREAML1RA.BUN` is 92394688 bytes and
`STREAML1RA_1003.BUN` 0. The loader keeps a pointer to it (0x004442E1) and
no reader of that pointer was traced.

### The table against the data

Measured over all 31 `TRACKB*.lzc` files, **7807 records**, without one
exception:

- every name is printable ASCII, NUL-terminated, with zero bytes after it;
- every byte the loader writes (+11, +12..+15, +36..+55) is zero;
- file indices are 0, 1 and 2 only - 31, 7761 and 15 records - and the only
  file-0 record of each table is its first, named `--`, number 0, and never
  flagged;
- every track byte is 0 or 1 (5468 and 2339);
- no two records of a table share a number;
- no record keeps more than it reads, and 5178 keep exactly what they read;
- every range lies inside the size `0x00034108` declares for its file, and
  every offset is a multiple of 2048.

For `TRACKB1003`, the 400 stream records are in file order and **tile
`STREAML1RA.BUN`**: each starts on a top-level chunk and ends on a chunk
end, the gaps between them are under 2048 bytes and the last ends at byte
92394688. The table is **the same for all 16 `L1RA` tracks** except for the
track byte: with that byte left out, the SHA-256 of the 401 records' other
fields is identical across `TRACKB1001` to `1008` and `1102` to `1109`.

A record that keeps less than it reads carries a texture pack: `Y0`
(number 2500) reads 19561872 bytes and keeps 149624. `TRACKB1210`, an
`L1RB` track, places its `Y0` in the pack file instead (index 2, the whole
8206776 bytes of `STREAML1RB_1210.BUN`), which is what the pack file is for;
`L1RA` keeps it in the stream, and so `STREAML1RA_1003.BUN` is empty.

## Which sections the loader requests: 0x00444B50

0x00444B50 builds the list of section numbers the streamer wants resident.
It runs from the streamer's update (0x0044526F) and appends, in this order,
into a buffer of int16 numbers:

```text
00444B6E  mov word ptr [esp + 0x18], 0x9c4   ; 2500
00444B75  mov word ptr [esp + 0x1a], 0x960   ; 2400
00444B7C  mov word ptr [esp + 0x1c], 0xa28   ; 2600
00444B83  mov word ptr [esp + 0x1e], 0x9c3   ; 2499
00444B90  movsx ecx, word ptr [eax + 0x6c]   ; the current track's number
00444BA4  cmp  dword ptr [eax + 8], ecx      ; 0x00034154 records (list 5)
00444BB4  mov  edx, dword ptr [eax + 0xc]    ;   the first that matches:
00444BC6  rep movsd                          ;   its count of entries
00444BD6  mov  eax, dword ptr [0x73612c]     ; a debug section, below
00444BF2  mov  eax, dword ptr [ebx + 0x590]  ; a forced list, below
00444C16  lea  eax, [ebx + 0xe6]             ; two tracked slots:
00444C48  cmp  word ptr [eax + 8], cx        ;   the zone keyed on the slot's
00444C6F  rep movsd                          ;   zone key, and all its list
00444C94  mov  al, byte ptr [ebx + 0x178]    ; unless this byte is set,
00444CB0  mov  dl, byte ptr [eax + 2]        ;   every record whose track
00444CBA  mov  word ptr [esp + edi*2 + 0x18], dx ; byte is set, table order
```

Then 0x00444D0E..0x00444D73 resolves the list: each number is looked up by
+8 (0x00444D30), the first record that matches is marked requested
(0x00444D51) and appended to the load list at streamer `+0x180`, a record
already marked is skipped, a number no record carries is skipped, and the
walk stops once 256 records are listed (0x00444D5B `cmp ecx, 0x100`).
0x00444D80 then reads each listed section while its byte count plus 0x400
fits the pool's free space (0x00444DD4..0x00444DDF).

What each part contributes to a race:

| Part | What it is | For a race |
| --- | --- | --- |
| the four fixed numbers | 2500 (`Y0`, the shared texture pack), 2400 (`X0`, the shared mesh library), 2600 (`Z0`) and 2499 (`X99`) | always |
| `0x00034154` | the first record keyed on the track number; for 1003 its one entry is 2603 (`Z3`) | always |
| the debug section | appended when 0x0073612C and 0x00736130 are both set. Both lie in zero-initialised memory past the file's `.data`, 0x0073612C is written only as zero (0x00445D7F) and 0x00736130 is written by no instruction that names it; the one string beside that write is `Seeulator` | never, as traced |
| the forced list at `+0x590` | filled only by 0x004444F0, whose callers are the front end's 0x0047BA47..0x0047BABC with section 2300; cleared by 0x00444590 from 0x0042228D and 0x0042424B | not in a race |
| the two zone slots | the key of the zone each tracked position stands in, and the zone's own list. 0x004443FD..0x00444404 set both keys to -1 whenever the streamer is reset, so the part is empty until a position is known | position-dependent |
| the track byte | every record whose +10 byte is set; the byte at `+0x178` is written only at 0x00444365 and 0x0044463C, both times as zero | always |

A last step, 0x00444CCA..0x00444D0A, adds 50 to every `Y` number below 2550
when 0x0078A320 is 2. Its other readers suggest a player count - 0x00456BBD
reads the two records at 0x007361B4 and 0x007361B8 when it is 2 - but that
reading is not traced further. No
`L1RA` record is numbered 2550 or above, so for this stream the step could
only drop sections, never add them, and this note loads the single-player
set.

**So the permanent set of a track is the four fixed numbers, its
`0x00034154` list and its track bytes**, and it is resident from the start
of a race: the streamer is initialised at 0x0044974D, once
`TRACKB%d.LZC` has loaded (the callback 0x004496A0 is registered at
0x004497F6), with both zone keys at -1.

### The pool the loader sizes for it: 0x00444AF0

```text
00444B05  mov  cl, byte ptr [edx + 2]        ; the track byte, or
00444B0C  mov  cx, word ptr [edx]            ;   one of 2500 2400 2600 2499
00444B2B  mov  ecx, dword ptr [edx + 0x14]   ; kept
00444B2E  mov  esi, dword ptr [edx + 0x10]   ; read
00444B31  add  edi, ecx                      ; sum of kept
00444B37  add  ecx, esi                      ; read + kept, for a record
00444B3D  mov  eax, ecx                      ;   that keeps less: the largest
00444B4B  mov  eax, edi                      ; the larger of the two
004499A0  add  eax, 0x200000                 ; plus 2 MiB
004499AA  call 0x444480                      ; allocate the pool
```

The pool holds every permanent section once loaded, or the one section
whose load peak is highest while its pixels are still in memory, whichever
is larger, and 2 MiB beside it - read here as the room for what a position
adds, which is not traced. For track
1003: the kept bytes sum to 14750908, `Y0`'s peak is 19561872 + 149624 =
19711496, and the pool is **21808648 bytes**. `nfsu::track_stream_pool_bytes`
is that formula.

### The route: 0x00444870 and `0x00034153`

The zone key of a tracked slot comes from 0x00444870, which samples the
slot's position and two look-ahead points along its velocity. It asks a
path structure at 0x007372B0 first (0x0044495C) and the zone polygons
second (0x00444A17). A look-ahead point's zone is accepted only when the
`0x00034153` record the streamer keeps at `+0x598` names its key:

```text
00444A20  test ebp, ebp                      ; the slot's own point: any zone
00444A28  mov  edx, dword ptr [ecx + 0x598]  ; look-ahead: the track's list
00444A32  mov  esi, dword ptr [edx + 0xc]    ;   its count
00444A42  cmp  word ptr [edx], bp            ;   does it name this key?
```

0x004446A1..0x004446CB stores at `+0x598` the first `0x00034153` record keyed
on the current track's number. That record answers
[`TRACK_ZONES.md`](TRACK_ZONES.md)'s open question about `0x00034153`: it is
the set of zones the loader expects the track's route to cross. For 1003 it
names 13: `1202 1203 1204 1205 1206 1003 1002 1001 1104 1105 1106 1103 1201`.

**Measured: the track byte is exactly what those zones can ask for.** For
every track whose `TRACKB` file has a `0x00034153` record keyed on itself -
28 of the 31; `1099` has no such record and no track byte set, and `1202`
and `1207` carry track bytes with no such record - the records named by the
union of those zones' lists are exactly the records whose track byte is
set: 125 of 125 for 1001, 73 of 73 for 1003, 203 of 203 for 1105, 3 or 6
for each drift track, and so on, with nothing missing on either side in any
of the 28. For 1003 the 13 zones' lists name 119 section numbers; 73 carry
a record, and all 73 are flagged. Of the other 46, 27 are among the 85
small scenery sections that live inside `X99` - which is always resident -
rather than in records of their own, and 19 are numbers no section of the
stream holds.

That is why the permanent set is the whole track and not a subset of it:
the exporter marked every section the route's zones can request, and the
loader keeps all of them from the start. A zone off the route can still
request more, and those land in the 2 MiB of headroom.

### What is not decoded, and the bounds that stand in for it

What a tracked position adds depends on where the car is, and that is the
path structure at 0x007372B0 and the route itself, neither of which is
decoded here (the `0x00034121`..`0x00034134` chunks of the `TRACKB` image
are the likely source and belong to #61). The measurement above says that
for a car on the route of 1003 the answer is nothing, and that is an
inference from the data plus the look-ahead rule, not a trace of the path
code. So `track-whole` loads two bounds beside the permanent set:

| `--set` | What it loads | Retail rule? |
| --- | --- | --- |
| `permanent` | the three always-requested parts above, resolved as 0x00444D0E does | yes |
| `zones` | that, plus every section any zone of the file lists | no: what a position anywhere on the map could ask for |
| `stream` | that, plus every other record of the stream or the pack file | no: the outer bound |

## Loading them: `nfsu::load_track_stream_sections`

`include/nfsu/track_stream.h` and `src/track_stream.cpp`.

- `parse_track_stream_table` decodes `0x00034107` and `0x00034108` from the
  decompressed image and refuses, with the record's offset, a name that is
  not printable or not terminated, a non-zero loader-written byte, a file
  index above 2, a record that keeps more than it reads, a range past its
  declared file size and a number two records share. Every rule holds for
  all 7807 retail records.
- `select_track_stream_sections` applies the rule above for one set and one
  track number, in the loader's order, with its 256-record stop for the
  permanent set, and also runs the route check.
- `load_track_stream_sections` reads each selected record's byte range and
  nothing else - one seek and one read per record - refuses a section whose
  offset is not a multiple of 128 (every retail one is a multiple of 2048),
  walks its top-level chunks, and parses its geometry roots with
  `parse_track_region`, its scenery with `parse_track_objects` and its
  texture packs with `parse_texture_pack_set`. It then merges every loaded
  section's meshes and scenery sections, renumbered and in selection order,
  into one `TrackRegionReport` and one `TrackObjectReport`, whose offsets
  stay absolute file offsets.
- `nfsu::build_track_scene` places that pair as it places one region. The
  difference is the point of the issue: an instance in one section now
  resolves a mesh in another. `X0` holds 1377 meshes and places nothing;
  the track's own sections place them.
- Texture sources are the packs of the loaded sections in load order, then
  the files named with `--texture` in the order given; the earlier source
  wins a shared hash, as for every track command
  ([`TRACK.md`](TRACK.md), "Texture sources for a region"). The loader's own
  lookup order across packs is not decoded, so the order is this project's.

## The totals for L1RA/1003

`nfsu_probe track-whole Tracks/STREAML1RA.BUN Tracks/TRACKB1003.lzc 1003
--texture Tracks/TEX1003TRACK.BIN --texture Global/INGAMEB.BUN --texture
Global/GLOBALB.BUN`, the permanent set:

| Part | Sections | Bytes read | Triangles | Instances | Placed | RGBA8 texture bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `Y0` 2500, fixed | 1 | 19561872 | 0 | 0 | 0 | 89289984 |
| `X0` 2400, fixed | 1 | 6338100 | 131716 | 0 | 0 | 0 |
| `Z0` 2600, fixed | 1 | 15456 | 720 | 1 | 0 | 0 |
| `X99` 2499, fixed | 1 | 51840 | 88 | 250 | 249 | 0 |
| `Z3` 2603, `0x00034154` | 1 | 9152 | 28 | 111 | 111 | 0 |
| `J` 10xx, track byte | 21 | 3192568 | 57008 | 1862 | 1861 | 2361344 |
| `K` 11xx, track byte | 16 | 2314860 | 39905 | 1456 | 1452 | 1822720 |
| `L` 12xx, track byte | 28 | 3877288 | 70952 | 2108 | 2095 | 3047424 |
| `M` 13xx, track byte | 4 | 429556 | 7752 | 92 | 92 | 360448 |
| `Y` 25xx, track byte | 4 | 680832 | 0 | 0 | 0 | 2392064 |
| **78 sections** | **78** | **36471524** | **308169** | **5880** | **5860** | **99273984** |

The three texture files add 15 retained textures and 1290496 bytes:
`TEX1003TRACK.BIN` 2 (524288 bytes), `INGAMEB.BUN` 11 (753920) and
`GLOBALB.BUN` 2 (12288), which brings the scene to 100564480.

| | permanent | zones | stream |
| --- | ---: | ---: | ---: |
| sections | 78 | 348 | 400 |
| bytes read | 36471524 | 79116308 | 92012052 |
| bytes the table says they keep | 14760060 | 47817620 | 59312732 |
| meshes (valid, empty, refused) | 2095 (2039, 56, 0) | 4870 (4813, 57, 0) | 5066 (4863, 57, 146) |
| vertices | 457564 | 1539210 | 1539410 |
| triangles | 308169 | 1011140 | 1011240 |
| scenery sections | 156 | 407 | 423 |
| instances, placed | 5880, 5860 | 18190, 18164 | 21461, 21435 |
| texture bytes, RGBA8 | 100564480 | 145019648 | 145019648 |
| draw-list entries, all visible | 14946 | 46918 | 52662 |
| triangles drawn, all visible | 656347 | 2216301 | 2383977 |
| texture binds, material-group order | 2825 | 9744 | 9744 |
| resident bytes | **114238870** | 190643144 | 191119568 |

`resident bytes` is `TrackBudgetCost::prepare_resident_bytes` of
[`TRACK_BUDGET.md`](TRACK_BUDGET.md): vertex, index, RGBA8 texture and
instance bytes of the prepared scene. The stream set's 146 refused meshes
all sit in the `W` sections (23xx) and all select the 60-byte crowd layout
the 24-byte decoder refuses by name ([`TRACK.md`](TRACK.md)); no section of
the permanent set holds one, so its run exits 0 and the stream set's exits
3.

The permanent set reads 36471524 bytes and keeps 14760060 by the table's
own count; this project keeps 114238870, because it holds every texture as
RGBA8 where the game uploads its compressed pixels and drops them
(19561872 bytes of `Y0` become 149624).

What does not resolve, all of it listed by the command:

- **10 mesh hashes, 20 instances.** One is `SKYDOME` (0xBE43EDBB), which the
  loader skips. The other nine are placed by the permanent set and carried by
  no record of `STREAML1RA.BUN` at all: the stream set still leaves all ten
  unplaced, and five more of its own.
- **48 texture hashes**, of which **2 are drawn**, one draw-list entry each:
  `0x2FD2B696` and `0x60CD8020`, whose only copies are in `Y8` and in
  `Y4`/`Y5`, sections neither the permanent set nor the route's zones
  request. The other 46 name only meshes no placed instance draws.
- **417 entries are `unusable`**: `0x3394FE62`, `HEADLIGHTGLOW` in
  `GLOBALB.BUN`, is a P8 texture, and the decode path refuses P8
  ([`GPU_UPLOAD.md`](GPU_UPLOAD.md)). Without `INGAMEB.BUN` and `GLOBALB.BUN`
  15 more hashes are unresolved, 11 of them drawn: 808 unresolved entries
  instead of 2. The game loads both files for a race - `INGAMEB` at
  0x0044933C, `GLOBALB` at 0x0044865F - which is why the pinned commands
  name them.

### Against the budgets of #52

The permanent set, from the same run. Desktop rows compare against what the
default one-region preparation accepts; Switch rows against
`switch_memory_budget()`, whose values are the placeholders of
[`MEMORY_BUDGETS.md`](MEMORY_BUDGETS.md).

| Platform | Row | Value | Ceiling | |
| --- | --- | ---: | ---: | --- |
| desktop | `region.upload` | 113395030 | 260046848 | within |
| desktop | `region.vertices` | 457564 | 4194304 | within |
| desktop | `region.triangles` | 308169 | 4194304 | within |
| desktop | `region.draws` | 6424 | 65536 | within |
| desktop | `region.drawn_triangles` | 308169 | 16777216 | within |
| desktop | `region.texture_bytes` | 100564480 | 134217728 | within |
| desktop | `textures.decoded_bytes` | 100564480 | 134217728 | within |
| desktop | `frame.draw_list_bytes` | 119568 | 33554432 | within |
| switch | `streaming.region` | 114238870 | 16777216 | **over by 97461654** |
| switch | `streaming.arena` | 114238870 | 50331648 | **over by 63907222** |
| switch | `gpu.arena` | 115492784 | 268435456 | within |
| switch | `transient.arena` | 119568 | 1048576 | within |

`gpu.arena` is the scene's upload table placed by `upload_table_requirement`
with the linear layout, which is a lower bound: 12831152 bytes of buffers,
100564480 of images and one 2097152-byte staging copy. The zones and stream
sets exceed the desktop 128 MiB texture limit (145019648 bytes), which is
why `track-whole` raises that one limit to 1 GiB for its own preparation;
every other limit is the default.

**What that means for the Switch.** The whole track fits the GPU placeholder
with room to spare, and does not fit the streaming placeholders: it is about
seven times one region's and a little over twice the whole arena's. Nearly
nine tenths of the resident bytes are RGBA8 texture
(100564480 of 114238870), and the game's own figure for the same sections
is 14760060 kept bytes in a 21808648-byte pool. The placeholder is not the
problem so much as the decision to keep textures decoded on the CPU: a
Switch path that kept only the geometry and the instances resident on the
CPU (13674390 bytes) would sit inside one 16 MiB region. That is #157's and
#52's decision to make, and this note only measures it.

Wall-clock, for scale only ([`TRACK_BUDGET.md`](TRACK_BUDGET.md) explains
why no time is pinned): a Release run of the permanent listing, which reads,
parses, decodes, prepares and costs but does not render, took 163 to 191 ms
over five runs on this machine; the zones and stream sets 316 to 365 ms.

## The two frames

Both are CPU reference renders of the permanent set's whole draw list - every
placed instance, slot 0, material-group order - through
`nfsu::render_track_draw_list`, the renderer [`TRACK_RENDER.md`](TRACK_RENDER.md)
describes. They are derived game data and are not committed; the commands
and hashes are.

```text
build/nfsu_probe track-whole X:/Games/NFSU/Tracks/STREAML1RA.BUN \
    X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 \
    --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN \
    --texture X:/Games/NFSU/Global/INGAMEB.BUN \
    --texture X:/Games/NFSU/Global/GLOBALB.BUN \
    --list 0 --out overview.tga --view-pitch 89 --view-yaw 0 --view-margin 1

build/nfsu_probe track-whole X:/Games/NFSU/Tracks/STREAML1RA.BUN \
    X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 \
    --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN \
    --texture X:/Games/NFSU/Global/INGAMEB.BUN \
    --texture X:/Games/NFSU/Global/GLOBALB.BUN \
    --list 0 --out street.tga --view-target 919 62 216 --view-distance 30 \
    --view-yaw 0 --view-pitch 10 --view-fov 60 --view-near 0.5 --view-far 3000
```

| Frame | Configuration | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| overview | default, Debug and Release | 3686418 | `DD32C4BF685C79F630EABF6EBD4F500933A901BBA7249D1D19D826C99192C9A0` |
| street | default and Debug (`-O0`) | 3686418 | `009D20F2DFC5F2D17289E3AAEA150B644A71F16089AFC2CA08279DF16B134B6D` |
| street | Release (`-O3`) | 3686418 | `58140093C341138E086D3E7ADD6B0F56947E75854FD41AE5EB3482288BD3407D` |

Each was rendered twice in separate processes in each of the three
configurations, with the output deleted by exact name first; every pair was
byte-identical and no run wrote to standard error.

**The overview** is the framed camera straight down (pitch 89): the view
frames the bounding sphere of everything drawn, 3959 by 355 by 3164 world
units, so the circuit - a closed loop of streets around several blocks,
with a park beside it - sits right of centre, and the pieces scattered
around it are the always-resident `X99` scenery and `X0` placements across
the city.
21675 pixels are drawn, 46 of them fallback.

**The street view** stands on the route: the target, world `919 62 216`, is
raw slot `px 919, pz -216`, the mean of the 16 vertices of the zone polygon
keyed on 1003, 2.7 units above the static collision's ground there (59.3052,
from `nfsu_probe track-collision Tracks/TRACKB1003.lzc 919 -216`), and the
eye is 30 units from it along +x, 10 degrees up. It shows a multi-lane street with yellow centre
markings, a row of shopfronts and the towers behind them. 912050 pixels are
drawn, none of them fallback; 252355 of the 656347 submitted triangles
rasterize and 402656 are outside the frustum.

**Why the street view has two hashes.** The Debug and Release frames differ
in **6 pixels of 921600**, all inside `x 906..941, y 261..292`, by at most 5
in any channel. That is the divergence [`TRACK_RENDER.md`](TRACK_RENDER.md)
records for its own street view and traces to `src/render.cpp` at `-O0`
against `-O3` (issue #125): geometry hundreds of units from the origin, at a
grazing angle. The overview, whose camera is far above everything, is
identical in all three.

The command's own listing is pinned as well: the permanent run above
without `--out` exits 0, writes nothing to standard error and prints 319
lines; with CR LF folded to LF they are 29920 bytes with SHA-256
`8B6C2D59A9B699C8F48FE9938D41146D7F7690C7ABD7C47B8678F25C8DD864F7`,
identical in all three configurations (`... | tr -d '\r' | sha256sum` in
Git Bash). The listing names its files without their directories, so the
hash does not depend on where the installation lives.

## Nothing M2 pinned has moved

Every pin of [`TRACK_RENDER.md`](TRACK_RENDER.md),
[`TRACK_PROBE.md`](TRACK_PROBE.md) and [`TRACK_BUDGET.md`](TRACK_BUDGET.md)
was re-run on this branch in the default configuration, an explicit Debug
build and an explicit Release build:

| Pin | default | Debug | Release |
| --- | --- | --- | --- |
| region render `D17D632B...` | same | same | same |
| street render `F691A28D...` (`-O0`) / `20CCC85C...` (`-O3`) | `F691A28D` | `F691A28D` | `20CCC85C` |
| track-probe default framing `B5C806B2...`, 4916 bytes | same | same | same |
| track-probe street `CFF94B4A...`, 4523 bytes | same | same | same |
| track-probe `--mesh` `D234ECD0...`, 5143 bytes | same | same | same |
| track-probe turned and flown `E666BC2F...`, 3139 bytes | same | same | same |
| track-budget: counts identical, `budget: limits 11 exceeded 0` | same | same | same |
| track-budget counts: region bytes 182436, resident 3173846, draw calls 46, pixels 33449, frame 15667200 | same | same | same |
| track-region summary lines of [`TRACK.md`](TRACK.md) | same | same | same |

`src/track.cpp`, `src/track_objects.cpp`, `src/track_scene.cpp`,
`src/track_draw_list.cpp`, `src/render.cpp` and `src/track_render.cpp` are
not modified: the whole-track layer calls them.

## Tests

`tests/track_stream_tests.cpp` builds every fixture as bytes and parses it
with the real parsers: a `TRACKB` image carrying a nine-record streaming
table, its file sizes and a zone container with two polygons, two zones and
both kinds of section list, and a stream of eight sections at 2048-byte
boundaries whose gaps are filled with bytes that are not a chunk, so a
loader that read outside a selected record would refuse what it read. Its
sections mirror the retail shape: a texture-only `Y0`, a mesh library `X0`
that places nothing, `Z0` and `X99` scenery that place `X0`'s meshes, a
`0x00034154` section, the flagged track section, a section only a zone
lists, and a `W1` section whose crowd mesh the decoder refuses.

It pins, among other things: every record field at its offset; each refusal
at its offset, and the accepting side of each boundary; the loader's order
and reasons; that the `0x00034154` and `0x00034153` lists are read to their
count and not their capacity (the fixture writes a real section number just
past each count); the first list keyed on the track winning; the 256-record
stop at 255, 256 and 300; the pool formula on both of its branches; the
merge's renumbering and cost sums; that the whole scene places instances
across sections while the track section alone places one of its four; the
per-section costs; and every budget row, at and one past its ceiling.
379 checks.

`tests/track_whole_guards.cmake` drives the command in 56 cases: every
option refusal and both sides of every bound with no input at all, then the
listing, the three sets, other tracks' lists, missing files and two rendered
frames over the fixture the unit tests write with `--write-fixture`. The
whole fixture listing is pinned by SHA-256.

**Deliberate breaks, each applied alone to `src/track_stream.cpp`, rebuilt,
run, then restored byte for byte and touched:**

| Break | Result |
| --- | --- |
| read each `0x00034154` list to its capacity, not its count | caught: 18 checks, the first `the permanent set in the loader's order: Y0 X0 Z0 X99 Z3 W1 -- J3` - the slot past the count requests 2301 and the empty slots request 0, the `--` record |
| drop the largest-section term of the pool | caught: 2 checks |
| keep each section's own mesh numbering in the merge | caught: 17 checks, including `the whole scene builds` |
| accept a section at any offset | caught: 2 checks |

After the restore the file's SHA-256 matched the original and the suite
passed, 379 checks. The first break is the one that matters on retail
data: `TrackZoneSectionList` carries a list's full capacity, and reading it
past the count is exactly how an earlier draft of this layer requested
section 0.

## Observed

- `TRACKB####.LZC` chunk `0x00034107` is a table of 56-byte records naming
  every streaming section of the track's stream and pack files; `0x00034108`
  is the three files' sizes. For `L1RA` the table is the same for all 16
  tracks except for the track byte at +10.
- 0x00444B50 requests, in order, 2500, 2400, 2600 and 2499, the first
  `0x00034154` list keyed on the track, the lists of the zones two slots
  stand in, and every record whose track byte is set; 0x00444D0E resolves
  them by number and stops at 256.
- 0x00444AF0 sizes the streaming pool from the flagged and fixed records,
  and 0x004499A0 adds 2 MiB: 21808648 bytes for track 1003.
- For every one of the 28 tracks with a `0x00034153` record keyed on itself,
  the flagged records are exactly the records its zones' lists name.
- `STREAML1RA_1003.BUN` is 0 bytes and named by no record.
- The permanent set of 1003 is 78 sections and 36471524 bytes, all of which
  parse with no issue.

## Inferred

- The track byte marks the sections the track's route can see, and the
  permanent set is therefore the whole track a race on it draws. The
  loader's code proves the set is resident; that the route never leaves the
  13 zones rests on the look-ahead rule and the data, not on the path code.
- 0x0078A320 is the number of players, from its readers; its value in a
  career race was not traced.

## Unknown

- What a tracked position adds: the path structure at 0x007372B0 that
  0x00444870 consults before the zones, and the route chunks behind it.
- What file index 0 names, and what the `--` record's 79328 bytes are.
- The word at record +32, which is close to but not equal to the section's
  byte sum (1448604 against 1448724 for `Z17`).
- The loader's texture lookup order across packs, and so which of two
  shared copies a draw really takes. `track-whole` reports how many hashes
  were shadowed (39 for the permanent set) and uses its own order.
- Which mesh slot the game draws at a distance; every frame here uses slot 0,
  as [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) records.
- Whether the Switch should keep textures decoded on the CPU at all, which
  decides whether the whole track fits a streaming region.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`.
