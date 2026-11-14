# Vehicle tuning data

Issue #57, under #56: what a CarTypeInfo record holds beyond the stock setup
[`VEHICLE.md`](VEHICLE.md) decodes, and the rule that folds it into a car.
[`VEHICLE.md`](VEHICLE.md) listed "the per-level tables from +0x3C0" under
"What is not known"; this document decodes them.

The code is `include/nfsu/vehicle_tuning.h` and `src/vehicle_tuning.cpp` (the
library `nfsu_vehicle_tuning`), the built-in table
`src/vehicle_tuning_240sx.inc`, the probe command `vehicle-tuning`, and the
test `nfsu_vehicle_tuning_tests`.

**Nothing here is wired into the driving model.** The model of
[`VEHICLE.md`](VEHICLE.md) reads a stock record and is untouched by this
branch; its pinned telemetry is unchanged. The last section says what wiring
it in would change.

The same two labels as [`VEHICLE.md`](VEHICLE.md) are used: **RECOVERED** for
a value read from a named offset or a rule read from the instructions that
apply it, and **APPROXIMATION** for anything this project invents. Nothing in
this document is an approximation; where no reader was traced the field is
called unknown and what was observed is said instead.

Which numbers this document carries, under CLAUDE.md's rule that a pinned
value lives in its test: the 240SX's own field values are held by
`stock_240sx_tuning()` and pinned by `nfsu_vehicle_tuning_tests`, so they are
named there and not copied here - round 2 of the review reported them as a
note (N4) and this round removed them. What stays is the observation over all
35 records: a count, a range, or which cars an equality holds for. No test
reads a retail file, so those are the pin of record, and
`nfsu_probe vehicle-tuning` prints every one of them for every car.

## Evidence base

| File | SHA-256 | What it gives |
| --- | --- | --- |
| `speed.exe` | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` | the rules; addresses are virtual, file offset = VA - 0x400000 |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` | the 35 CarTypeInfo records, chunk 0x00034600 |

`Global/GLOBALB.BUN` is the uncompressed original; the installation's
`GlobalB.lzc` and `GLOBALA.BUN` are the owner's 2025 modifications and nothing
here rests on them. Both files were read, never written. The installation's
file manifest - path, size and modification time of all 476 files - was taken
before and after every run this document rests on and compared with `cmp`; it
is identical.

Record 18 is the 240SX, at absolute offset 1736752; the whole 3216-byte record
hashes to
`212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B`, the same
value [`VEHICLE.md`](VEHICLE.md) records, which is what says the extraction
here reads the same bytes it does.

## Where the data is, and what writes it

[`PART_PLACEMENT.md`](PART_PLACEMENT.md) decodes the record array: chunk
`0x00034600`, 35 records of `0xC90` bytes at `align_up(header + 8, 16)`.
[`VEHICLE.md`](VEHICLE.md) decodes the setup sub-object at record `+0x110`,
`0x2B0` bytes, which `0x00575960` copies into `RideInfo + 0x60` for each car
instance (`0x0057596C`, `mov ecx, 0xAC` / `rep movsd`). Everything from
`+0x3C0` to `+0x9BB` is the upgrade data that the same function then folds
into that copy.

`0x00575960` takes three arguments - the record's sub-object pointer, the
instance's copy, and a levels object - and calls, in this order:

| Call site | Callee | What it applies |
| --- | --- | --- |
| `0x00575980` | `0x00575A10` | the suspension table |
| `0x00575989` | `0x00575A90` | the tyre table, then the tyre-friction item |
| `0x00575994` | `0x00575B30` | the two downforce tables |
| `0x005759A0` | inline | the grip block table, then its item |
| `0x005759E6` | `0x00575BC0` | the two engine tables, the induction curve, the three item curves, the nitrous table |
| `0x005759EE` | `0x00576360` | the gearbox table, then the drive-split item |
| `0x005759F7` | `0x00576250` | the weight table, its item, and the inertia rebuild |

Every callee addresses the record through the pointer the copy was read from,
`record + 0x110`, so an offset it computes as `source + X` is record
`+0x110 + X`. Every table gates its level with `test`/`jle` then `cmp 4`/`jge`,
so a level outside 1..3 means the table is not read at all and 0 is "nothing
installed". The boolean items are separate fields of the same levels object,
each tested with `test`/`je`.

The levels object, read in full by the seven callees above:

| Offset | What it selects | Table |
| --- | --- | --- |
| `+0x00` | weight level | `+0x3C0` |
| `+0x04` | suspension level | `+0x7F0 + 0x50 * level` |
| `+0x08` | engine level A | `+0x4B0` |
| `+0x0C` | induction level | `+0x630` |
| `+0x10` | nitrous level | `+0x780` |
| `+0x14` | engine level B | `+0x570` |
| `+0x18` | gearbox level | `+0x3F0` |
| `+0x1C` | tyre level | `+0x6C0` |
| `+0x20` | grip block level | `+0x810` |
| `+0x24` | downforce level A | `+0x7B0` |
| `+0x28` | downforce level B | `+0x7E0` |
| `+0x2C` | mass item | scalar `+0x930` |
| `+0x34` | torque item A | curve `+0x950` |
| `+0x38` | torque item C | curve `+0x998` |
| `+0x3C` | nitrous item | scalar `+0x938` |
| `+0x40` | torque item B | curve `+0x974` |
| `+0x44` | drive-split item | scalar `+0x934` |
| `+0x48` | tyre-friction item | scalars `+0x940`..`+0x94C` |
| `+0x4C` | grip block item | scalar `+0x93C` |

What the levels object is a field of, and what fills it from the player's
garage, was not traced. The names above are this project's; the offsets and
what each one gates are read.

## The region, table by table

| Record offsets | Levels | Entry | What it does |
| --- | --- | --- | --- |
| `+0x3C0`..`+0x3EF` | 3 x `0x10` | weight | scales the mass, moves the centre of mass, replaces `+0x270` |
| `+0x3F0`..`+0x4AF` | 3 x `0x40` | gearbox | replaces `+0x2C0`..`+0x2FF` |
| `+0x4B0`..`+0x56F` | 3 x `0x40` | engine A | adds to the rpm limits, the torque curve and the engine braking |
| `+0x570`..`+0x62F` | 3 x `0x40` | engine B | the same fields, additive, so the two stack |
| `+0x630`..`+0x6BF` | 3 x `0x30` | induction | replaces the second torque curve at `+0x334` |
| `+0x6C0`..`+0x77F` | 3 x `0x40` | tyres | replaces both tyre blocks, `+0x1E0` and `+0x200` |
| `+0x780`..`+0x7AF` | 3 x `0x10` | nitrous | writes the four words at `+0x3B0` |
| `+0x7B0`..`+0x7DF` | 3 x `0x10` | downforce A | adds to `+0x374`, `+0x378`, `+0x37C` |
| `+0x7E0`..`+0x80F` | 3 x `0x10` | downforce B | the same three, additive |
| `+0x810`..`+0x83F` | 3 x `0x10` | grip block | replaces `+0x384`, `+0x388`, `+0x38C` |
| `+0x840`..`+0x92F` | 3 x `0x50` | suspension | replaces both strut blocks and lowers the car |
| `+0x930`..`+0x94F` | - | item scalars | eight floats the boolean items use |
| `+0x950`..`+0x9BB` | - | item curves | three nine-point torque curves, one per torque item |

The suspension table is the one the game indexes by the level itself, at
`+0x7F0 + 0x50 * level` (`0x00575A1C`, `lea edx, [ecx + ecx*4 + 0x6E]` then
`shl edx, 4`), so its slot for level 0 would start at `+0x7F0` - which is
where the downforce B table's level 2 entry lives. That overlap is why the
1..3 gate is load-bearing rather than cosmetic, and it is the only table whose
base is not its first entry. [`PART_PLACEMENT.md`](PART_PLACEMENT.md) already
quoted this table's base and its three ride-height deltas; the rest of the
entry is decoded below.

Every entry of every table has words the retail data keeps at zero and no
traced instruction reads: the fourth word of each `0x10` entry, the last three
of each induction entry, the three after each tyre block's `+0x10`, the third
of each nitrous entry, and `+0x4C` of each suspension entry. All 45 of them
are zero in all 35 records, 1575 words in total, and the parser refuses a
record where one is not.

### The weight table, `+0x3C0`

Read at `0x00576250` with the level from levels `+0x00`.

| Entry offset | Field | Evidence |
| --- | --- | --- |
| `+0x00` | mass scale | RECOVERED: `0x0057626B` loads it in place of the 1.0 at `0x006CC7BC`, `0x00576285` subtracts the mass item when levels `+0x2C` is set, `0x0057628B` multiplies `+0x220` by the result and stores it back |
| `+0x04` | centre of mass, longitudinal, m | RECOVERED: `0x00576270`..`0x00576275` adds it to `+0x110` |
| `+0x08` | yaw inertia scale | RECOVERED: `0x00576277`..`0x0057627A` stores it over `+0x270`, the factor `0x00584B42` builds the rigid body's second inverse inertia with |

The mass scales fall with the level in all 35 records. The six level 3 values
in the file are 0.8, 0.85, 0.9, 0.91 and 0.95 over the racers and 1.0 for
every traffic vehicle, so a traffic vehicle's weight table takes nothing off;
the Supra and the Skyline are the two racers at 0.8. The yaw inertia scale
falls with the level too, and its level 3 value is 1.8 for 15 of the 20
racers, 1.77 for the Skyline, 1.82 for the 240SX, 1.85 for the Peugeot and
1.9 for the Civic and the Celica - five exceptions and fifteen at 1.8; every
traffic vehicle's is 1.8.

### The gearbox table, `+0x3F0`

Read at `0x00576385` with the level from levels `+0x18`. The entry has the
layout of the stock block at `+0x2C0` and replaces it word for word
(`0x00576393`..`0x005763DF`), with one exception.

Entry `+0x18` is the stored forward-gear count, and the game does not copy it.
`0x005763E1` writes zero over it in the loaded record, and
`0x005763F0`..`0x0057640D` walks the gear slots from slot 2 upward, counting,
and stops at the first ratio that is **equal** to the 0.0 at `0x006CC7A4`;
that count goes to `+0x2D8`. RECOVERED, and the stop condition is equality
rather than "not above zero", which round 2 of the review corrected (F2):
`0x005763F2` compares the ratio with 0.0, `0x005763F8` reads the status word
and `0x005763FA` masks it with `test ah, 0x44`, which keeps C3 and C2 and
drops C0, then `0x005763FD` leaves the loop on `jnp`. A ratio above zero
clears all three flags, so the masked byte is 0x00, parity is even and the
jump is not taken; a ratio equal to zero sets C3 alone, so the byte is 0x40,
parity is odd and the jump leaves; a ratio below zero sets C0 alone, which the
mask drops, so the byte is 0x00 again and the slot is counted and the walk
continues. A NaN sets C3 and C2 together, an even byte, so it is counted too.
The difference matters only for a record the parser refuses anyway - a
negative forward ratio is refused before any caller reaches the rule - so it
never changes an answer this library returns, and it is still the rule in the
binary. In every one of the 105 retail entries the stored value equals the
count the ratios give under either reading. `vehicle_derived_forward_gears` is
the rule, and `nfsu_vehicle_tuning_tests` pins it twice: directly, over nine
cases including a negative ratio, a NaN and a zero before a positive ratio
(`test_derived_gear_rule`, which also fails if the two readings stop
disagreeing), and end to end over synthetic one- to six-speed entries
(`test_gear_count_rule`).

Level 3 gives every one of the 20 racers six forward gears and leaves all 15
traffic vehicles at five. It also shortens the final drive of 17 of the 20
racers, leaves the Supra's and the Skyline's alone and lengthens the Neon's. The rear drive fraction in all 60 of
the racers' entries is the one that racer's stock record already has; every
traffic entry holds 1.0 where the stock fraction is 0.5, or 0.4 for the bus,
which is one of several signs that the 15 traffic records share one default
set of tables rather than tuned ones - the `vehicle-tuning` probe prints the
same digest for all 15.

### The two engine tables, `+0x4B0` and `+0x570`

Read at `0x00575BDE` (levels `+0x08`) and `0x00575CF0` (levels `+0x14`). Both
have the same 16-float layout and every field is added to its target, so
installing both stacks them.

| Entry offset | Target | Field |
| --- | --- | --- |
| `+0x00` | `+0x300` | idle rpm delta |
| `+0x04` | `+0x304` | redline rpm delta |
| `+0x08` | `+0x308` | maximum rpm delta |
| `+0x0C` | `+0x30C` | a delta for the field no reader of which was traced |
| `+0x10` + 4 i | `+0x310` + 4 i | torque delta, kN m, at the same nine grid points |
| `+0x34` + 4 i | `+0x358` + 4 i | engine braking delta, kN m, three points |

RECOVERED, each by the `fld`/`fadd`/`fstp` triple at its own address:
`0x00575BDE`..`0x00575CD9` for table A and `0x00575CF0`..`0x00575DE5` for
table B, in the target order `+0x1F0`, `+0x1F4`, `+0x1F8`, `+0x1FC`,
`+0x200`..`+0x220`, `+0x248`, `+0x24C`, `+0x250` - sub-object offsets, so
record `+0x300`..`+0x330` and `+0x358`..`+0x360`.

The two tables carry the same nine torque deltas as each other in every one
of the 35 records, and differ in the rpm limits: table A's idle, redline and
maximum deltas are zero in every entry in the file, and table B raises the
redline and the maximum, by 1500 and 2000 respectively at level 3 for the
Golf and by 0 to 2000 rpm over the file. Both tables are all-zero for every
traffic vehicle.

Each car's three levels are a fixed fraction of its own stock torque curve,
and the fraction is that car's: on the 240SX the three levels are the same
fraction at all nine points, which `nfsu_vehicle_tuning_tests`
(`test_engine_table_ratios`) pins, while the Golf's level 3 delta at the
second point is 60% of its stock value there. The engine braking deltas do not
follow the torque fraction, even on a car whose torque deltas do - the same
test pins that - and six of them, all the Skyline's third point, are negative:
an upgrade that brakes the engine less. The parser therefore bounds no delta's
sign.

### The induction curve, `+0x630`

Read at `0x00575DFD` with the level from levels `+0x0C`: nine floats copied
over the sub-object's `+0x224`, that is record `+0x334`.

`+0x334` is a second nine-point torque curve over the same [idle, maximum]
grid as `+0x310`, and `0x00575E11`..`0x00575EAD` adds it into `+0x310`
**unconditionally** - outside every level gate. So a car whose `+0x334` is
non-zero gets that torque with nothing installed at all. Three retail records
have a non-zero `+0x334` in stock: the Eclipse, the RX-7 and the Skyline, the
three factory forced-induction cars in the game. That, and the fact that the
`+0x630` table replaces it, is what names the field; no string or symbol was
found for it.

The three levels are about a third, two thirds and all of the level 3 curve.
The first two points of every entry are zero except in the RX-7's levels 1 and
2, so for every other car the curve adds nothing at idle or at the first grid
point above it.

### The tyre table, `+0x6C0`

Read at `0x00575AAB` with the level from levels `+0x1C`: two `0x20`-byte
blocks with the layout [`VEHICLE.md`](VEHICLE.md) gives for `+0x1E0` and
`+0x200`, front then rear, each copied over its block
(`0x00575AB2`..`0x00575ACF`). The slip, load and lateral scales are 12, 1 and
1 in every entry of all 35 records, exactly as in every stock block, so only
the two friction coefficients move.

Every one of the 210 blocks in the file has both coefficients above zero, and
neither ever falls with the level. Where they rise is narrower than round 1 of
this document claimed (review note N1): the 15 traffic records share one
default set of tables, whose peak is 2.0 and sliding coefficient 1.5 at all
three levels, and among the 20 racers the Supra's front peak is 2.0 at all
three levels too. So both coefficients are non-decreasing with the level in
all 35 records and strictly rising in 19 of the 20 racers, with those two
exceptions. Over the whole file the peaks run 1.668 to 2.6 and the sliding
coefficients 1.453 to 2.45.

`0x00575ADA`..`0x00575B1C` then adds the four item scalars at `+0x940`,
`+0x944`, `+0x948` and `+0x94C` to the front peak, the rear peak, the front
sliding and the rear sliding coefficient when levels `+0x48` is set. All four
are 0.1 in every one of the 35 records.

### The nitrous block, `+0x780`

Read at `0x0057620A` with the level from levels `+0x10`: four words copied
over the sub-object's `+0x2A0`, that is record `+0x3B0`, whose four words are
zero in every retail record. The entries are identical in all 35 records: a
float and a u32 that are both 2, 4 and 6 by level, a reserved zero word, and
0.5. `0x00576233` then adds `+0x938`, 0.125 in every record, to the last word
when levels `+0x3C` is set.

What reads `+0x3B0` was not traced, so the four fields are unknown.
[`VEHICLE.md`](VEHICLE.md) records that the crank torque has a nitrous term at
engine `+0x194` with a curve at `0x006FFF34` and that it is off for a stock
car; that is consistent with a block the stock data leaves at zero, and it is
not a traced reader of these words.

### The two downforce tables, `+0x7B0` and `+0x7E0`

Read at `0x00575B83` (levels `+0x24`) and `0x00575B42` (levels `+0x28`): three
floats added to the sub-object's `+0x264`, `+0x268` and `+0x26C`, that is
record `+0x374`, `+0x378` and `+0x37C`. The game applies the `+0x7E0` table
first. [`VEHICLE.md`](VEHICLE.md) has the block itself: `0x00456969` averages
the first two, multiplies by a speed-like argument and adds the weight, and
the strut preload's total scales by the third, which reads like downforce and
whose argument was not traced.

The two tables hold the same second and third component as each other in every
one of the 35 records and differ only in the first, which is zero in every
entry of the `+0x7B0` table and non-zero in every entry of the `+0x7E0` one
for all 20 racers. Both tables are all-zero for every traffic vehicle, so the
only records where a component rises at all are the 20 racers': all three are
non-decreasing with the level in every one of the 35, and rise in the racers
except where the `+0x7B0` table's first component holds it at zero (review
note N1). The deltas run 0 to 0.115 over the file.

### The grip block table, `+0x810`

Read inline at `0x005759AB` with the level from levels `+0x20`: three floats
written over the sub-object's `+0x274`, `+0x27C` and `+0x278` - in that order,
which is the only place in this function where the words are not written in
offset order - that is record `+0x384`, `+0x38C` and `+0x388`. Then
`0x005759CF` adds `+0x93C` to `+0x384` when levels `+0x4C` is set.

These three fields are labelled **unknown**, and round 2 of the review (F3)
was right that round 1's evidence sentence for that did not reproduce. The
scan, restated so that it does: over `.text` (VA `0x00401000`, `0x295EA7`
bytes), find every occurrence of the four little-endian bytes of `0x274`,
`0x278` or `0x27C`, decode backwards into each one, and keep every
instruction with a memory operand whose displacement is one of the three and
whose addressing is a plain base register plus that displacement, with no
index. That gives 167 distinct instruction addresses, of which 16, in 8
places, are x87 instructions - the shape a float field read has:

| Addresses | Base | What it is |
| --- | --- | --- |
| `0x005759D5`, `0x005759DB` | `ebx` | the writer above |
| `0x00576648`, `0x0057665D`, `0x00576672` | `esi` | the checksum that hashes the whole sub-object |
| `0x0047D205`, `0x0047D224` | `ebx` | another object, whose `+0x270` is a byte flag `0x0047D1DB` tests |
| `0x00581E4F`, `0x00581E5B`, `0x00581E8A`, `0x00581E96` | `ecx` | a different structure |
| `0x0046FA21`, `0x0048AB8E`, `0x0048ABA5` | `esp` | stack locals, not an object at all |
| `0x00538538` | `esi` | another object; `0x0053850A` subtracts its `+0x27C` as an integer |
| `0x00477936` | `edx` | not ruled out, below |

`0x00477936` is `fld dword ptr [edx + 0x27c]`, inside the vehicle physics
range, and `0x00477933` reaches it with `mov edx, dword ptr [ecx + 0x54]`. It
is used as the value or as one minus it (`0x0047793E` subtracts it from the
1.0 at `0x006CC7BC`), doubled, scaled by `[edx + 0x1E4]` of the object the
previous `edx` held (`0x0047792D` loads it before `0x00477933` reloads the
register) and stored to the `+0x170` of the entry its loop is on.

That loop walks **four** entries, not the eighteen round 2 of this document
claimed - review finding R2-F1 corrected the count, and with it the conclusion
drawn from it. `0x00477891` sets `edi` to 0x38 (`bf 38 00 00 00`),
`0x00477B9F` adds 4 and `0x00477BA2`..`0x00477BA9` bounds it below 0x48; over
the 216 instructions from `0x0047788D` to `0x00477BBC`, decoded in sync, those
two are the only writes to `edi`. So `edi` takes 0x38, 0x3C, 0x40 and 0x44.
Eighteen is what 0x48 / 4 gives if the initial 0x38 is missed. Two further
signs of the same four: the preamble's `mov ebx, 0x6FFE28` / `sub ebx, edi` at
`0x00477896` leaves a base biased by that initial value, and `0x00477953` then
reads `[ebx + edi]`, which is four floats at `0x006FFE28`..`0x006FFE37`; and
each entry is a pointer read as `mov eax, dword ptr [edi + ecx]`
(`0x004778C7`, `0x00477944`) whose `+0x170` the result is stored to.

The `cmp edi, 0x40` branches at `0x004778C2` and `0x0047792A` split those four
into a first pair and a second pair. That is the shape the suspension rule of
this document uses for the four wheels at `0x00575A4A`..`0x00575A70` - wheels
0 and 1, then 2 and 3 - so the loop is **consistent with** the four wheels and
does not rule them out. What fills `[ecx + 0x54]` was still not settled - the
same function also dereferences `[ecx + 4]` and `[ecx + 0x58]` - so **the
ruling-out is incomplete**: this document does not claim the grip block has no
reader, only that no reader of it was identified, and `0x00477936` is the one
site that could be one.

What was observed of the three fields: the first value is non-decreasing with
the level in all 35 records and the second and third are not always - the
Golf's second goes 0.568, 0.634, 0.55 - and every traffic record holds 0.35,
0.3 and 0.6 at all three levels (review note N1); the item bonus runs from
0.04 to 0.08 over the 20 racers and is 0.2 for every traffic vehicle; and
every value in the table lies between 0.3 and 0.85. The block sits between the
downforce words and the nitrous words, and `+0x394`, two words further on, is
the 2.5 [`VEHICLE.md`](VEHICLE.md) quotes from the untraced brake term of
`0x0047576B` - which is suggestive and is not evidence.

### The suspension table, `+0x7F0 + 0x50 * level`

Read at `0x00575A1C` with the level from levels `+0x04`. Entries at `+0x840`,
`+0x890` and `+0x8E0`.

| Entry offset | Target | Field |
| --- | --- | --- |
| `+0x00`, `0x20` bytes | `+0x280` | the front strut block, replaced (`0x00575A31`) |
| `+0x20`, `0x20` bytes | `+0x2A0` | the rear strut block, replaced (`0x00575A43`) |
| `+0x40` | `+0x118` | subtracted from the centre of mass's vertical (`0x00575A76`) |
| `+0x44` | `+0x128`, `+0x158` | added to wheels 0 and 1 (`0x00575A4A`, `0x00575A53`) |
| `+0x48` | `+0x188`, `+0x1B8` | added to wheels 2 and 3 (`0x00575A5C`, `0x00575A67`) |

The two strut blocks include a physical bump limit at `+0x14` and a
wheel-display compression limit at `+0x1C`. Original 478B20 reads the latter
after calculating the physical travel. The Golf's stock rear block is one
example where the display limit is lower. `nfsu_probe vehicle-tuning` prints
how many of each record's six blocks have matching limits.

The spring rates never fall with the level, and rise strictly in all 20
racers; every traffic entry is 100 kN/m at all three levels, so the round 1
form of this claim was too wide (review note N1). Every block in the file has
a positive spring rate, a positive bump limit and a negative droop limit; over
the file the rates run 43.6 to 120 kN/m, the bump limits 0.055 to 0.15 m and
the droop limits -0.15 to -0.09 m.

The travel does not move one way for every car, and round 2 of this document
said it did in the form note N1 was about - it counted "does not rise" as
"falls" and gave 17 of 20, which review finding R2-F2 corrected. Measured over
both axles of the three entries, at block `+0x14`: the bump limits fall
strictly across the levels on both axles in 13 of the 20 racers; they are
constant at all three levels on both axles in four more, the Focus, the Lancer
and the Integra at 0.15 m and the Civic at 0.125 m, so those do not fall
either; and they rise on at least one axle in three - the RX-7 on both, the
Impreza on the rear with its front constant at 0.15 m, and the Tiburon on the
rear while its front falls. 13 plus 4 plus 3 is 20. The droop limits, which
round 1 covered in the same sentence and round 2 dropped rather than restating
(review note R2-N1), shrink strictly toward zero on both axles in 15 of the 20
racers; the Impreza, the Lancer and the Integra hold -0.15 m at all three
levels on both axles, and the Focus and the Tiburon have a rear droop limit
that grows away from zero instead.
[`PART_PLACEMENT.md`](PART_PLACEMENT.md)
already recorded the three ride-height deltas for the 240SX, the Golf, the
350Z and the traffic vehicles, which are zero for all 15 of the latter; what
is new here is that `+0x40` also lowers the centre of mass, and by more than
the wheels rise, and the strut blocks themselves. Over the file the wheel
deltas run 0 to 0.05 m and the centre-of-mass drop 0 to 0.15 m.

### The item scalars, `+0x930`, and the item curves, `+0x950`

| Offset | Field | Retail values |
| --- | --- | --- |
| `+0x930` | subtracted from the mass scale | 0.015 in all 35 records |
| `+0x934` | replaces the rear drive fraction `+0x2D0` | 0.199, 0.6 or 0.801 |
| `+0x938` | added to the nitrous block's last word | 0.125 in all 35 records |
| `+0x93C` | added to `+0x384` | 0.04 to 0.08 over the racers, 0.2 for traffic |
| `+0x940`, `+0x944` | added to the front and rear peak friction | 0.1 in all 35 records |
| `+0x948`, `+0x94C` | added to the front and rear sliding friction | 0.1 in all 35 records |
| `+0x950`, `+0x974`, `+0x998` | nine-point torque curves, added to `+0x310` | below |

`+0x934` is worth a line of its own. Only three values appear in the file, and
each sits just outside a threshold of the drive-type classifier at
`0x0047517E`, which splits at 0.2 and 0.8: 0.199 lands in front drive, 0.6 in
four-wheel drive and 0.801 in rear drive. Nineteen of the twenty racers get
the value of the class their stock `+0x2D0` already puts them in; the Peugeot
206 is front-wheel drive in stock, with `+0x2D0` zero, and holds 0.801 here.
Every traffic vehicle holds 0.199 where its stock fraction is 0.5, and the
bus's is 0.4. So the item sets a definite drive class rather than a nudge, and
it is not a no-op on one racer.

The three item curves are added to the torque curve point by point at
`0x00575EB3`..`0x005761F2`, in the order curve `+0x950` (levels `+0x34`),
curve `+0x974` (levels `+0x40`), curve `+0x998` (levels `+0x38`); the third
one is also added into `+0x334`, the induction curve, which is what says
`+0x334` is a record of the forced-induction torque and not a private scratch
word. Curves `+0x950` and `+0x974` are equal to each other in every one of the 35
records.

## The inertia rebuild

`0x00576250` ends, outside every level gate, by rebuilding the rigid body's
inertia. `0x0057628B`..`0x005762F6` writes the 4x4 matrix at the sub-object's
`+0x120`, record `+0x230`, as the identity, and then
`0x005762FC`..`0x00576352` overwrites its three leading diagonal entries with

    roll  = mass * (width^2  + height^2) / 12      -> +0x230
    pitch = mass * (height^2 + length^2) / 12      -> +0x244
    yaw   = mass * (width^2  + length^2) / 12      -> +0x258

from `+0x220`, `+0x224`, `+0x228` and `+0x22C`, with the float 1/12 at
`0x006CCA6C`. RECOVERED.

[`VEHICLE.md`](VEHICLE.md) observed that every retail record's diagonal
already is this box inertia "rounded to float, or one float step from it"; the
rebuild is why, and with the game's own float 1/12 rather than a 1/12 in
double there is no step to allow. Computed in binary64 from each record's own
mass and dimensions and rounded once, all 105 diagonal entries of the 35
records come out bit for bit equal to the stored ones.

Two consequences. The dimensions at `+0x224`..`+0x22C`, which
[`VEHICLE.md`](VEHICLE.md) marks "observed only: no reader was traced", do
have a traced reader, and it is this. And `+0x230` is derived data rather than
a tuning input: the weight table changes the mass, and the inertia follows.

`vehicle_box_inertia` is the rule, and `nfsu_vehicle_tuning_tests`
(`test_box_inertia`) requires it to reproduce the 240SX's stored diagonal
exactly.

## Units, ranges and defaults

Nothing in this region states a unit. Every field either lands in a stock
field whose unit [`VEHICLE.md`](VEHICLE.md) fixes - tonnes, metres, kN, kN/m,
kN s/m, kN m, rpm - or is a dimensionless scale or a coefficient. The three
kinds of entry are:

- **a replacement**, which carries the target's unit unchanged: the gearbox,
  tyre, strut and grip blocks, the induction curve, the nitrous words and the
  yaw inertia scale;
- **an additive delta**, in the target's unit: the engine tables, the
  downforce tables, the ride-height deltas, the item curves and the item
  scalars other than `+0x930`;
- **a multiplier**, dimensionless: the mass scale, with `+0x930` subtracted
  from it before the multiply, so that scalar is dimensionless too.

The default is zero. A level of 0 reads nothing and an unset item adds
nothing, and for that to be the whole story the mass scale starts from the 1.0
at `0x006CC7BC`, so the mass item alone still takes 0.015 off the mass. The
one thing that is not defaulted away is the unconditional pair of rules: the
inertia rebuild and the addition of `+0x334` into the torque curve both run
with nothing installed.

Observed ranges over all 35 records: mass scale 0.8 to 1.0; yaw inertia scale
1.77 to 1.966; torque deltas 0 to 0.108 kN m and engine braking deltas -0.0175
to 0.09 kN m; idle, redline and maximum deltas 0 to 2000 rpm; tyre peak
friction 1.668 to 2.6 and sliding 1.453 to 2.45; spring rates 43.6 to 120
kN/m; bump limits 0.055 to 0.15 m and droop limits -0.15 to -0.09 m; downforce
deltas 0 to 0.115; grip block values 0.3 to 0.85; ride-height deltas 0 to
0.05 m at the wheels and 0 to 0.15 m at the centre of mass. The parser's
bounds are looser than these, deliberately: it refuses what the game would
divide by or index with, not what this one file happens to contain.

## Version assumptions

The record array carries no version word, and neither does this region. Four
things stand in for one, and the parser checks all four:

- the array's shape is hard-coded in the loader, not derived: `0x0057C8F1`
  compares against `0x23`, so exactly 35 records, and `0x0057C8D2` multiplies
  the index by `0xC90`. `parse_car_type_table` already enforces both.
- every record carries its own index at `+0xC50`, and every one of the 35 does
  ([`PART_PLACEMENT.md`](PART_PLACEMENT.md) records the same self-describing
  property for the wheel table's `+0x18`).
- the 45 reserved words of this region are zero in all 35 records.
- each gearbox entry's stored gear count equals the count its ratios give.

A file that fails any of those is not the layout this document decodes, and
saying so is the whole of the version check available. No other release of the
game was examined; the claims here are about this executable and this
`GLOBALB.BUN`.

## What is still not decoded

- `+0x384`, `+0x388`, `+0x38C` and `+0x3B0`..`+0x3BF`: the grip block and the
  nitrous block, whose writers are read and whose readers are not, above.
- `+0x2C4`, `+0x2D4`, `+0x2DC`, `+0x30C` and block `+0x1C` of a strut, which
  the tables carry a value or a delta for and which
  [`VEHICLE.md`](VEHICLE.md) already lists as unread.
- `+0x9BC`..`+0xC4F`, everything between the item curves and the record's own
  index. Observed, and out of this issue's scope: `+0x9C0`..`+0xB4F` is four
  `0x50`-byte blocks of packed 16-bit values in the 200..1500 range beside a
  pointer-sized word - rpm-like, and engine audio is
  [`RACE_AUDIO.md`](RACE_AUDIO.md)'s ground; `+0xB50`..`+0xBFF` is two
  five-entry tables of (index, float, float, float) whose floats are the same
  in all 35 records, so nothing per-car; and `+0xC00`..`+0xC4F` holds round
  figures like 275, 300, 7000 and 360 that read like dial and HUD limits.
  None of it was traced to a reader, and none of it is decoded here.
- What object the levels object belongs to, and what fills it from the
  player's garage.

## The parser

`decode_vehicle_tuning(record, size, out, issues)` takes one record's bytes
and never throws. It refuses, each with the record offset of the offending
field:

- a null record, and a size that is not exactly `0xC90`;
- any non-finite float, by its own offset - 338 fields are checked;
- any of the 45 reserved words that is not zero;
- a mass scale outside (0, 2], or a yaw inertia scale not above zero;
- a final drive not above zero, or a rear drive fraction outside [0, 1];
- a gear table whose reverse ratio is not negative, whose neutral slot is not
  zero, whose forward ratios do not fall from first gear up, which gives no
  gear or more than six, or whose stored count is not the count its ratios
  give;
- a tyre block whose slip scale, peak friction or sliding friction is not
  above zero;
- a strut block whose spring rate or bump limit is not above zero, or whose
  droop limit is not below zero;
- a nitrous entry whose capacity or rate is not above zero, whose count is
  outside 1..64, or whose count and capacity differ;
- a negative mass, friction or grip item scalar, or a drive-split override
  outside [0, 1].

`check_vehicle_tuning` applies the same checks to a table built in memory,
except the 45 reserved words: those are not fields of `VehicleTuning`, so only
`decode_vehicle_tuning` sees them (review note N3). `encode_vehicle_tuning`
writes a table back into a record image and touches nothing else, and
`compare_vehicle_tuning` returns the offset of the first field two tables
differ in.

`apply_vehicle_tuning(record, size, levels, out_record, out_size, issues)`
reproduces `0x00575960`: it copies the whole record and rewrites
`+0x110`..`+0x3BF` in the order and with the arithmetic of the seven callees.
It refuses a record either decoder refuses, a size that is not `0xC90`, and a
level outside 0..3 - which the game silently ignores instead, and a tool that
is handed 7 has been handed nonsense. A refused call leaves the output
untouched. Where the game keeps a two-operation expression on the x87 stack
and rounds once at the store - the mass multiply and the three inertia entries
- the reproduction evaluates the same expression in binary64 and rounds once
at the same place.

`stock_240sx_tuning()` is the 240SX's tables in the source, under the owner's
rule of PR #169 comment 3860, as `stock_240sx_handling()` is: the header
states the source file, its SHA-256, the chunk, the record and its offset, and
the SHA-256 of the bytes the table reproduces, and a test rebuilds them.

## The probe

```text
nfsu_probe vehicle-tuning <GLOBALB.BUN>
```

Reads the file read-only, decodes all 35 records and prints one line each -
the three mass scales, the three gear counts, the three tyre peak pairs, the
three spring-rate pairs, the drive-split override, how many of the record's
six strut blocks have `+0x1C` equal to `+0x14`, and the SHA-256 of the bytes
the encoder writes back - then the record count, how many decoded, how many
were refused, the byte count, that equality count over the whole file, and the
SHA-256 over all of them. For the 240SX
it compares the decoded tables with the built-in ones field by field and
prints `equal to the built-in 240SX tables`. It exits 0, 1 on an argument it
refuses, and 3 on a file it cannot read, a record it refuses, or a 240SX that
does not match. Run it to reproduce the retail figures quoted above; this
document copies no digest it prints.

## Tests

`nfsu_vehicle_tuning_tests`, in both desktop builds. It reads no retail file.

| What | Pinned by |
| --- | --- |
| the built-in table's source | encoding it into an all-0x00 and an all-0xFF record image and hashing the bytes that are equal in both: their count, their run count, the first and last offset, and their SHA-256, which is `kVehicle240sxTuningBytesSha256` beside the table |
| the round trip | encode then decode gives the same table, plus seven values read by hand so that a uniform shift of every offset cannot pass |
| the refusals | every refusal above, each by the record offset it must name, plus the null, short and oversized record |
| the box inertia | the rule reproduces the 240SX's stored diagonal exactly, and scales as a box inertia must with the mass and the dimensions |
| applying nothing | the setup sub-object comes out byte for byte the same on the 240SX, which is what says the unconditional rebuild and the `+0x334` addition are no-ops on a stock car |
| applying everything | every table at level 3 with four items: the mass, the inertia that follows it, the gear count and ratios, the zeroed stored count in the table, the tyre coefficients with their item bonuses, the spring rates, the centre of mass and the four wheel heights, the redline, all nine torque points through both engine tables and the induction curve, the yaw inertia scale, the grip block, the nitrous block and the three downforce words in the game's application order |
| applying the items alone | the mass scale starting from 1.0, the drive-split override, and the three item curves in their order with curve C also landing in `+0x334` |
| the level gate | 0 to 3 accepted, -1, 4 and 7 refused with the output untouched, a short or null record refused, and a malformed table refused by `apply` with the decoder's own issue |
| the derived gear count | `test_derived_gear_rule` pins the recovered rule over nine cases, including a negative ratio and a NaN, which the equality stop counts, and a zero before a positive ratio, which ends the walk, and fails if a "greater than zero" reading would give the same answers; `test_gear_count_rule` drives synthetic one- to six-speed entries end to end |
| strut `+0x1C` | `test_strut_visual_limit` verifies that decode, encode, tuning application and runtime handling carry the visual limit independently from the bump limit at every level |
| the engine tables' ratios | `test_engine_table_ratios` holds the 240SX figures this document used to quote: the three levels as fractions of its stock torque curve, the two tables' equal torque deltas, the engine braking deltas not following that fraction, and table B alone raising the rev limits |

## What wiring it into the model would change

Nothing in this branch touches the driving model, and that is deliberate:
every one of the rules above changes a field the model reads, so wiring them
in moves the telemetry pins of [`VEHICLE.md`](VEHICLE.md) and the drive-replay
pin. A branch that does it should expect:

- **Even a stock car changes.** The inertia rebuild and the `+0x334` addition
  run with nothing installed. On the 240SX both are no-ops, which is why the
  demo's pins would survive that step alone; on the Eclipse, the RX-7 and the
  Skyline the `+0x334` addition is not a no-op, so those three cars have more
  torque than a reader of `+0x310` alone gives them. The right shape is for
  the record loader to run `apply_vehicle_tuning` with an all-zero
  `VehicleUpgradeLevels` and hand the model the result, so that the model
  never sees a raw record again.
- **The model needs fields it does not have.** The yaw inertia scale at
  `+0x270` selects the second inverse inertia, which
  [`VEHICLE.md`](VEHICLE.md) lists as an approximation because what sets body
  `+0x138` was not traced; the tuning tables make that field a tuning input
  and do not answer that question. The nitrous and grip blocks have no reader
  at all, so nothing can be wired to them yet.
- **The shift thresholds move.** They are built from the ratios and the torque
  curve, so a gearbox or engine upgrade rebuilds them; the search rule is
  already recovered and needs no change.
- **What would confirm it.** The run to make is a console lap, or the owner's
  photo of one, with a car the game itself has upgraded to a known set of
  levels, against this project's `apply_vehicle_tuning` on the same levels:
  the 0-100 km/h time, the top speed and the upshift speeds of an upgraded car
  are three numbers the game prints on its own screens and the model prints
  from the telemetry. Short of that, the checkable claim is narrower and still
  worth having: the setup sub-object this code produces for a set of levels
  should be byte for byte what the game's own copy at `RideInfo + 0x60` holds,
  which a memory read of a running game under a known garage state would
  settle. Neither was done here, and until one is, the wiring is a
  reproduction of a rule rather than a measured match.
