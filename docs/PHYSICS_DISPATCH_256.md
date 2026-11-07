# Driver, physics and contact phase contract

Bounded follow-on to [issue256 assignment7370](https://git.accad.pro/murzintsevav/nfsu-switch/issues/256#issuecomment-7370)
and the scheduler ownership correction at
`c669599cf6cc802395cc2a3ab249a7985a912de2`.
[PR285 at7c6a5e3](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285)
is source context, not an accepted runtime integration dependency.
This delta is research only. Inherited production changes remain unaccepted;
no C++, solver, traffic policy, console deployment or issue completion is added.

The missing edge is **collision body -> actor helper -> separate physics**.
The world's driver update is an earlier phase, not that edge. Ordinary physics
selectors1 and3 select different implementations and different subdivision
constants. Neither a world-call rate nor a collision rate follows from those
constants. This contract enables #257/#258 to place independent actor physics
and contact work without confusing driver/controller ownership or dt domains.

Evidence labels used below:

- **S**: authenticated original PE instructions/tables, conditional source paths.
- **E**: original x86 interpreted by Unicorn on synthetic memory with declared
  external hooks. No native host execution, full force model or game process.
- **R**: original running game measurement. Unavailable throughout this task.
- **P**: proposed integration boundary, not an original engine API.

## Reproduction and boundaries

Run from the assigned worktree using standalone Python3.8+:

```powershell
$py = 'C:/ETAP 2102/ThirdParty/Python/Python384/python.exe'
& $py -m pip install --target build/256-astra-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/256-astra-deps'
& $py -B tools/physics256_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/256-evidence
& $py -B tools/physics256_test.py --report build/256-evidence/report.json
& $py -B tools/collision256_trace_test.py
```

Every output directory must be fresh, within the current workspace, outside
retail/protected paths, and have ASCII relative components. Existing Unicode
workspace ancestors are handled through runtime APIs. Dependencies and reports
stay ignored. The executable is 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
No track geometry, images, retail executable or derived asset is exported.

The trace checks the prior ownership assertions as well as its new ranges,
instruction boundaries, table contents and direct edges. Two in-memory negative
source guards reject replacing actor+0x34 with +0x30 and confusing the two
physics targets. These guards never patch a retail file. The report records
tool hashes so stale reports fail the regression test. Full root/directory/
file size/mtime/SHA-256 manifests bracket every probe, including failures.
Windows reparse points are refused. A manifest match alone is not a probe pass.

`source.txt` gives the disassembly and range hashes; `report.json` gives S
assertions/constants and E case inputs, outputs and encountered hooks.
Tests are the record of numerical expectations. An instruction-budget limit,
undeclared instruction, memory error or incomplete return fails the probe.
No crash is classified as a successful negative case. The x87 control word is
explicitly synthetic; original precision/rounding and live values are R gaps.

## Ownership and the actual driver methods

**S.** Actor+0x2C owns its controller. Controller+8 is copied to the driver
alias at actor+0x30 and physics+4 by `0x004260D4..0x004260E7`.
Actor+0x34 owns a distinct physics object. Actor+0x28 is its selector, not the
controller selector at +0x24 or the driver pointer. The dispatch jump table
at `0x004262A8` maps selector1 to constructor `0x00476840`, and selector3 to
`0x004740D0`. Their installed tables/step targets are:

| Selector | Physics vtable / virtual+4 | Actor / embedded state / driver pointers |
| --- | --- | --- |
| 1 | `0x006C8364` / `0x004774A0` | physics+0x5C / +0x58 / +4 |
| 3 | `0x006C8468` / `0x00474A90` | physics+0x34 / +0x30 / +4 |

Both embedded-state pointers refer to actor+0x40. Physics+0x20 is the rigid
component in both implementations. Identical numeric offsets in different
objects are not interchangeable. Ordinary selector choice `0x00425150`
usually distinguishes the human role/config gates from others, with globals
`0x00736208`, `0x0078A344` and `0x007361A8` affecting that choice. It does
not establish immutable human=1/AI=3 ownership for every live frame.

World `0x00420DA9/DB2` calls actor+0x30 virtual+4 with world dt.
The human table `0x006C8AD0` selects `0x004615E0`; the AI table
`0x006C8C18` selects `0x0045E970`. Their complete entry ranges are in S.

- Human `0x004615F2` asks controller virtual+0x40 for input, then calls
  processed-controls `0x00461A60` with the supplied dt at `0x0046165B`.
  Steering has a driver+0x2B8 and actor+0x3B8 threshold gate. Under
  actor+0x28==1, `0x004616EC` reads physics+0x4C and manages transmission
  state, including `0x0046DAC0`. These are control/state interactions, not
  the physics virtual+4 integration call.
- AI `0x0045E970` uses actor through driver+0x1F0 and updates controls/history.
  Its local guarded dt at `0x0045E99C/9A2` is not the scheduler's dt: several
  operations still read the original argument. At `0x0045F538/550`, it loads
  actor+0x34 and invokes **virtual+0x10**. For selector3 that accessor is
  `0x004740C0`, reading physics+0x4C. It is not virtual+4. Later, under
  selector1, `0x0045FAB3/FABB` writes physics+0x78. Neither edge establishes
  force integration inside the AI driver.

**E.** The full human entry is executed for selector1 with transmission
management cases3/4 inactive; controller input and processed controls are
stubbed. The full AI entry is executed for selector3 with airborne synthetic
wheel flags, zero goal/curve outputs and bounded history storage. Its actual
virtual+0x10 accessor executes. These cases reach no physics integration
entry. They do not prove that every out-of-scope controller callback, grounded
AI path or driver helper is free of side effects. **S** establishes the
positive integration chain below; absence of a direct call is not a universal
negative claim about all transitive callbacks.

## Body dispatch, dt and gates

**S.** CarCollisionBody constructor installs `0x006C8798` and stores actor
at body+0x140. Body virtual+0x28 is `0x0046A030`:

1. `0x00468F70` snapshots body orientation, position, velocity and supplied dt
   at body+0x80/+0xC0/+0xD0/+0x70, through body accessors.
2. `0x0046A03E..46` passes that actor and the same dt to `0x004264D0`.
3. Actor wrapper runs `0x0042A530` (collision-state maintenance) and
   `0x00429B10` (conditional parameter update), then loads actor+0x34 at
   `0x004264EB` and calls physics virtual+4 at `0x004264F5` with unchanged dt.
4. The wrapper stores the returned float, runs gear/state/recovery handling,
   and returns it. Body wrapper discards it; for zero `0x00736200` it also
   calls bounds helper `0x00468EC0`. This is not a second integration.

The recovered wrapper has no dt-positive, actor-map-visibility or finished
test before the physics call. Body participation belongs to the enclosing
scheduler. A later recovery/teleport branch is separate from ordinary force
integration and is not numerically executed by this probe.

| Scheduler branch | Proven integration placement and dt | Gates and limits |
| --- | --- | --- |
| `0x00736200==0` | World calls `0x00420310`, which snapshots registered body pointers in list order and calls their virtual+0x28 with the world dt; then `0x00420380` builds/queries/resolves the queue. | body+0x16 must be nonzero. This pass does not additionally require +0x1D. Different world actor-array and registered-body orders must remain distinct. |
| `0x00736200!=0`, paired groups | `0x00420890` prepares swept bounds/groups and searches contacts; `0x00592D50` advances a group interval through `0x00592CB0` before calling contact helper `0x00592FD0` when a next-hit pointer exists. | Pair nodes with +0x0C set are skipped. Each eligible body is visited once per call, using signed body ID +0x18 for the temporary visited map; body+0x16 still gates it. Group iteration/requery can cause more than one physics call within a world call. |
| Non-pair/default group | Other branch of `0x00592D50` scans the registered list and calls virtual+0x28 on bodies whose group pointer +0x0C matches the group. | Matching membership and body+0x16 required. The world scheduler also calls this helper for its default group after the paired-group loop. |

For finite nonnegative interval cases, let `q` be float `0x006EF2C8`,
`candidate=group+0x10 - group+0x0C`, and `remaining=group+8 - group+0x0C`.
`0x00592D50` keeps `candidate` if it is at least `q`; otherwise it chooses
`min(remaining,q)`. It adds the chosen interval to group+0x0C **before**
body advancement. This is not a universal maximum-step clamp, nor proof that
the requested next-hit time is always used. Valid live group-state invariants,
negative/non-finite inputs and total integration per world call need R evidence.
E independently checks several intervals, advance-before-contact, membership,
inactive-body omission, pair marking and deduplication. It does not execute
group formation, time-of-impact search, response, or the full outer scheduler.

## Internal subdivision and publication

**S/E.** Each ordinary physics method computes
`n = trunc(supplied_dt / divisor) + 1`, then uses equal float slices
`supplied_dt/n` when `n>0`, through `0x0046AA40`. The truncation helper
`0x00674898` executes in E; it is not replaced with a Python rounding rule.
Selector1 reads divisor `0x006FFE14`; selector3 reads `0x006FFF10`.
Tests pin the divisors, threshold-neighbor behavior, zero input and multiple
positive inputs. Do not generalize the selector3 subdivision to selector1.

The per-slice global `0x006F0738` receives the slice, and orientation
normalization follows integration. Selector1's loop is
`0x00477D56..0x004781B9`; selector3's is `0x00474BE4..0x00474C4B`.
Zero dt still enters one zero-length slice in these routines; a skipped
body and a zero-length physics call are different events. Negative/NaN dt,
overflow, and a live FPU configuration are outside the E contract.

Selector1 tests `0x00736200` at `0x00477511/518`; nonzero jumps over its
input/parameter setup block to subdivision at `0x00477D56`. Integration and
publication still occur. Thus a port cannot replace this branch with an early
return or assume freshly recomputed setup on every group interval. Producers
and lifetimes of all retained component parameters require further tracing.
Selector3 does not have that same early setup branch. In both implementations,
actor+0x38 and +0x3CC control rigid flags; the tested state branches alter
those flags but do not suppress the integration loop.

After all slices, selector1 calls publication `0x004789D0` at `0x00478257`;
selector3 calls `0x00474730` at `0x00474C53`, each with the unsplit supplied dt.
With `0x007798E0==0`, both export:

| Source | Actor destination | Evidence |
| --- | --- | --- |
| rigid+0x20 position | actor+0x60 | S/E actual stores |
| rigid+0x30 orientation | actor+0x90 through embedded-state+0x50 | S, copy callee stubbed in E |
| rigid+0xA0 linear velocity | actor+0x70 | S/E actual stores |
| rigid+0xC0 angular velocity | actor+0xD0 | S/E actual stores |

Nonzero `0x007798E0` skips this pose/velocity publication slice, not the
whole physics call. Origin adjustment `0x00465390`, heading conversion,
wheel/RPM/effect export and alternate publication branches have further
semantics; E stops at the end of the stated slice. A copied rigid pose is not
automatically a complete rendered-car transform. Full publication fidelity
and the original meaning/writers of this global remain R/source follow-ups.

## Phase contract for #257 and #258

**S.** Within world `0x00420B40`, the driver loop completes first. The actor
pre-pass `0x004267A0` follows for each world car; for positive world dt it
copies actor+0x60/+0x70/+0xD0 to +0x490/+0x4A0/+0x4B0. After prop
maintenance, one of the collision branches above invokes body physics.
After that branch returns, actor post-pass `0x00426930` computes
`(current_velocity - saved_velocity)/world_dt` and the analogous angular
quantity at actor+0x80/+0xE0. For nonpositive world dt these derivative
stores are skipped. E executes these actual slices with explicit synthetic
changes between them; it does not claim a measured contact impulse.

**P.** Callable contract `physics256-v1`, to be implemented only within the
remaining #257/#258 acceptance gates:

| Input | Required output and ordering |
| --- | --- |
| world call identity, supplied world dt, ordered actor set, controller/driver identities and controls | Finish the driver phase before taking per-actor pre-contact snapshots. Preserve raw selector, participation gates and object ownership. |
| ordered registered bodies or a scheduler-selected group, membership/IDs, exact selected interval | Invoke each eligible body through its actor's selected physics; keep interval, internal slice and world dt distinct. Produce updated component state and the appropriate publication before that branch's next contact operation. |
| body pair/contact witness, pre/post states, original selector/gates | #257 applies only evidence-backed shared response. No separate traffic/opponent restitution follows from either physics selection or role. Pair discovery and solve order remain the scheduler contract. |
| completed contact branch plus original pre-pass snapshots | #258 runs actor post-work using world dt, then progress/status/finish producers before publishing the read-only racer snapshot proposed by PR285. A pose exported before an impulse is not a final race snapshot. |

The usable new input/output is an authenticated call-chain report and a
reproducible interval-to-integration/publication capture. It enables #257 to
choose the correct integration/contact seam and #258 to maintain separate
driver, physics, body and final-snapshot phases. It does not authorize a new
solver or establish an original dynamic contact witness. #259 body foundation
and #270 lifecycle remain separately owned.

## Missing original witness and next capture

**R unavailable.** No original launch, attach, patch, save or console action
was performed. The bounded probes do not establish active event0 selectors,
actual actor/body order, cadence, AI quality, forces, impulses or hardware
acceptance. The old selector3-based suggestion of a general retail 120Hz
model rate is insufficient, especially with the distinct selector1 path.

The next authorized original capture must record executable/profile identity,
input sequence, loaded base/thread, world call sequence and supplied dt;
driver entry/return controls; actual controller/driver/physics/body pointers
and tables; selectors `0x00736200/04/08`; body flags, IDs and group membership;
each `0x004264F5` receiver/dt; component setup provenance; every `0x0046AA40`
slice; publication gates/outputs; current-hit witness/impulse and body state;
and final post/status/finish snapshots. Include no-contact, controlled two-car
contacts, pause, restart and selector transitions. Correlate time domains by
call identity, not a guessed 60Hz or 120Hz cadence.

The missing witness matrix remains a blocker for original-parity integration,
not for reviewing this research. Research/ASCII checks approve no inherited
C++/Release/Switch substrate. Later hardware checks retain scene-height540 and
gpu-boost on, with actual clocks measured separately.
