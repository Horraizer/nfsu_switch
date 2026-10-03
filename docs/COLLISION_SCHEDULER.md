# Collision scheduling and capture boundary (issue #256)

This continuation corrects the dispatcher premise in
[COLLISION_PAIR_TRACE.md](COLLISION_PAIR_TRACE.md), traces both live scheduler
branches and adds duplicate hull provenance for the named pylon. It changes
no gameplay solver. The source base is
`8090cb058c658089c83cc101299861754e03504e`; addresses refer to the pinned
PC executable in [WORLD_OBJECT_BODIES.md](WORLD_OBJECT_BODIES.md).

Evidence types are deliberately separate:

- Static original-file evidence: decoded instructions, constants and asset
  record provenance checked by the reproduction below.
- Synthetic Python replay: list writes and the bounded comparator branch,
  tested with constructed inputs. This is neither x86 emulation nor native
  execution of the original routines.
- Original runtime evidence: unavailable. No game was launched, attached,
  patched or measured. Native process discovery on 2026-10-03 found no running
  `speed` process. No synchronized contact capture or original save/input
  scenario was supplied. A successful static probe cannot fill that gap.

## Corrected caller: serialization is not the collision clock

`0x0041FD76` is an interior block of `0x0041FC00`, not a function entry.
The real immediate call is `0x00421E97 -> 0x0041FC00`, inside the
`0x00421E80` serialization path. The caller opens the `RaceCoordinator`
label at `0x006CAE64`; the callee opens `World` at `0x006CC0A0` and
passes field addresses/sizes to `0x00571C20`. That helper's `+0x3D`
branch calls `0x00571BA0`, which copies bytes from buffer `+0x158` and
advances cursor `+0x150`. This establishes read/reconstruction semantics,
independently of a guessed function name.

The labels preceding the calls formerly described as collision dispatch
are `AllCollisionBodies`, `AllTrackPolygonCollisionBodies` and
`AllCollisionBodyWitnesses` at `0x006C888C/0x006C8814/0x006C88A8`.
The conditional cleanup/rebuild and serialization of pairs explain why
those functions appear together here. Counting this block's calls would
measure a serialization path, not the live contact schedule. This corrects
the earlier timing paragraph; the earlier pair and BB constructor evidence
does not depend on that paragraph.

## Live driver update and unresolved integration placement

The live call is `0x00447969 -> 0x00420B40`. Its second argument comes
from the returned time at `0x00447939` (`0x0043A8A0`), after
`0x0044792B -> 0x00420110`. Record the actual argument; neither the
render rate nor an integer world-clock counter supplies it by definition.
The source order within this invocation is:

| Stage | Source | What it establishes |
| --- | --- | --- |
| CarDriver update | `0x00420DA7..0x00420DD5` visits the world car array at `+0x28`; `0x00420DA9` reads actor `+0x30`, then `0x00420DB2` calls that driver's virtual `+4` with the supplied dt. | Driver methods complete before the following collision dispatch. This is not a direct call to actor `+0x34` physics. Runtime array membership and actual virtual targets must be recorded. |
| Car preparation | `0x00420DE6..0x00420E03` calls `0x004267A0` for each car, then `0x00420E05` calls prop maintenance `0x00595670`. | These execute after the driver loop and before either collision branch. |
| Collision branch | `0x00420E0D..0x00420E26` reads `0x00736200`. Nonzero calls `0x00420890`; zero calls `0x00420310`, then `0x00420380`. | Two different algorithms exist. The selector is outside the PE's raw file-backed sections, so this tool deliberately reports its live value as unavailable. |
| Post-contact car update | `0x00420E37..0x00420E4B` calls `0x00426930` for each car. | Capture here separately from immediately after impulse application. |
| Clock accounting | Frame tail `0x00447A15 -> 0x00585D70` updates whole-tick counters and the remainder from frame time. | The 60-based conversion in that helper does not call `0x00420B40` repeatedly. It cannot prove 60 or 120 collision passes per second. Pause, playback and frame-time gates need runtime evidence. |

The receiver ownership matters. Controller replacement stores the controller
at actor `+0x2C` (`0x004260D4`), reads controller `+8`, and copies that
CarDriver alias to actor `+0x30` and physics `+4`
(`0x004260D7..0x004260E7`). One physics-replacement branch calls
`0x004740D0` at `0x00426230`, stores its return at actor `+0x34`
(`0x00426235`), and relinks physics `+4` to the driver at
`0x0042628C..0x00426292`. The AI driver installs vtable `0x006C8C18`
at `0x0045DBE9`, with virtual `+4=0x0045E970`. The human driver installs
`0x006C8AD0` at `0x00460E8B`, with virtual `+4=0x004615E0`. These
checked source targets differ from the separate physics target below; no
live object selection follows from the static tables alone.

The separate physics constructor installs vtable `0x006C8468` at
`0x00474110`, with `0x00474A90` at `+4`. Within that physics method,
`0x00474BE4..0x00474C4B` divides its supplied dt by float `0x006FFF10`,
converts to an integer through `0x00674898`, adds one and integrates equal
slices through `0x0046AA40`. The tool prints the exact source float.
This is a local subdivision observation, not a recovered world-to-physics
call chain. The intervening dispatch, its gates, supplied dt and integration
placement relative to the world's collision branch remain untraced here.
In particular, no claim is made that the world's driver call directly invokes
this method, that every driver selects it, or that its slices each schedule
a collision pass. A fixed 120 Hz collision cadence remains unestablished.

At `13c1fc8`, this document incorrectly attached that physics subdivision
to the world's actor `+0x30` call. This correction independently checks the
stores and tables identified by [PR285 at7c6a5e3](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285),
and removes that asserted linkage. The reproduction now checks the receiver
and both object stores as well as the three distinct vtable slots; it records
the missing dispatch and placement as null fields. The append/lookup,
separate collision selector and branch-local evidence below are unaffected.

This is a conditional static call order, not an observed race timeline.
One world call can resolve several contacts; that is different from several
world calls. Local physics slices also do not establish the world-call count.

## Pair creation, filtering and solve order

The zero-selector path first snapshots bodies with byte `+0x16` set in
registered-list order and invokes their virtual `+0x28` with dt
(`0x00420310..0x0042037C`). `0x00420380` then:

1. Walks the existing pair list `0x00779BE0`. Pairs without records are
   removed when horizontal bounds fail, neither body has `+0x1D`, or a
   virtual `+0x50` predicate fails (`0x004203D0..0x00420458`). A record
   takes a different retention path; absence of a new allocation is not
   absence of a live contact.
2. Visits registered bodies with both `+0x16/+0x1D` and invokes
   `0x00469580` (`0x00420487..0x004204AB`). This is the complete entry
   containing the prior report's `0x0046959A` slice. It refreshes track
   candidates first, then scans the registered-body bounds, skips self,
   checks the body-ID bit matrix and eligibility predicate, converts
   kind-2 proxies, and allocates overlapping pairs. Pair construction
   appends to the sentinel tail (`0x00466D43..0x00466D53`).
3. Visits pairs in list order and calls `0x00468060` to query and enqueue
   jobs (`0x004204E0..0x00420508`). Its kind-4 filter allows a kind-4 body
   only against kind 0 or 1, symmetrically (`0x00468079..0x004680B0`).
   Thus 0/4 remains eligible, but 4/4 and 4/3 do not pass this filter.
   This is additional to bounds/flags/predicates, not a universal yes/no
   contact rule. Kind 0/0 and 0/3 do not enter that kind-4 exclusion.
4. Constructs a job through `0x0046AE40` with bodies at job `+0xC/+0x10`,
   pair at `+0x14`, and a searched time at `+8`. `0x0046AE60` repeatedly
   samples poses through virtual `+0x30` and queries witnesses. It bisects
   a bounded interval up to eight times, consulting the two witness flags
   (`0x0046AF71..0x0046B01B`). This is separate from model integration.
5. Inserts jobs using `0x00564940` and comparator `0x0046ADC0`, then
   processes the queue head with `0x0046B5E0` at `0x0042054A`.
   For finite job times and the pinned negative float at `0x00700088`,
   the absolute-difference test takes `0x0046AE1A`. Insertion is descending
   by job `+8`; equal keys scan past existing entries and keep insertion
   order. The nearby track-priority helper branch is not selected by that
   pinned negative threshold for finite inputs. This does not establish
   that the threshold was never changed in a live process.
6. After resolution, removes queued jobs touching the changed bodies with
   `0x0046AC90`, handles kind-1 child replacement, regenerates their pairs,
   and re-queries affected pairs (`0x00420558..0x004206C0`). Source limits
   at `0x00700E5C/+4` bound total jobs and per-pair repetitions; the tool
   reads their values. Therefore initial insertion order and the initial
   sorted queue alone do not determine the whole pass.

For the nonzero-selector path, `0x00420890` refreshes swept bounds via
`0x00468D70`, sets up groups, then calls `0x00592C50` at `0x004209BE`.
That is the higher-level caller of the prior object-pair path. Group pair
creation goes through `0x00593170/0x00469770`. It does not prove a separate
traffic or opponent impulse rule.

`0x00592E80` searches group contacts through `0x00467700`, retains a next
hit/time at group `+0x18/+0x10`, and `0x00593100` orders the group list by
ascending `+0x10` (strictly smaller inserts before; equal scans past).
`0x00420A30` repeatedly advances the current group through `0x00592D50`;
that function uses elapsed/target time and the minimum interval at
`0x006EF2C8`. Its `0x00592CB0` path deduplicates body updates by body ID.
`0x00592FD0` resolves through `0x0046B790`, refreshes affected pairs and
reorders the group. The default group `0x007377F0` is advanced afterward
at `0x00420A85`. Runtime group membership, ties, selector and list
mutations must be captured before claiming a global order for event0.

The class matrix is consequently:

| Pair | Source-supported path | Still missing |
| --- | --- | --- |
| 0/3 | Track candidates precede registered candidates within a body scan; both scheduler branches reach the common response. Polygon-normal handling applies. | Actual polygon, pair/job order, dt, witness and pre/post states. |
| 0/4 | Registered overlap plus eligibility and kind-4 0/1 admission; common response. | Named kind-4 placement, descriptor/activation, actual chosen scheduler and hit. |
| 0/0 | Registered overlap, self/duplicate and eligibility checks; common response. | Player/traffic/opponent identities, controller/setup differences and actual order. |
| 0/2 -> 0/1 | Proxy conversion occurs before the resulting pair; lifecycle is owned by #270. | Runtime definition/shape choice and activation episode; this does not fill 0/4. |

## Witness, contact geometry and impulse boundaries

`0x00468060` calls `0x00472830` then `0x00472950` at
`0x0046814D/0x00468154`; time search has another such pair at
`0x0046AFCD/0x0046AFD5`. The latter uses sampled poses, so not every
witness query describes the bodies' committed current poses.

`0x00472830` compares origins against cached `+0x40/+0x50`, with the
coordinate tolerance at `0x006CC8BC`. It sets `+0x240` dirty on a failed
comparison; an already-dirty pair also refreshes the matrices. This routine
does not compare orientations to decide dirtiness, and it does not clear
the dirty byte itself. Capture other writers rather than assuming rotation
alone must be detected here.

`0x00472950` returns the cached scalar if clean. If dirty it invokes
`0x00471DE0`, stores its scalar at pair `+0x10`, copies witnesses to
`+0x20/+0x30`, sets `+0x15` for scalar <= float `0x006B71E4` and
`+0x14` for scalar <= float `0x006B71E8`, and clears dirtiness. These
are finite-value comparisons. The geometric routine returns either zero
(`0x00472356`) or a square root (`0x00472385..0x00472390`); this is
not evidence of a negative signed penetration depth. Retain the raw scalar
and both flags; do not rename a zero scalar to a measured penetration.

At `0x0046BAE0`, geometry helper `0x0046B8C0` computes the contact point
as the midpoint of the two witnesses. When both bodies have `+0x16`, its
normal is the normalized second-minus-first witness vector; a sufficiently
small length takes the body-origin difference fallback
(`0x0046B991..0x0046BA42`). `+0x16` is not inverse mass: an unbacked
kind-4 object may still have it set. If one body's flag is clear, the
helper calls `0x0046B070`; that reads the polygon normal through body
`+0x60/+0x70` and has sign/shape-state branches. Do not substitute the
witness-difference rule for that path. Capture the helper's actual output.

The source provides three different impulse/state observation boundaries:

| Observation | Exact source boundary | Meaning |
| --- | --- | --- |
| Response-entry state | `0x0046BAE0` after the caller has positioned/prepared bodies; job passed at entry `[esp+4]`. | Snapshot both bodies here, not only at the enclosing frame start. |
| Current-hit event | Entry `0x00465830`: `[esp+4]` record, `[esp+8]` contact point pointer, `[esp+0xC]` current vector pointer. Called at `0x0046C3EE`. | This vector is forwarded at `0x00465850..0x004658B0` to `0x00465D00`. Record accumulated vector `+0x50..+0x58` and sum of magnitudes `+0x60` separately. |
| Applied body impulses | Indirect calls at `0x0046C5D2` and `0x0046C5E4` to body virtual `+0x4C`. At each call site ECX is the receiver, `[esp]` vector pointer, `[esp+4]` contact-arm pointer. | Save each actual argument and the body after that call. The current-hit callback occurs earlier, and source adjustments/callbacks can intervene. Do not silently equate every vector channel. |

`0x0046B5E0` calls body virtual `+0x2C` with the job time, writes poses,
refreshes body state, calls the solver, then calls virtual `+0x28` again
(`0x0046B603..0x0046B77A`). `0x0046B790` has its own preparation path.
Thus frame-post, solver-post and post-current-hit state are distinct.
Car hard-hit state and impulse-class gates remain documented in
[COLLISION_CLASSES.md](COLLISION_CLASSES.md); they are not reimplemented
or revalidated as dynamic episodes here.

## Named placement: duplicate definitions AND duplicate hulls

The candidate remains `ZCS_PylonA`, hash `0x5087DB01`, through the distinct
VirginSmokeable kind-2 to Smokeable kind-1 path. The tool independently
checks placement 810656, section 1204/instance 49/model `0x090FE489`,
against STREAML1RA scenery record 47241648 and object directory record
47252384. Placement 810768 is the neighboring instance 50/scenery 47241696.
Translations must match exactly and the model must occur in the scenery
record's mesh slots. Offsets in TRACKB1003 are decompressed byte offsets;
STREAML1RA offsets are file offsets. Active event0 route membership is
unavailable, not implied by track membership.

New hull provenance is in the track's `0x80034020` container. Two distinct
`0x00039200` headers match the definition's `+0x80` hash:

| Header chunk / aligned record | Point chunk / aligned start | Index chunk / aligned start |
| --- | --- | --- |
| 227576 / 227584 | 227672 / 227680 | 228064 / 228080 |
| 228152 / 228160 | 228248 / 228256 | 228640 / 228656 |

`0x00472B00` links those headers at the shape-list tail, points shape
`+0x54` at the aligned 0x30-byte point records and `+0x50` at the u16
index array, and sets shape type 1. `0x00472EE0` reads a point from
record `+0x10`. `0x00472C60` searches forward from `0x00779BD8` for
the first matching shape `+0x18`. The constructor obtains that hash from
definition `+0x80`; definition `+0xB0` supplies a separate pivot argument.
On the first unadjusted shape use, `0x00472E40` subtracts the pivot from
the stored points and marks shape `+0x3E`. Therefore the selected shape,
selected definition, and prior pivot adjustment all matter to a runtime
hull. A mesh-derived box cannot replace this evidence.

Definitions at 257504 and 257760 remain separate, with differing descriptor
hashes and pivots. The tool preserves both and both hull-record hashes.
It never zips the two arrays into an asserted definition-to-hull pairing.
For an initially empty list and just this file loaded in array order, the
synthetic pointer-write replay yields the first definition and first shape
as forward matches. It does not establish a live winner after all loads,
unloads and earlier shape uses.

This confirms, and does not close, [PR272 finding 6814](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/272#issuecomment-6814):
definitions append, they do not prepend. The review's correction is retained;
this branch does not modify PR272 or its worker tree. A complete original
runtime selection remains missing even after the local order is understood.

The candidate definitions have zero activation and break fields in this
input; the report prints them instead of selecting a mass by name.
`0x00597980`'s zero-threshold branch can activate through `0x00597C90`;
the latter changes active/update flags. Nonzero-threshold and break-budget
branches are different paths. This establishes possible activation semantics,
not an observed knock-down, break, settle, reset or streaming lifetime.
Those transitions belong to #270; shared response belongs to #259. A named
kind-4 instance is still required separately.

## Reproduction and next original capture

From the assigned worktree, with standalone Python 3.8+ and Capstone:

```powershell
python -B tools/collision256_trace_test.py
python -B tools/collision256_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/256-evidence
```

The output directory must be fresh, ASCII relative to the actual workspace,
inside that workspace and outside retail/protected E:/Games. Pinned input
size/SHA identities come from `world_body_trace.INPUTS`. Complete before/after
retail manifests include paths, directories, sizes, mtimes and file hashes.
Every bounded instruction range must decode fully; independently specified
call boundaries/targets, receiver/alias/physics stores and distinct driver
and physics vtable slots are checked. Reports include range hashes, source
constants, duplicate record provenance and explicitly synthetic list/queue
results. No executable, hull vertices or decompressed asset bytes are saved.
Synthetic tests exercise duplicate retention, malformed input refusal, queue
ties and output guards; they make no original-runtime assertion.

The next capture requires the pinned executable running in an identified
original event0 scenario, with an authorized bounded debugger/trace facility,
save/profile and deterministic input/initial-state description. No such
facility or running episode was available to this pass. Use source VAs only
after checking the loaded image base and executable identity. A stopped
snapshot alone cannot measure cadence; debugger overhead must be recorded.

Capture these correlated events in one sequence with monotonic event number,
thread identity, frame/world-call identity, wall-clock timestamp and game
clock. Retain source addresses and raw numeric fields, never fill an unknown
with zero:

- World entry/return `0x00420B40`, supplied dt and live `0x00736200`;
  actor `+0x2C/+0x30/+0x34` identities and actual vtables; driver virtual
  entry/return, the still-missing driver-to-physics call chain, its gates and
  supplied dt, and actual physics substep boundaries. Correlate them with
  the selected collision branch and post-contact boundary `0x00420E2B`.
- Pair construction `0x00466CE0` (entry ESI pair, EAX body A, stack body B),
  body IDs/kinds, bounds/flags, source geometry, list/group identity and each
  filter result. Record failed/no-contact candidates as well as contacts.
- Every witness call/return with pair identity: `0x00472830` entry ESI pair,
  EDI first origin and three stack arguments; `0x00472950` entry EAX pair.
  Include dirty/type flags, both transforms, witnesses and raw scalar. Keep
  sampled-time queries distinct from current-pose queries.
- Queue/group insertion, selected job, searched time, re-query/invalidation
  and repeated resolutions. A final pair list is insufficient to recover
  temporal order or discarded jobs.
- Solver geometry helper input/output; both bodies' pose, linear/angular
  velocity, descriptor/mass/inertia and state before response; current-hit,
  accumulated and both applied vectors at the separate boundaries above;
  states immediately afterward and after the enclosing world call.
- For the pylon, loaded source records, definition pointer, shape pointer and
  pivot-adjusted state, placement/body identity, proxy conversion, activation
  callback and visible transform. For kind 4, add the still-missing named
  source record and backing gate `+0x60`, interfaces `+0x68/+0x6C`.

The original contact matrix remains unperformed: 0/3 wall at several angles,
0/4 named immovable/backed object if present, 0/0 stationary/moving traffic
and opponents, no contact, repeated contact, and competing simultaneous
contacts. The pylon adds a separately labeled 0/2 -> 0/1 lifecycle case.
Each needs matching input, body identities and pre/post evidence; a synthetic
fixture or port replay is not a retail measurement. Share those events with
#222 without inventing audio-bank mappings, and with #275 without calling
the static schedule an explanation of the owner's rollover.

This research narrows implementation prerequisites but leaves original
execution, dynamic fidelity and owner acceptance open. C++ default/Release,
Switch/shader/NRO and deployment checks are inapplicable to this four-file
docs/Python research delta; none are claimed. The assigned base is an unmerged
integration candidate, so the full branch-to-main diff also contains inherited
production changes. Their gates and known candidate blocker are not certified
by this research pass. Review the research delta against the assigned base;
the combined PR is not a validated merge candidate. Later actor/body/lifecycle
implementation must run its full gates. The owner baseline remains a 960x540
scene with GPU boost on, actual clocks measured separately; no hardware result
is supplied here.
