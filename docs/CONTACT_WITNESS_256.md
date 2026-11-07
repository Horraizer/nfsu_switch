# Isolated current-hit contact witnesses

Research for [issue256 assignment8230](https://git.accad.pro/murzintsevav/nfsu-switch/issues/256#issuecomment-8230),
based on `31e3af4b665b9cd4cc93d9d13dccda505f301605` (PR288).
This is a bounded continuation of [PHYSICS_DISPATCH_256.md](PHYSICS_DISPATCH_256.md).
It adds original-byte executable geometry/response cases and a shared response
contract. It adds no production solver, placed-body decoder or gameplay wiring.

Evidence labels:

- S: authenticated original PE instructions, tables and conditional branches.
- E: those original x86 instructions executed by Unicorn on synthetic memory.
- R: measurements from the original running game. None were made here.
- P: a proposed consumer contract, not an original engine API.

The central E result is that current-hit, accumulated impulse and applied body
impulses are distinct channels. The actual state callback can apply additional
impulses before the solver's final pair. Solver return changes rigid momenta,
but does not refresh the sampled linear/angular velocity fields in these cases.
Those stale caches must not be reported as final post-impact speed or yaw rate.

## Reproduction

Run from this worktree, including when its ancestors contain non-ASCII text:

```powershell
python -m pip install --target build/256-witness-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/256-witness-deps'
python -B tools/contact256_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/256-witness-r1
python -B tools/contact256_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/256-witness-r2
python -B tools/contact256_test.py --report build/256-witness-r1/report.json --compare build/256-witness-r2/report.json
```

Use standalone Python 3.8 or newer. Output directories must be fresh. The tool
refuses an existing destination, workspace escape, non-ASCII relative output
path, and retail/protected `E:/Games` overlap. Input reparse points are refused.
The only executable input is `speed.exe`, 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
No executable or retail geometry is exported. Whole-retail directory/file,
size, mtime and SHA-256 manifests bracket every attempt, including failures.

`report.json` records inputs, both body identities, before/after raw state,
query scalar and flags, witnesses, normal/point, current-hit input, callback
return state, every impulse application and its return state, accumulated
vector, magnitude sum, hooks, tool hashes and source checks. `source.txt`
disassembles every executed original instruction; `executed_blocks` records
the contiguous visited ranges and their hashes. These are executed ranges,
not a claim that unvisited branches were decoded or tested. The separate S
assertions check specific ownership, call and store boundaries and car vtable
targets. Tests own numerical expectations; this document does not duplicate
the response pins.

No successful report is written before all assertions and the final manifest
comparison succeed. Missing imports, memory faults, instruction-budget exhaustion
and failure to return are failures, never passing negative contact cases.
Tests reject missing callbacks, invented runtime evidence, altered velocity
caches, stale tool hashes and absent/tampered supporting artifacts. Two distinct
fresh runs must agree. This is a regression/guard check, not a mutation score.

## Synthetic memory and explicit execution limits

E uses two kind-0 bodies with the original car vtable `0x006C8798`, each with
its own actor, rigid component and shape. These are manually assembled objects;
neither a retail car constructor nor live actor discovery supplies them.
The shape constructor `0x00473CA0` and its bounds setter `0x00473CF0` execute
on explicit synthetic box bounds. Shape type 3 here means the box support
implementation; it is not body kind 3 (a track polygon). Synthetic car boxes
are not recovered retail car hulls, and are not the named BB body from #259.

The fixture explicitly chooses positions, velocities, mass, identity orientation,
diagonal inverse inertia, state, four-wheel count and optional record seed.
Initial angular velocity/momentum are zero. The mapped scratch memory starts
zero; every other unwritten field therefore has an explicit zero assumption.
Both body+0x16 flags and backing markers are set; body+0x6C points to each
rigid component and body+0x140 to its actor. The same synthetic config object
is used by both actors. Physics selector is 3; actor optional feedback/effect
pointers are null. The source file itself supplies unmodified constants.

Global `0x0078A344` and `0x007361A8` are zero, disabling the optional
alternate-mode handling path. `0x0078A345` is one, bypassing the downstream
race-event branch at `0x00466286`. A synthetic world object and explicit
PRNG seed support the hard-hit branch at `0x00466113`; its random branch is
deterministic here, not a live game's seed. `0x0073AD34` is a chosen record
timestamp. No world clock, frame scheduler or physics integration is executed.

The original math initializer `0x005B065B(0)` copies its fallback table from
`0x006D4BE0` to `0x006D4AC0`. This is a declared synthetic selection of the
original scalar implementation, avoiding registry/CPU discovery and unresolved
imports. It is not evidence of the live process's selected math backend.
The x87 control word is explicitly `0x007F`; each independent top-level call
starts with an empty x87 stack. Live FPU settings and alternate SIMD rounding
are unmeasured. Original instructions are checked byte-for-byte before execution
and only original executable sections are allowed to execute.

There is one hook that changes control flow: after `0x00465D00` has returned,
`0x004658B5` jumps to the original epilogue `0x00465CF2`. The omitted tail
contains effects/audio/feedback work and is not assumed inert in a full game.
Its omission is logged in every hit. Geometry, mass, inertia, friction,
restitution, current-hit accumulation, the tested state callback, car impulse
scaling and rigid impulse application execute original instructions without
replacement formulas. Observation hooks only copy state into the report.

Pair/job/record allocation and lifecycle are synthesized: pair+0x258/+0x25C
hold A/B, pair+0x264 points to the existing record, record+0x64 points back,
and job+0x0C/+0x10/+0x14 hold A/B/pair. This bypasses the solver's null-record
allocator, broad phase, eligibility, ordering, queued time search, separation
preparation and invalidation. No E case proves those upstream phases.

## Source-addressed ownership and observed behavior

| Boundary | S/E observation |
| --- | --- |
| `0x004727C0` | Stores shape A/B at pair+0x244/+0x248 and initializes dirty/threshold state. Bodies remain the distinct owners at pair+0x258/+0x25C. |
| `0x00472830`, ESI pair, EDI origin A; stack matrix A, matrix B, origin B | Compares origins and refreshes cached transforms when dirty. A rotated non-cubic box with unchanged origins retains its prior result until the fixture explicitly sets dirty. Translation then invalidates it through the actual comparison. Other engine invalidators remain necessary. |
| `0x00472950`, EAX pair | Runs actual `0x00471DE0` and box support/simplex helpers, stores scalar+0x10 and witnesses+0x20/+0x30, derives distinct flags+0x14/+0x15, clears dirty. The scalar can be zero despite nonidentical witnesses; it is not signed penetration. |
| `0x0046B8C0`, EDI job, EAX normal output, ECX point output | Point is witness midpoint. The two flagged bodies use normalized B-minus-A witness separation; exactly coincident witnesses exercise the body-origin fallback. Unseparated overlap can yield a reversed witness normal. |
| `0x0046BAE0`, stack job | Solver uses relative contact velocity and only responds to the tested closing cases. It does not itself reject the forced distant pair. |
| `0x00465830`, stack record, point, current vector, tangent speed | Called from `0x0046C3EE`, with the A-directed current vector. Accumulates vector and sum of magnitudes, then calls `0x00465D00`. Large seeded accumulation alone does not trigger the current-hit hard gate. |
| `0x00465EB4/EC0/EC3` | Hard current hit sets record+0x10, car state+0x38 and clears state timer+0x4CC. Both actors are processed through the pair. |
| `0x00466094`, `0x00466283` | The tested hard-hit state callback itself calls body virtual+0x4C for extra impulses. The report's return addresses are `0x00466097/0x00466286`. |
| `0x0046C3F3..0x0046C52C` | The now-set record gate and four-wheel states project final arms onto the plane perpendicular to actor up. An already-state1 fixture with a fresh clear record does not set that gate through this hit. |
| `0x0046C5D2/E4` | Final solver applications occur B then A, after the callback. Return addresses are `0x0046C5D5/E7`. Every application has its own body, arm, vector and before/after snapshot. |
| `0x0046A260 -> 0x00584F60` | Applies original car mass rescaling, angular-state scaling and rigid clamps; stores linear momentum+0x70 and angular momentum+0x80. Position/orientation and raw velocities+0xA0/+0xC0 remain unchanged at this boundary. |

The near-contact boxes use a small positive gap rather than silently applying
an invented penetration correction. Their source flags are reported individually;
no flag is relabeled a universal scheduler admission decision. Box support can
select a corner even in the symmetric head-on case. Thus a centered approach
does not justify replacing the original witness with an assumed face center.

`contact256_test.py` covers head-on, offset, glancing, separating, stationary,
query-only no-contact, deliberately forced distant solve, hard current hit,
already-state1, seeded accumulator, light mass, slow closing, coincident-witness
fallback and unseparated overlap. The last two diagnostic admission cases are
not claimed reachable through the original scheduler. The distant query case
omits solver entry by harness choice, not by executing the game's scheduler.
The cache sequence separately tests clean, resized/forced dirty, rotation-only,
explicit dirty after rotation, and translated state.

Untested branches include other body kinds, polygon normals, shape type 1/2,
real vehicle hulls, unequal mass/inertia/rotated dynamic bodies, external record
creation/lifetime, friction against an immovable body, optional mode globals,
alternate PRNG seeds, wheel counts, callback race events, effects tail,
non-finite/degenerate inputs and simultaneous/repeated scheduled resolution.
The seeded record tests channel ownership, not a repeated-contact episode.

## Shared response contract for #257/#259/#270/#222

P contract `contact256-v1` extends `physics256-v1` without selecting a new
production solver or claiming the original internal data layout is a public API.

| Consumer boundary | Required data and ordering |
| --- | --- |
| Admission and preparation (#257/#259) | Stable A/B identities and actual body kinds; shape/backing identities and provenance; source selector, membership, time domain, eligibility and pair order; prepared poses, both witnesses, raw scalar, both flags, dirty/shape-state history. Preserve how the pair was admitted. Direct solver entry is not an admission test. |
| Response entry (#257/#259) | Snapshot each actor/body/rigid separately: pose, raw cached velocities, linear/angular momenta, inverse mass/inertia, collision state, wheel gates and record identity. Record normal/point actually returned by geometry. |
| Current hit (#222/#270) | Emit one correlated event containing current vector, point, tangential speed, pair/record identity and event order. Keep accumulated vector and magnitude sum separately. Do not derive current-hit severity from either accumulator or net body momentum delta. No audio bank/sample mapping is added here. |
| State callback and applications (#257/#259/#270) | Preserve callback state transitions, record gate and every extra application, then the final B/A applications and their possibly changed arms. Tag each application by source boundary and receiver. Do not assume all response is one equal-and-opposite vector pair. Preserve named object identity across activation/lifecycle elsewhere. |
| Publication (#257 and actor consumer) | Keep solver return distinct from refreshed rigid kinematics and final actor publication. This probe supplies momenta and unchanged raw caches; final speed/yaw rate and yaw/pose after integration remain unavailable. Refresh/publish through the recovered physics contract only after the responsible phase is established. |

For #259, this supplies the shared event/state vocabulary, not kind-4 mass,
geometry, backing behavior or a named placement. For #270 it does not prove
activation, breakage, knockdown, settling or reset. Neither task should create
a parallel solver from the synthetic boxes. For #222 the executed callback
and the deliberately omitted audio tail are different evidence boundaries.

## Remaining acceptance and smallest next capture

R remains unavailable: no game was launched, attached, patched or written;
no event0 scene, hardware acceptance or global 120Hz cadence is claimed.
The remaining original-parity blocker is a correlated original episode with
real geometry, preparation/admission and post-response publication. The smallest
useful next capture is one identified two-car current hit plus a matched
no-contact approach, at the pinned executable identity and known loaded base:

1. Record pair/job/record/body/actor/rigid identities, actual hulls and vtables,
   scheduler branch, selected time, eligibility and pose/separation preparation.
2. Capture entry/return of `0x00472830/0x00472950`, dirty-state writers,
   both witnesses/scalar/flags and `0x0046B8C0` normal/point.
3. Capture solver entry; `0x00465830` current vector; `0x00465D00` state
   transitions and its internal applications; every virtual+0x4C application,
   its receiver/vector/arm and resulting momenta.
4. Continue through the actual kinematic refresh and actor publication to
   obtain final velocity/yaw, correlated with world call and input identity.

Repeat with named kind-4 and prop-lifecycle cases before those consumers claim
parity; preserve #259/#270 ownership. Missing live evidence blocks that parity,
not review of this bounded research delta. C++ default/Release, Switch and
deployment gates do not apply to these docs/Python changes and were not run.
Inherited production in the full branch-to-main diff is not validated by these
research gates; this PR alone is not a tested gameplay integration candidate.
