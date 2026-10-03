# Event0 actors and authoritative snapshot producers

Bounded research for [issue258](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258),
assigned by [comment7275](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258#issuecomment-7275).
Base: `10563e2555270cb564fac910f1f2e1ba9569d4e4`. This is an isolated,
unaccepted integration substrate. This research adds no C++, AI, solver,
renderer, race results or gameplay and does not validate that substrate.

The new evidence resolves registration order, separate controller/driver/physics
ownership, the status-to-finish dispatch order and particular map-gate writers.
It locates the gap-table constructor. A complete original controller, traffic
population policy and original event0 run remain unavailable. This is sufficient
to split the next source work and define an honest producer boundary, not to
declare the event playable or to replace opponents with a route animation.

Evidence labels:

- **S**: authenticated original-file instructions/data, statically inspected.
- **E**: original x86 executed by Unicorn on bounded synthetic memory. Declared
  hooks replace out-of-scope callees; this is neither the game nor native host
  execution of a complete actor, controller, race or physics solver.
- **R**: actual original-runtime measurement. None was obtained in this pass.
- **P**: proposed port interface or implementation policy, not a retail symbol.
- **U**: unresolved source/runtime evidence; never silently replaced by zero.

## Reproduce and inspect

Use standalone Python3.8+, Capstone5.0.7 and Unicorn2.1.4. From this worktree:

```powershell
$py = 'C:/ETAP 2102/ThirdParty/Python/Python384/python.exe'
& $py -m pip install --target build/258-astra-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH = Join-Path (Get-Location).Path 'build/258-astra-deps'
& $py -B tools/actors258_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/258-evidence
& $py -B tools/actors258_test.py --report build/258-evidence/report.json
```

Choose a new output name on every run. The tool refuses an existing output,
an output outside the current workspace, retail overlap and new non-ASCII
names. Existing Unicode workspace ancestors are used through runtime APIs.
It authenticates all four inputs in `event0_trace.INPUTS`, including the
uncompressed `Global/GLOBALB.BUN`, before interpreting them. Their sizes and
SHA-256s are recorded in the report; [EVENT0_SETUP_HUD.md](EVENT0_SETUP_HUD.md)
has the provenance table. The executable is 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.

`before.json` and `after.json` cover the entire retail root, directories,
relative names, file sizes, modification times and content hashes. Reparse
points are refused. The after manifest is checked even after extraction or
emulation failure. A manifest PASS alone is not an experiment PASS. No retail
binary, image, decompressed pack or geometry buffer is exported. Keep the
ignored reports local; commit only these tools and this source-addressed note.

`source.txt` contains decoded instruction listings. `report.json` records
range hashes, exact-boundary source assertions, source tables, event0/grid
parser output, dependency versions and E results. PE reads use section mapping.
Every selected span must decode completely. Named slices are just slices;
their branch destinations can be outside the slice. The E harness separately
whitelists all executed ranges and external hooks, fails on an undeclared
instruction or instruction-budget exhaustion, and never turns a crash into a
passing case. Tests hold the experiment expectations; numerical fixture
results are not copied here as current runtime measurements.

E coverage: grid selector (ground loader and factory stubbed), actual role
partition and lap registration (initial course update stubbed), status pass
after clock update (crossings, notices and finish side effects stubbed), actual
first-free-place routine, controller/driver scheduling with synthetic callees,
finish hide branch, physics selector and actual unlink loop with destructor
and allocator hooks. No bounded test measures elapsed time, steering quality,
dynamic contact, final crossing totals, RNG sequence or a live gate lifecycle.

## Instantiation, identity and destruction

**S.** Keep [#268 setup](EVENT0_SETUP_HUD.md) as the source of roster, presets,
eleven named performance levels, adaptive tuning, traffic config and transforms.
This probe independently authenticates and parses the event0 and grid records.
Its native grid fixture supplies the already recovered final grid assignments;
it does not execute the career builder or adaptive tuner.

`0x00422A70` iterates grid slots1..4, searching config records at
`0x0078A418` with count `0x0078A41C`, stride `0x750`. A config with signed
byte `+0 > 0` is skipped. For a matching grid byte `+3`, config `+0x734`
nonzero selects controller0; otherwise role1 selects1 and role2 selects3.
Each call `0x00422AEC -> 0x00422410` supplies that config, its own grid
marker and controller selector. With the recovered event0 grid, creation
order is BRIAN/player/LOLA/BRETT, distinct from the event's rival record order.
The factory allocates the actor and calls `0x00425350`, then appends the actor
to the world list at `0x0042254A..0x0042255D`. **E** tests selection/marker
association and omission of a flagged config; allocation and pose construction
remain S, not an emulated full car factory.

The constructor writes config pointer at actor `+0x14`, RideInfo pointer at
`+0x10`, sign-extends config byte `+1` into actor word `+0x0A`
(`0x00425598/59D`), and initializes actor index word `+8` separately.
`0x0041FF50` compares the signed word ID, not a name, rank or pointer index.
This gives racer identity within the rebuilt session; it does not prove a
global identity across teardown, retry or recycled traffic lifetimes.

`0x0041FE50` traverses the linked list in four stable role groups: humans,
opponents, roles4/5, traffic. It publishes pointers at world `+0x28`, cumulative
ends at `+0x1C/+0x20/+0x24/+0x18`, and rewrites each actor `+8` to the new
index. Given the recovered creation order, racer registration input is
player/BRIAN/LOLA/BRETT. **E** inserts another human before a rebuild and shows
indices move while IDs remain. This is not permission to use the static
fixture's pointer values or array position as production identity.

Role3 config creation is separate: `0x00422B36 -> 0x00598BF0`; only a
non-null return is appended, followed by another role rebuild at `0x00422B62`.
Event0's traffic setting requests the pool described in #268, but a requested
configuration count is not a simultaneous active-traffic count.

**S/E.** Teardown `0x00422B70` unlinks each live-list actor before calling
`0x00425990`, then frees it. The destructor disposes controller `+0x2C`,
physics `+0x34`, collision body `+0x3C` and other owned interfaces through
their own paths. It does not separately destroy the aliased driver `+0x30`.
Session teardown calls this at `0x004221B1`, frees status `+0x564` and status,
clears `0x0073619C`, destroys the world and clears `0x007361F8`.
The E test checks unlink-before-destroy and exactly one destroy/free per node;
it stubs destructor internals. Steady-state traffic recycling/removal and all
retry callbacks still require the runtime witness below. No generation counter
has been recovered; `session_generation` is P, not a claimed original field.

## Controller, driver and physics ownership

**S.** These three pointers are different objects, not three names for one:

| Actor field | Construction/ownership evidence | Relevant update |
| --- | --- | --- |
| `+0x2C` controller; selector `+0x24` | `0x00425FE0` destroys the previous controller, dispatches the selector table at `0x004260F8`, stores returned pointer | world calls controller virtual `+0x0C` |
| `+0x30` CarDriver alias | `0x004260D7..E7` copies controller `+8` into actor `+0x30` and physics `+4` | world calls driver virtual `+4` |
| `+0x34` physics; selector `+0x28` | separate `0x00426110`, table `0x004262A8`, relinks physics `+4` to driver at `0x0042628C..92` | actual physics dispatch needs its own call chain; not automatically the driver call |

The serialization names establish `RealController` at `0x006CAD04`,
`AIDriver` at `0x006CACE8`, `TrafficController` at `0x006CACD4`,
`AICarController` at `0x006C9990`, and `CarDriver` at `0x006CAD34`.
Controller selector1 constructs `0x0059BFC0` (RealController); selector3
constructs `0x004620C0` (AIDriver, derived through `0x0044A330`);
selector4 constructs `0x00582F90` (TrafficController). Selector2 is the
separate DragController; event0's ordinary rivals do not select it.

For ordinary event0, the factory gives the human physics selector1. Rival
selector3 calls `0x00425150`: outside its human/override/drag predicates it
returns physics selector3. These selectors are not collision-body kinds.
Physics selector1 constructs `0x00476840`; selector3 constructs `0x004740D0`.
No separate opponent or traffic restitution follows from controller ownership.

**Source qualification for [PR283 at13c1fc8](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/283):**
its live loop address is useful, but the direct identification of world call
`0x00420DB2` with model `0x00474A90` is not established. That receiver is
actor `+0x30`. The AI driver installs vtable `0x006C8C18`, whose `+4` is
`0x0045E970`; the human driver installs `0x006C8AD0`, whose `+4` is
`0x004615E0`. Vtable `0x006C8468` and its `+4=0x00474A90` belong to the
separate actor `+0x34` object. This probe checks all these distinct source
tables and stores. Internal physics subdivision cannot be attached to the
world's driver call without tracing the intervening dispatch. The collision
selector and before/after call boundaries remain useful; #256 owns the full
physics/contact schedule and actual witness measurements.

**U.** Controller type and route availability do not recover AI decisions.
The base AI initializer passes the actor and settings direction into route
state initializer `0x0044CD30`, invokes `0x00457060`, then resets route state
with `0x0044CE60`. That supplies useful entry addresses, not a complete route
choice, catch-up, obstacle avoidance, stall recovery or RNG contract. The
existing `path_follow` is explicitly a project controller in
[RACING_LINE.md](RACING_LINE.md); its deterministic seeds cannot establish
original opponents. Keep #63's remaining source work separate from actuation.

## Original order and the coherent publication boundary

**S/E.** Within one world call `0x00420B40`, after earlier world bookkeeping
and human pose caching, the bounded schedule slice is:

1. Traffic manager `0x00598600`, if world `+0x2F0` exists.
2. Role2 controller calls in world-array order (`0x00420C8A..CA`).
3. Role3 controller calls in world-array order (`0x00420CCA..0D0A`).
4. Role1 controller calls (`0x00420D0A..4D`), then a separate human-only pass.
5. Every actor's `+0x30` driver virtual `+4` (`0x00420DA7..DD5`).
6. Every car `0x004267A0`, prop maintenance `0x00595670`, then the selected
   #256 collision branch, then every car `0x00426930`.
7. Later distance/view/other world work, linked-list sort at `0x00421118`,
   each view `0x00430730`, other callback, then status `0x00435570` at
   `0x00421155`, immediately before world return.

The E test stops after the post-contact loop. It checks roles4/5 are not in
the three controller loops but are in the all-car driver/pre/post loops.
Zero supplied dt still reaches these calls; each callee and outer pause gate
has its own responsibility. This does not prove zero-time AI is inert. Neither
this call order nor world tick quantization establishes a fixed scheduler Hz,
number of substeps or exact input acquisition time. Record actual dt and calls.

**S.** `0x00426930` reprojects actor world position via `0x004443A10` at
`0x00426AFE` with destination actor `+0x800`. Projection writes normalized
coordinate at destination `+0x10` (actor `+0x810`) and world distance at
`+0x14` (actor `+0x814`). Failure branches and pose discontinuities are not a
validity-free successful route projection. This track-progress object is
separate from the AI racing-line selection state.

`0x00434A80` registers humans, then opponents, at contiguous status rows
`+0x30 + 0x13C*i` and pointers `+0x520`. `0x004333C0` sets row `+0` to the
actor and actor `+0x1C` back to that row, initializes timing sentinels, then
invokes the initial progress update. Registration sorts twice. Thus the
registration order supplies the tie input; exact initial rank still depends
on actual projected grid progress. E uses equal synthetic initial progress
and does not claim to have measured the original initial positions/ranks.

The status pass first handles countdown/race clock, then updates unfinished
rows through `0x004338C0` in the **previous** `+0x520` order. That routine
clears lap/split pulse bytes `+0x0D/+0x0E`, reads actor `+0x810`, and writes
crossing/lap data. At `0x00433DAE/DB1` it writes unwrapped progress to row
`+0x14/+0x20`. `0x004356D1` sorts by the new progress/finish-place rule;
`0x004356D7` handles rank-change notices. The ordinary lap-limit finish loop
is later at `0x00435A17..5F`, in this newly sorted order, calling
`0x004350D0` with place0, hide0, reason1. Already-finished rows are skipped.

Consequently same-pass finish dispatch is **not** the fixed config-ID loop
or controller update loop, nor a sort on crossing timestamps. Larger progress
sorts first unless both rows have finish places; equal progress retains the
previous pointer order. `0x00435BD0` finds the first unused positive place;
`0x00435399..B5` uses it and the stored crossing/lap-start data to latch total.
E proves the sort-before-finish dispatch, ties, skips and first-free search;
it does not execute crossing interpolation or full finish side effects.

**P.** Publish the port snapshot after the complete status/finish pass, using
the same pre-tail world clock that produced its times. Do not combine pose
from the next step, rank before sorting, total before finish or a tail-advanced
tick. The original does not expose a recovered immutable snapshot object;
this is an adapter over mutable state, with a deliberately chosen commit
boundary. View state read earlier in the original world pass may have a
different age; its one-frame presentation semantics need R evidence.

## Map gate writers and gap-table producer

**S.** [PR284 atf81b932](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/284)
requires actor `+0x4D0` and `+0x8A9` for markers and follows actual actor pose.
The constructor writes both to1 at `0x004257F7/0x004258C6`. The following
writers narrow the prior unknowns, without inventing names for all uses:

| Field | Proven writer | Limit |
| --- | --- | --- |
| actor `+0x4D0` | setter `0x00432D30`; traffic creation clears it at `0x00599565`; traffic suspend clears via `0x0059B75C`; resume sets/clears via `0x0059B801/81A` from body virtual `+0x50`; stage3/4 sets traffic at `0x00434CF2` | An activity-related gate shared with body/controller work, not just map visibility. All live transitions and the predicate still need a trace. |
| actor `+0x8A9` | finish helper `0x00435336..64` clears it only when its second argument is nonzero; also stamps `+0x884` and sets `+0x8A8` | Normal lap finish passes zero here. Do not define it as `!finished`. |
| same numeric offsets in other objects | e.g. `0x00583018` writes TrafficController `+0x4D0`, and `0x0046212D` writes AIDriver `+0x4D0` | These are not actor fields. An offset-only xref search is not a writer inventory. |

Actor serialization also passes addresses of these fields at
`0x00425D90/0x00425E63`; reconstruction is another potential writer. No claim
is made that the direct-store list exhausts indirect writes. The map's whole
component visibility and per-marker eligibility remain separate. Hide flags,
body activity, controller selection, finish state and allocation lifetime must
not be collapsed into one `active` boolean.

**S.** Gap table status `+0x564` has an identified producer, improving #268's
open item: `0x00434590` selects the relevant human RideInfo, prepares it,
reads signed category byte RideInfo `+0x424`, allocates the table, calls
`0x00432E80` and stores the pointer at `0x00434622`.
The constructor clears the table, walks route list `0x0077AF20`, indexes
category plus five times direction, checks route enable bytes, and samples
adjacent points of stride `0x88`. It reads s16 point `+0x3C + 2*index` for
lateral position and byte point `+0x6A + index` as a divisor in its rate
calculation. It projects those points with `0x004443A10` and `0x00434A10`,
stores a positive rate into an as-yet-empty progress bin, fills missing bins
from the previous rate (initial fallback `0x006CCD40`), and integrates
`0x006CC8F0 / rate` into the cumulative table. Constants are printed by the
probe. This is not a live `distance / opponent_speed` estimator.

**U.** The complete category derivation, point-field units and all-route
selection/overlap behavior have not been independently numerically replayed
on event0. No table values or resulting gaps are accepted by this work. The
next bounded gap task should run this constructor on authenticated routes,
compare `0x00433340/0x004353E0` consumers, and verify the R result. Until
then a port gap is unavailable, not a plausible hand-tuned conversion.

## Proposed adapter and remaining implementation work

**P.** Contract `actors258-v1` depends on the exact research head containing
this document and tools, the assigned #268 setup base above, and the pinned
#256/#266 references above. It names future producers; none is implemented.

| Published group | Authority and required lifetime |
| --- | --- |
| generation, event/track/direction, configuration identity | setup owner creates a new generation before start/retry; validate actual event0 presets, named tuning, grid and saved RNG state; keep old snapshots alive until readers release them |
| actor key and role | generation plus signed config ID, plus a spawn/incarnation key if traffic reuses IDs; never pointer, role-array index, name or standings index |
| allocated, simulation participation, raw map gates and validity | actor/lifecycle owner; publish direct observed gate values with provenance until the full state machine is recovered; removal publishes a tombstone or absence at the same boundary |
| pose/heading, controls, driver and physics selectors | actual independently owned actor state; config, controller and solver agree; preserve raw horizontal axes and u16 heading convention through a verified adapter |
| progress/timing/finish | actor progress after solver, then status pass; preserve invalid timing sentinels and crossing provenance, previous order for ties, finish place/total latch |
| standings and optional gap | status owner publishes stable actor keys and rank/previous rank after its sort and finish pass; gap includes reference actor and validity; traffic has no lap/finish row |
| HUD/minimap events and view state | events keyed by generation and sequence; presentation reads only; reset discontinuity explicit; map camera/filter state has its own verified producer |

Implementation sequence and dependencies:

1. Recover #63 route/goal/action decisions, input acquisition, per-controller
   seeds and recovery, and the full traffic activation/reposition schedule.
   Preserve #268's resolved adaptive tuning per actor. A requested traffic
   pool and three rival labels are not real simulated actors.
2. Reconcile #256's driver/physics pointer distinction, then complete #257's
   controlled two-car no-contact/contact solver path. Keep #259 shared response
   and #270 prop lifecycle separate; actors cannot manufacture their witnesses.
3. Implement a bounded actor owner with independent config, controller,
   VehicleState, course progress and timing; explicit role-order phases and
   identified solver placement. Start with the authorized one-opponent and
   one-traffic scenarios, then the actual event0 field. No decorative rivals.
4. Bind #254 timing, #266 map and #269 standings/results to one committed
   snapshot. Trace and test the gap producer before exposing numeric gaps.
   #67 owns pause/retry/quit generation and stale-event invalidation; a car
   reset stays within a race generation and does not reset accumulated timing.
5. Re-run applicable C++/Release/retail/Switch gates for that implementation
   and obtain real no-contact/contact, overtaking, reset/retry and full-race
   evidence. Single-car results cannot satisfy full-field CPU/GPU/memory cost.

## BLOCKED original evidence and exact next capture

**R unavailable.** Read-only process discovery found no running `speed`
process on 2026-10-03. No original event0 save/profile, deterministic launch
and input sequence, continuous run or authorized debugger trace was supplied.
No game was launched, patched, attached or saved by this worker. The existing
[#268 original clip request](https://git.accad.pro/murzintsevav/nfsu-switch/issues/268#issuecomment-7020)
remains open and is coordinated by the coordinator, not relayed by the owner.

To unblock actual original parity, provide an identified executable/profile
and event0 entry/input recipe plus a bounded trace facility. Record loaded
base, sequence, thread, world-call identity, supplied dt and world ticks:

- Config creation, final grid and all resolved RideInfo vectors/seeds; factory
  entry/return, actor ID/index, role, pointers `+0x2C/+0x30/+0x34/+0x3C` and
  their actual vtables. Capture traffic factory failures as well as successes.
- Controller entry/return and controls, selected AI route/point/lane/action,
  RNG state before/after, driver and physical integration calls, then #256's
  exact collision dispatch/contact witnesses. Include no-contact episodes.
- Each post-contact pose/projection, prior standings, `0x00435570` entry/exit,
  row pulse/timing fields, sort result and finish calls with all three args;
  two cars crossing within one world call in both orders and at tied progress.
- Writes to actor `+0x4D0/+0x8A9`, body activity, traffic relocate/destruct and
  the whole map visibility mask during grid, GO, pause, car reset, retry and
  finish. Keep same-offset controller fields distinct. Record new/reused IDs
  across retry rather than treating matching pointers as persistence.
- Gap constructor inputs/outputs and live gap consumers; original continuous
  HUD/map footage aligned to these calls, followed by real results and intro
  continuation. Clip-only evidence cannot recover internal ordering or seeds.

The smallest unblock is this original run/trace input, not authorization to
invent missing AI, seed or active-state rules. Runtime, hardware driving,
appearance/audio and event0 completion remain blocked. Future handheld
measurement uses `--scene-height 540 --gpu-boost on`; record measured clocks
separately. This research makes no hardware or original-runtime PASS claim.

The PR to main includes inherited pending substrate changes. Review this new
document and three Python files against the assigned base; also inspect fresh
`origin/main...head` scope. The coordinator must reconcile that inherited
delta before merge. This worker runs research/ASCII gates only and does not
claim inherited C++/Release/Switch validation or alter issue acceptance/state.
