# Race events and the Underground order

Clean-room notes on which races Need for Speed: Underground runs, on which
track, stream and texture pack, of which type, for how many laps and in
which direction, and in which order Underground mode (the career) offers
them. Everything here comes from reading a user-provided retail
installation and its executable read-only. No game data is stored in this
repository: this note holds offsets, counts, hashes and field meanings, and
the few short identifiers needed to name race 1.

The answer first, for issue #155 and the first-lap target of #159:

- **Race 1 of Underground mode is event 1: a circuit on track 1003, 2
  laps, forward.** Track 1003 lives in stream region `L1RA`, so it needs
  `Tracks/STREAML1RA.BUN`, `Tracks/STREAML1RA_1003.BUN`,
  `Tracks/TEX1003TRACK.BIN` and `Tracks/TRACKB1003.LZC`. The track
  record's name field reads `DT1 - Olympic Square`.
- **Race 1 is a circuit, so the first circuit in Underground order is race
  1 itself.**
- Before race 1, a new career enters **event 0**, which is also a circuit
  on track 1003, 2 laps, forward. Whether the owner counts that race or
  not, the track, stream, type, lap count and direction are the same.
- **No circuit runs on `STREAML1RB`**, the stream milestone M2 decoded:
  every circuit and lap-knockout race in the table is on tracks 1001-1008,
  and all eight are region `L1RA`. `L1RB` holds four drag tracks and four
  drift tracks.

The two tables are decoded by `nfsu::parse_track_info_table` and
`nfsu::parse_race_event_table` (`include/nfsu/race_events.h`), and
`nfsu_probe race-events` lists both with the offset of every value.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Global/GLOBALB.LZC.bacc` | 972201 | `DCFEA6677B0893B40937C886CF2FD953FBDD12CC878FB8FF9DD228FF3A9F9236` |
| `Global/GlobalB.lzc` | 2610196 | `B4E06F7EDC933C3DCAD2E607E04184B45B010281C9BE71C5E98CA4198F31756B` |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` |

Executable addresses are `speed.exe` virtual addresses. The image base is
0x00400000, and the section table puts `.text`, `.rdata` and `.data` at raw
offsets 0x1000, 0x297000 and 0x2D4000 for virtual addresses 0x401000,
0x697000 and 0x6D4000, so `file_offset = VA - 0x400000` holds for all
three. Offsets into a data file are decimal offsets into the decompressed
image.

**Which file is retail.** The game loads `GLOBAL\GLOBALB.LZC` (string
0x006C9E18, pushed at 0x0044865F) and `FRONTEND\FRONTB.LZC` (0x006C9D3C,
pushed at 0x00448BE9). This installation carries modifications, and
`Global/GlobalB.lzc` is one of them: it is dated 2025 and stored
uncompressed. `Global/GLOBALB.LZC.bacc`, dated 2003, is a JDLZ blob whose
decompression is byte-identical to `Global/GLOBALB.BUN`, so `GLOBALB.BUN`
is used here as the retail image. The track table payload is identical in
all three (SHA-256
`8F6B6CA2337EB321B3554EF3238C0E9617E3E037E829A31F550AC2CCE5C557B9` over its
7812 bytes); only its offset differs in the modified file.
`FrontEnd/FrontB.lzc` is dated 2003, has no backup beside it, and is JDLZ;
its image is 12822772 bytes with SHA-256
`94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77`.

## How the two tables were found

The file names do not say which tracks are circuits, so the search started
from the data. A walk of the top-level chunks of `GLOBALB.BUN` finds one
chunk, `0x00034201` at 2101632, whose 7812-byte payload repeats a pattern
every 252 bytes: a NUL-padded name, a `Location1\Track_<n>` path, a region
string, a region path, and the track number twice as 16-bit words. That is
the track table. It holds one record per track number and no race list.

A scan of the retail files for dense runs of the 31 track numbers as 16-bit
words found no event list in `GLOBALB.BUN` or `speed.exe`, and a walk of
the chunks of the decompressed `FrontB.lzc` found a container `0x80034A00`
at 11744896 whose child `0x00034A02` (38512 bytes) holds a track number at
the same place in every 332-byte stretch. That is the event table. Both
layouts were then confirmed from the code that reads them, below, before
any field was named.

## The track table: `GLOBALB`, chunk `0x00034201`

The loader at 0x00442040 recognises the chunk (0x00442044 `cmp dword ptr
[eax], 0x34201`), keeps the payload pointer at 0x00736168 and stores the
payload size divided by 252 at 0x0073616C (0x00442058..0x00442068). The two
lookups by number, 0x004484F0 and 0x00441FE0, step records with `add ecx,
0xfc` (0x0044850C, 0x00441FFB). The retail payload is 31 x 252 bytes
exactly, and in every record the name at +0x00 is repeated at +0xDC, which
fixes the period independently of the code.

| Offset | Type | Meaning | Code | Retail data |
| --- | --- | --- | --- | --- |
| +0x40 | char[8] | stream region | 0x004496FD..0x0044970D format `TRACKS\STREAM%s.BUN` with record + 0x40; 0x00449728..0x0044972C format `TRACKS\STREAM%s_%d.BUN` with it | five values, `L1RA`, `L1RB`, `L1RC`, `L1RE`, `L1RX`: exactly the five `STREAML1R?.BUN` in `Tracks/`. The next string starts at +0x48 |
| +0x69 | s8 | lap-skip byte | 0x004ABC62 copies it to 0x0078A318; 0x00439408 skips the comparison against the lap count 0x0078A31C at 0x0043941A when it is non-zero | 1 on exactly the 14 tracks whose mode mask is 0x24 or 0x02, 0 on the other 17 |
| +0x6C | s16 | track number, the lookup key | 0x00448503 `movsx edi, word ptr [ecx + 0x6c]`, compared at 0x00448507; the `%d` of the stream pack name (0x00449723); copied to the current track number 0x0078A2F0 when a career race starts (0x004ABC58..0x004ABC5C) | the 31 numbers of the `Tracks/` file names |
| +0x6E | s16 | unknown | no reader traced | equal to +0x6C in all 31 |
| +0x74 | u32 | quick-race mode mask | 0x004AB49F switches on a quick-race mode 0..6 and tests one bit per mode (table 0x004AB4EC: 0x01, 0x04, 0x40, 0x10, 0x20, 0x02, 0x08); 0x004BA204 leaves bit 0x08 out of the reverse pass of the quick-race track list | 0x71 on 1001-1008, 0x24 on 1102-1109, 0x02 on the six 12xx, 0x08 on 1301-1308, 0 on 1099 |

Which quick-race mode each bit stands for is not decoded, and the mask is
reported raw. The strings at +0x00, +0x20, +0x48 and +0xDC and the word at
+0x70 are not decoded either; the word at +0x70 equals the key of one of
the four records of chunk `0x00034202`, which is all that was observed.

**Per-track file names.** The loading game flow formats four names:

| Name | Format string | Pushed at | Filled from |
| --- | --- | --- | --- |
| `STREAM<region>.BUN` | 0x006C9B88 `TRACKS\STREAM%s.BUN` | 0x0044970D | record +0x40 |
| `STREAM<region>_<number>.BUN` | 0x006C9B70 `TRACKS\STREAM%s_%d.BUN` | 0x0044972C | record +0x40, +0x6C |
| `TEX<number>TRACK.BIN` | 0x006C9C00 `TRACKS\TEX%dTRACK.BIN` | 0x0044953E | 0x0078A2F0 (0x00449526) |
| `TRACKB<number>.LZC` | 0x006C9B38 `TRACKS\TRACKB%d.LZC` | 0x004497BE | 0x0078A2F0 (0x00449791) |

The current track record is found by 0x004484F0 from 0x0078A2F0, which
0x0044979B calls. Built for all 31 records these names are 5 streams and
31 of each of the other three, 98 names, and `Tracks/` holds exactly those
98 files and nothing else.

## The event table: `FrontB.lzc`, chunk `0x00034A02` in `0x80034A00`

The container loader at 0x005A00A0 checks the id (0x005A00A4 `cmp dword
ptr [eax], 0x80034a00`) and walks the children. A child `0x00034A02`
(0x005A00C2 `sub eax, 0x34a01`, 0x005A00C9 `dec eax`) goes to 0x005A0060,
which divides its size by 332 (0x3159721F and a shift of 38) and files
each record in a 256-entry pointer table at 0x00733FF8, indexed by the
record's first word:

```text
005A0084  mov  ecx, dword ptr [eax]          ; record +0x00
005A0086  cmp  ecx, 0x100                     ; 256 slots
005A008C  jae  0x5a0095                       ; out of range: not filed
005A008E  mov  dword ptr [ecx*4 + 0x733ff8], eax
005A0095  add  eax, 0x14c                     ; next record, 332 bytes on
```

The retail payload is 116 x 332 bytes exactly.

| Offset | Type | Meaning | Code | Retail data |
| --- | --- | --- | --- | --- |
| +0x00 | u32 | event number | the key above; the brief screen prints it with `Event %d` straight from the record (0x004C5A3D..0x004C5A47) | 116 distinct values, 0..84, 86..113 and 120..122 |
| +0x04 | u32 | event kind | 0x004C4924..0x004C4934 add `_TOURNAMENT` to the mode icon name when it is 1; 0x005A1015 never offers an event of kind 8 | kind 1 on 12 events, each with 3 or 4 races; every other event has exactly 1 race |
| +0x08 | u32 | race type | 0x004C4939 `mov eax, [eax + 8]`, `cmp eax, 5` and the jump table 0x004C49D4 name the icon `CIRCUIT`, `DRAG`, `SPRINT`, `DRIFT`, `TIME_TRIAL`, `LAP_KNOCKOUT` for 0..5 | see "Race type" below |
| +0x78 | 8 x 4 bytes | race slots | 0x005A2454 `lea esi, [ebp + 0x7a]`, stride 0x005A250B `add esi, 4`; the list they are appended to holds 8 (0x005A2485) | 142 used slots; every unused slot is zero |
| slot +0 | s16 | track number | 0x005A2463 `movsx ecx, word ptr [esi - 2]` | all 142 name one of the 31 tracks |
| slot +2 | s8 | lap count | 0x005A2460 `movsx eax, byte ptr [esi]` | see "Laps" below |
| slot +3 | s8 | direction, 0 forward, 1 reverse | 0x005A2467 `movsx edx, byte ptr [esi + 1]` | see "Direction" below |
| +0x98 | u32 | race slots used | 0x005A2444, 0x005A2504 | 1 on 104 events, 3 on 10, 4 on 2 |
| +0x9C | 6 x s16 | events that must be won first | 0x005A1038 `lea edi, [ecx + 0x9c]`, 0x005A1040 `movsx ecx, word ptr [edi]`, 0x005A107D `add edi, 2` | every listed number is an earlier event, except that event 0 lists itself |
| +0xA8 | u16 | 0 all of them, 1 any of them | 0x005A101F; 0x005A1029 and 0x005A108A; any other value refuses (0x005A108E) | 0 on 111 events, 1 on 5 |
| +0xAA | s16 | prerequisites used | 0x005A102B `movsx eax, word ptr [ecx + 0xaa]` | 1 to 5 |

### Race type

The type word is read in one place that names it, the icon switch above.
The second, independent observation is where the types occur. Every type
sits only on tracks of one mode-mask class from the track table, and the
split is the one the names predict:

| Type | Race slots | Track mode mask | Lap-skip byte | Laps stored |
| --- | ---: | --- | --- | --- |
| 0 circuit | 44 | 0x71 (1001-1008) | 0 | 2 to 7 |
| 5 lap knockout | 12 | 0x71 (1001-1008) | 0 | always 3 |
| 2 sprint | 27 | 0x24 (1102-1109) | 1 | always 2 |
| 4 time trial | 7 | 0x24 (1102-1109) | 1 | always 2 |
| 1 drag | 31 | 0x02 (12xx) | 1 | always 2 |
| 3 drift | 21 | 0x08 (1301-1308) | 0 | always 4 |

The word at +0x128 (not decoded) is 0 on all 7 time trials and 1 or 3 on
every other event, and 3 on all 12 lap knockouts. The table holds 32
circuit, 23 drag, 23 sprint, 19 drift, 7 time-trial and 12 lap-knockout
events, and no other type value.

### Laps

Code, as a chain from the record to a global that other code names:

```text
005A2460  movsx eax, byte ptr [esi]           ; slot +2
005A24C2  mov   dword ptr [eax + 8], ecx      ; race entry +8
004ABC75  mov   edx, dword ptr [esi + 8]      ; current race entry +8
004ABC79  mov   dword ptr [0x78a31c], edx
```

The network settings path loads the same global from the quick-race option
at +0x10 (0x0043B1AB..0x0043B1B1), and the network code prints that option
as `LAPS` in `TRACK=%d DIR=%d LAPS=%d MYIP=%d` (0x0054A43A..0x0054A446,
string 0x006BD414). 0x0043C578..0x0043C584 loops a lap number from 1 up to
0x0078A31C over a per-lap table at +0xC8 of a race object.

Data: 3 on all 12 lap knockouts, 2 to 7 on circuits (2 on 14 slots, 3 on
14, 4 on 8, 5 on 2, 6 on 5, 7 on 1), and 4 on every drift. Sprints, time
trials and drags all store 2, and all of them run on tracks whose lap-skip
byte is 1, so the comparison at 0x0043941A is skipped for them. What else
reads the lap count for those races is not traced.

### Direction

Code, the same chain one field over:

```text
005A2467  movsx edx, byte ptr [esi + 1]       ; slot +3
005A24BB  mov   dword ptr [eax + 4], edx      ; race entry +4
004ABC6B  mov   ecx, dword ptr [esi + 4]
004ABC6F  mov   dword ptr [0x78a2fc], ecx
```

The network path loads 0x0078A2FC from the option it prints as `DIR`
(0x0043B18A..0x0043B18D), and the track load passes `0x0078A2FC == 1` to
0x004435E0 (0x00449921..0x00449938). The quick-race track list adds every
track twice, once with 0 and once with 1 (0x004BA182 and 0x004BA224 into
0x004B9F40, which stores the value at item +0x18 at 0x004B9F63), and shows
`REVERSE_TRACK_%d_ICON` and `REVERSE_TRACK_IMAGE_%d` for an item whose
+0x18 is non-zero (0x004BA293..0x004BA2F0). Drift tracks are left out of
the second pass (0x004BA204..0x004BA209).

A third, more direct reader: the brief screen (0x004C5AA7..0x004C5AB0)
passes the event record's +0x7B (slot 0 direction) and +0x78 (slot 0
track) to 0x004AD6D0, which formats `REVERSE_TRACK_NAME_%d` when the byte
is non-zero and `TRACK_NAME_%d` when it is 0. The game's own UI names
direction 0 forward this way.

Data: only 0 and 1 occur, on 86 and 56 slots. No drift slot is 1, which
matches drift tracks having no reverse entry in the quick-race list. And
the up to five 12-byte entries at +0x38 that 0x005A0E64..0x005A0EB1 walks
when 0x005A0DC0 marks an event won (count at +0x74; not decoded) hold 47
entries whose first word is 3, and every one of the 47 carries in its next
two words a track number and a 0 or 1 equal to one of its own event's
(track, direction) pairs.

What reverse does to the route is not traced: 0x004435E0 and 0x004430B0
were not read. [`RACE_ROUTE.md`](RACE_ROUTE.md) has since traced
0x004435E0 and the reverse measure of the track path.

### Not decoded

These were seen and are left out of the library, each for want of a
second observation or a named reader:

- +0x10, a float the event entry at 0x005A0D43 subtracts from the career's
  cash; with the `Entry Fee: %d` string nearby it is probably the fee.
- +0x28, +0x2C and +0x30, three floats the brief screen prints
  (0x004C5A56..0x004C5A8C), one of which 0x005A0DC0 adds to the cash,
  indexed by the career's +0x348 (0x005A0E3B).
- +0x38 to +0x77, the unlock entries above.
- +0xD4 to +0x127, three 0x1C-byte records that each begin with a short
  NUL-terminated name, and the word at +0x128.
- The meaning of kinds 0 and 2 to 6. Kinds 2 and 3 matter to the
  prerequisite rule below; kind 3 is not unique to event 111 - it is also
  events 15, 47, 68 and 77, all time trials. Kinds 5 and 4 are events 112
  and 113, the last two numbered events before the 120..122 gap.

## The Underground order

**The order is the event number.** It is the key of the pointer table and
the number the brief screen prints, and every prerequisite in the table
names a lower number than its own event (event 0 names itself, which never
matters, below).

**A new career enters event 0.** The handler at 0x004B37D0 (a screen class
whose vtable, 0x006C615C, is followed by the name string
`UndergroundExplained`) does this:

```text
004B3832  mov  esi, 0x75eef8                  ; the career
004B3837  call 0x5a0a00                       ; reset
004B383C  xor  edi, edi                       ; event 0
004B383E  call 0x5a0ce0                       ; enter the event
```

0x005A0A00 zeroes the 256 status bytes at career +0x34C (0x005A0A2A..
0x005A0A35). 0x005A0CE0 marks the event entered (status 3, 0x005A0D72) and
builds the race setup from its record (0x005A0D76 call 0x005A2350).

**The availability rule** is 0x005A0FC0. An event is not offered when its
number is 0 (0x005A0FC0..0x005A0FC7), when its own status is 2 or 4
(0x005A1005, 0x005A100D), or when its kind is 8 (0x005A1015). Otherwise a
listed prerequisite counts when its status is 2, or 4 or 5 on a
prerequisite of kind 2 or 3 (0x005A1054..0x005A106D), and mode 0 needs
every one while mode 1 needs any one. Status 2 is what 0x005A0DC0 writes
(0x005A0E60), the handler that also adds one of the event's reward floats
to the career's cash (0x005A0E3B..0x005A0E5A) and walks its unlock
entries; this note calls status 2 won. `nfsu::race_event_available` is
this rule.

Applied to the retail table with only event 0 won, the rule offers exactly
events 1, 2, 3, 4 and 5, all at once:

| Event | Kind | Type | Track | Region | Laps | Direction | Prerequisites |
| ---: | ---: | --- | ---: | --- | ---: | --- | --- |
| 0 | 0 | circuit | 1003 | L1RA | 2 | forward | 0 |
| 1 | 0 | circuit | 1003 | L1RA | 2 | forward | 0 |
| 2 | 0 | sprint | 1102 | L1RA | 2 | forward | 0 |
| 3 | 0 | drift | 1301 | L1RB | 4 | forward | 0 |
| 4 | 0 | drag | 1210 | L1RB | 2 | reverse | 0 |
| 5 | 0 | lap knockout | 1003 | L1RA | 3 | reverse | 0 |
| 6 | 1 | circuit, tournament | 1003, 1003, 1006 | L1RA | 2, 2, 2 | forward, reverse, forward | all of 1 2 3 4 5 |

So race 1 by the game's own numbering is event 1, and it is a circuit.
Event 0 is the race the new-career path enters before any other; nothing
in the data labels it a prologue, and that reading rests on the code path
above alone. It runs the same track, laps and direction as event 1.

Two other irregularities in the numbering: event number 85 does not exist,
and the numbers jump from 113 to 120. Events 120, 121 and 122 are the
three kind-8 events, which the rule never offers; all three are circuits
on L1RA tracks.

## No circuit on `STREAML1RB`

The 44 circuit slots and the 12 lap-knockout slots all name tracks
1001-1008, and the track table puts all eight in region `L1RA`. Region
`L1RB` holds tracks 1201, 1206, 1210 and 1214 (drag, mask 0x02) and
1301-1304 (drift, mask 0x08). The drift tracks there have a lap-skip byte
of 0 and their events store 4 laps; event 3, one of the five races offered
with race 1, is a 4-lap drift on 1301. That is a statement of what the
data holds, not a choice between the two. The owner's 2026-10-12 first-drift
task makes that event the port's first supported drift race, and the
2026-10-14 follow-up restored the rest: `nfsu::kSupportedDriftRows` names
every single-race drift event (tracks 1301-1308, all forward, four laps;
the three-race tournament event 38 stays refused), and
`load_supported_race` fails closed unless the retail record still reads one
drift race on the row's track, region `L1RB` for 1301-1304 and `L1RC` for
1305-1308, lap-skip 0. The owner's 2026-10-15 drag task restores the drag
family the same way: `nfsu::kSupportedDragRows` names the twenty
single-race drag events on tracks 1201, 1202, 1206, 1207, 1210 and 1214
(both directions, stored 2 laps on a lap-skip-1 course the port times as
one point-to-point pass), and `load_supported_race` fails closed unless
the retail record still reads one drag race on the row's track, region and
lap-skip 1. The drag tournaments 9, 20 and 80 stay refused. The owner's 2026-10-17 sprint task restores the last track family the same way: `nfsu::kSupportedSprintRows` names the twenty single-race sprint events of kinds 0 and 6 on the open tracks 1102-1109, validated against region `L1RA` and lap-skip 1; the tournaments 66 and 102 and the kind-5 event 112 (whose kind no reader has traced) stay refused. The 2026-10-18 lap-knockout task restores the last event family the same way: `nfsu::kSupportedKnockoutRows` names the twelve kind-0 single-race lap-knockout events (three laps each on the closed circuit tracks), validated like the circuit rows; the elimination rule needs opponents the solo port does not simulate, so the port runs each as its laps. The seven kind-2 and kind-3 time-trial events (the unlockable races the availability rule treats specially) close the single-race coverage the same day: `nfsu::kSupportedTimetrialRows` names them, one point-to-point pass each on the sprint tracks.

## The probe

```text
build/nfsu_probe race-events <install>/Global/GLOBALB.BUN <install>/FrontEnd/FrontB.lzc
```

Either file may be raw or JDLZ-compressed; the loader sniffs the header
and decompresses with `nfsu::jdlz_decompress`. It prints only the file
names, not their directories, so the listing is the same wherever the
installation lives. Per track: the number, the record offset, and each
decoded field with its offset, then the four file names. Per event: the
number, kind, type, race count and prerequisites, each with its offset,
then one line per race with the slot offset, the lap and direction offsets,
and the region and file names of its track. Then the counts, the order
lines and the circuit slots per stream. Issues go to standard error.

Exit status: 1 for a usage error; 3 when either table fails validation or
a race names a track the track table does not hold (the game would read a
null record at 0x004ABC58), in which case the order lines are not printed;
0 otherwise.

**The retail listing is pinned.** Over this installation, with
`<install>` the retail root, the command above exits 0 and prints 303
lines. Its standard output with CR LF folded to LF is 48401 bytes with
SHA-256

```text
7EF19667B0FCEE75AB59FF4F1A1226E6E22BB1A327562D94079D24EA7E1D07C3
```

measured with `... | tr -d '\r' | sha256sum` in Git Bash, not through
PowerShell, whose `>` adds a byte order mark. The last six lines are:

```text
underground order: ascending event number (0x004C5A40)
new career enters: event 0 (0x004B383E)
winning event 0 opens: 1 2 3 4 5
race 1: event 1 type circuit races 1 track 1003 laps 2 direction forward region L1RA stream STREAML1RA.BUN pack STREAML1RA_1003.BUN textures TEX1003TRACK.BIN
first circuit from race 1: event 1 type circuit races 1 track 1003 laps 2 direction forward region L1RA stream STREAML1RA.BUN pack STREAML1RA_1003.BUN textures TEX1003TRACK.BIN
circuit race slots by stream: STREAML1RA.BUN 44
```

Given `Global/GLOBALB.LZC.bacc` instead of `GLOBALB.BUN`, the listing differs
only in its first two lines, the file name and `stored 972201 image
2361176 jdlz 1`. Given the modified `Global/GlobalB.lzc`, only the track
offsets move; every event line is identical.

## Parser rules

`nfsu::parse_track_info_table`:

- The image is at most `RaceTableLimits::max_image_size` bytes, and its
  top-level chunks tile it exactly; exactly one is `0x00034201`.
- The payload is a non-zero whole number of 252-byte records. The game
  floors the division and would ignore a remainder; the parser refuses it
  rather than guess what the remainder was.
- Each region is NUL-terminated inside its 8 bytes, non-empty and
  printable ASCII, because it becomes part of a file name.
- Each track number is positive and no two records share one: the game's
  linear lookup would never reach the second.

`nfsu::parse_race_event_table`:

- The same size and tiling rules; exactly one top-level `0x80034A00`, its
  children tile its payload, and exactly one child is `0x00034A02`, a
  non-zero whole number of 332-byte records.
- Each event number is below 256 (the loader never files a larger one)
  and not shared (the pointer table would keep only the last).
- 1 to 8 races, each with a positive track number and at least one lap.
- A prerequisite mode of 0 or 1, 0 to 6 prerequisites, and each listed
  prerequisite the number of an event in the table: the availability test
  reads the listed event's record without checking it exists
  (0x005A1044..0x005A1060).
- Unused race and prerequisite slots are not read by the game and are not
  checked.

`nfsu::check_race_tracks` reports every used race slot whose track is not
in the track table.

## What remains unknown

- Which quick-race mode each bit of the track mode mask stands for.
- The track word at +0x6E, and what the lap-skip byte at +0x69 means
  beyond the one comparison it gates.
- What the reverse direction does to a track's route and geometry.
- Event kinds other than 1 (tournament) and 8 (never offered), and status
  values other than 2 (won) and 3 (entered).
- The entry fee, rewards, unlock entries and opponent records named under
  "Not decoded".
- Whether anything outside the career changes the lap count of a career
  race after 0x004ABC79 sets it.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`.
