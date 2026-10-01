# Retail collision pair trace (issue #256)

This is a static trace of the retail `speed.exe` identified in
`docs/VEHICLE_TRACK.md`. The inspected file is 3,178,496 bytes and its SHA-256
matches that document. Addresses below are virtual addresses in that image.
No retail collision episode was executed or measured for this issue. In
particular, a branch in the executable is evidence of a possible code path,
not proof of how often a race takes it.

## Pair sources and local order

| Source | Verified path | Limit |
| --- | --- | --- |
| Registered bodies | `0x0046959A` first refreshes polygon candidates through `0x00469810`. The scan at `0x004695C6..0x00469664` narrows the registered-body list at `0x00779C70` using the current body's bounds. The loop at `0x00469670..0x004696C4` checks two horizontal bound intervals. It then tests a body-ID bit in `0x0072CBFC` at `0x004696CA..0x00469707`, calls the other body's virtual `+0x50` predicate, optionally replaces a kind-2 body through `0x00596570`, and calls `0x00466600` for another overlap check. `0x0046973E..0x00469751` allocates and constructs a pair with `0x00466CE0`. | This is one creation path, not a proof that all contacts enter through it. The scan narrows by the `+0x2C` bound; its iteration order alone does not establish solver order. |
| Object-pair path | `0x00592C50..0x00592CA3` visits registered bodies with bytes `+0x16` and `+0x1D` set and calls `0x00593170`. That routine refreshes polygon candidates at `0x0059319A`, narrows the list by `+0x2C`, and calls `0x00469770` at `0x005932A9`. `0x00469783..0x004697C8` tests `0x00466600`, the same ID bit through `0x00468300`, virtual `+0x50` predicates, and an optional kind-2 remap through `0x005962C0`. It constructs with `0x00466CE0` at `0x004697DD` and checks `0x00467300` at `0x004697EA`. | The caller's higher-level grouping and the relative ordering of this path and the first path remain unproved. Do not turn the observed list order into a frame-wide contact order. |
| Track polygons | `0x00469831..0x004698B0` walks candidates at body `+0x64`, checks bounds, calls `0x00469A00` for an additional gate, and retains at most `0x30` candidates. `0x004698D0..0x004698F8` sorts retained entries by their `+0xC` field before the kind-3 polygon body construction at `0x004699B1` (`0x0046A440`). | The semantic name of polygon field `+0xC` and how candidate limits affect a particular race have not been measured. |

`0x00466CE0..0x00466D65` initializes the pair, calls `0x004727C0`
when both bodies have a shape pointer at `+0x10`, appends its `+0x250` node
to the `0x00779BE0` pair list, and stores body pointers at `+0x258/+0x25C`.
This establishes insertion order within that list, but the subsequent
processing order across all contact sources has not been traced. The
collision-record allocator `0x00467220` stores a record at pair `+0x264`;
its direct callers include `0x00466FC0` and `0x0046BB1C`. A pair can
therefore exist before it has that record. `0x00467300` checks the record,
overlap, body flags, and virtual predicates; its return value alone should
not be labeled a universal contact decision.

## Shape witness path

`0x004727C0` initializes pair witness state when the pair is built. At
`0x00472830..0x00472886`, `0x00472830` compares one input origin against
cached pair `+0x40..+0x48`, calls `0x00471D60` to compare the other against
`+0x50..+0x58`, and marks byte `+0x240` dirty if a comparison requires an
update. Its dirty branch `0x0047289B..0x00472942` refreshes cached
transforms and origins. This routine is a cache/transform update, not a
standalone collision yes/no test.

On a dirty pair, `0x00472950` handles special shape-type-2 state at
`0x00472961..0x004729E7`, calls the geometric witness routine
`0x00471DE0` at `0x004729F1`, stores its floating result at pair `+0x10`,
and copies witness points to `+0x20..+0x38`. It then sets bytes `+0x15`
and `+0x14` against two constants at `0x006B71E4/0x006B71E8` and clears
the dirty byte (`0x00472A33..0x00472A78`). The geometric algorithm inside
`0x00471DE0` and the meaning of those two thresholds need a separate trace.
Call sites at `0x004677E1..0x004677ED` and `0x0046AFCD..0x0046AFDA`
perform update then witness query in that order.

## Named kind-4 object: `BB`

The executable contains the ASCII tag `BB` at `0x006BA6B4`. The dispatcher
at `0x005785C5..0x0057860B` hashes that tag, compares it with the object
type value, requires object `+0x30`, and selects constructor `0x00592470`.
The other branch calls `0x00592280` at `0x0057862F`; this trace does not
equate the two data layouts.

The `BB` constructor passes kind `4` to `0x00468340` at `0x005924AC`,
installs vtable `0x006B8D88` at `0x005924F8`, and stores the object pointer
at body `+0x140` at `0x0059254F`. It passes data reached through the
argument at `[ebx+0x10]` then `+8` to `0x0053F5F0` at `0x00592587..0x00592597`.
That reader follows its input `+0xC` and copies two three-float vectors
from `+0x20..+0x28` and `+0x30..+0x38`; a null input yields two zero
vectors (`0x0053F5F0..0x0053F657`). `0x0059259C..0x005925E9` allocates
a box shape, passes those vectors to `0x00473CF0`, and stores the result at
body `+0x10`. `0x00473CF0..0x00473D5D` computes center as the average
of the two vectors and half extents as half their difference. This proves
a kind-4 `BB` bounding-box body data path, not which visible prop or race
asset instantiates it. Interpreting `BB` as "bounding box" is plausible
but remains an inference from the constructor.

## Timing and work still needed

The dispatcher at `0x0041FD76..0x0041FDE8` conditionally calls
`0x00593620` (or `0x004682C0`) before it calls pair processing
`0x00467010`; `0x00593620` itself ends in `0x004682C0`. This gives local
call order for pair cleanup and processing. It does **not** establish the
retail rate of that dispatcher, the number of physics substeps, or placement
of these calls relative to a 60 Hz gameplay tick. The port's 120 Hz vehicle
step is an approximation documented in `docs/VEHICLE.md`, not a measured
retail collision schedule. Do not wire a new solver to that assumption.

The next trace should (1) identify the caller and frequency of the
`0x0041FD76` dispatcher and its relationship to vehicle integration,
(2) trace how `0x00469810` fills and sorts its polygon candidates, and
(3) use a controlled retail/Deck scenario with one stationary `BB`
object, one traffic car, and one race opponent to record pair order,
witness flags, impulse, and post-impact velocity. Until those measurements
exist, use the class-specific observations in `docs/COLLISION_CLASSES.md`
as constraints, not as a complete collision model.

## Reproduction

Find the single 3,178,496-byte `speed.exe` below `E:/Games`, compare its
SHA-256 with `docs/VEHICLE_TRACK.md`, then inspect the virtual addresses
above as x86 32-bit code in a PE-aware disassembler. For the `BB` tag,
read the null-terminated bytes at `0x006BA6B4` and follow the branch at
`0x005785C5`. Follow direct calls from `0x00592C8A` to `0x00593170`,
from `0x005932A9` to `0x00469770`, and from `0x004697DD` to
`0x00466CE0`. No debugger timing or runtime contact trace was performed.
