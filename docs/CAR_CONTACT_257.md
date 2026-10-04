# Prepared two-car contact core

Issue #257 implementation is deliberately callable before actor integration.
`nfsu_car_contact` reads no file or clock and changes no production driving path.
It consumes separate body snapshots and prepared geometry from a caller that
has already established pair admission. It does not construct a hull or infer
admission from witness flags, distance or solver success.

## Implemented boundary

The source-addressed original subset is:

- Car/car point midpoint and normal preparation at `0x0046B8C0`, including
  original coincident-witness fallback to the two body origins. Input shape
  witness points are supplied by the caller; this does not perform hull query
  or pair admission.
- Relative contact velocity, closing rejection, effective inverse masses,
  state-scaled inverse inertia, normal denominator, restitution and minimum
  impulse at `0x0046BAE0` through the normal-mode kind-0 response branch.
- Tangential friction and the A-directed current-hit vector through
  `0x0046C3DB`. Both bodies use the original kind-0 vtable constants; there
  is no opponent or traffic coefficient.
- Record accumulation and metadata at `0x00465830..0x004658B0`.
  The current vector, accumulated vector and sum of magnitudes stay distinct.
- Car impulse mass scaling and angular-state scaling at `0x0046A260`, then
  rigid impulse clamps and momentum stores at `0x00584F60`.
- The witnessed opposing and non-opposing car/car hard-hit callback branches,
  identity orientation, four wheels, null optional actor hooks, normal-mode
  globals and race-event bypass. It resets each eligible state timer, changes
  state, applies branch-specific callback impulses to A before B, consumes the supplied
  world PRNG state in the opposing branch and projects final arms after the
  callback record gate. The non-opposing branch uses authenticated actor
  physical-data bounds and does not consume PRNG state.
- Final B then A application order at `0x0046C5D2/0x0046C5E4`.

The low-level calculation and application functions are usable independently.
`resolve_prepared_car_contact` provides a transactional bounded step. Caller
admission false leaves both bodies and the record unchanged. Non-closing pairs
also leave them unchanged. Without explicit `ContactCallbackContext`, a hard
state-changing hit returns `needs_state_callback` and does not partially apply
its hit or momentum. The same result names an unsupported callback branch or
unsupported previous record gate. With explicit witness context, both actors
already in state1 may reuse a gated record: original final-arm projection runs
without callback impulses or PRNG consumption. A caller must not interpret a
named gap as a solved collision.

`ContactCallbackContext` certifies the synthetic witness mode described above.
It must never be supplied for a production actor merely to bypass the gate.
Soft response accepts each body's supplied world inverse inertia and angular
cache independently; the witnessed hard callback has the narrower pose/mode
contract. No part of this core asserts the actual game's global cadence.

## Numerical and state evidence

Reference: PR308 head `988bc892f643c463ba02a8c83e33b48918f889c3`,
`docs/CONTACT_WITNESS_256.md` and its six pinned Python prerequisites.
The executable's size and SHA-256 are checked against that reference before
execution. `tools/contact257_trace.py` verifies the extracted tool bytes against
Git's pinned blobs, executes authenticated original x86 instructions, brackets
all attempts with complete retail manifests and refuses existing output paths.
Its outputs remain local, with no executable or retail hull exported. Execution
retains the reference synthetic scalar math selector and x87 control word
`0x007F`; neither is evidence of the live game's selected math backend.

`nfsu_car_contact_tests` owns the numerical expectations in
`tests/car_contact_pins.inc`; documents do not duplicate those pins. Tests
compare current-hit vectors, tangent speed, every observed momentum application,
record accumulation and both bodies' complete boundary snapshots bit for bit.
Cases include head-on, offset, glancing, separating, stationary, caller-rejected
no-contact, forced distant solver, hard hit, already-state1, seeded accumulator,
light mass, slow closing, coincident witnesses and unprepared overlap. Additional
original executions vary the second body's mass, each body's inverse inertia
and angular cache independently, exercise the high linear impulse clamp and
execute the alternate PRNG arm branch. Each application and bounded pair is
repeated to check determinism.

`tools/contact257_retail.py` extends the authenticated original execution with
stock 240SX handling initialization and its retail `240SX_CV` collision hull.
It checks the `GLOBALB.BUN` and geometry input identities documented in
`VEHICLE_TRACK.md`, executes RideInfo construction/reset/rebuild, loads the
hull and follows the car-body constructor's per-car clone/normalization path.
Original pair query and geometry preparation then provide solver inputs for
retail-hull hard/soft/state1/separating and distant-query fixtures. Pose,
orientation, body mass/inertia and initial velocity remain explicit synthetic
memory inputs; this does not capture admission or cadence of live actors.

The actor constructor at `0x004255AB..0x004255C8` stores RideInfo at actor+10
and its physical-data subobject at actor+20 (RideInfo+60). The non-opposing
callback at `0x0046617E..0x004661EB` uses that subobject's lateral bounds;
`0x004663A0` accepts both car-kind body bytes. Original hull support also calls
an imported performance counter. The trace supplies an explicit zero external
QueryPerformanceCounter result and unit divisor, distinct from the game
timestamp. Original support code combines the counter quotient with a PRNG
remainder to form vertex cache stamps. This fixture proves no live timer or
general clock-independent query. Heap allocation is an explicit external
service. Original code supplies hull coordinates, handling and response.

The distant forced case still produces a response. This is intentional original
behavior at direct solver entry and proves why the production admission gate
cannot be replaced with this calculator. The no-contact case is rejected by
explicit caller admission, as in the original synthetic harness. Existing
one-car replay tests run unchanged in the full retail gates.

## Reproduction

From the actual workspace, using standalone Python and pinned Capstone/Unicorn,
extract the six files named by `TOOLS` in `tools/contact257_trace.py` from the
reference Git SHA to an ASCII directory such as `build/contact257-reference`.
The trace refuses any changed prerequisite. Install dependencies into an ASCII
local directory with `python -m pip install --target build/contact257-deps
capstone==5.0.7 unicorn==2.1.4`, then set `PYTHONPATH` with `Join-Path
(Get-Location).ProviderPath 'build/contact257-deps'` in PowerShell.

```powershell
python -B tools/contact257_trace.py --reference-tools build/contact257-reference --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/contact257-r1
python -B tools/contact257_trace.py --reference-tools build/contact257-reference --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/contact257-r2
python -B tools/contact257_pins.py --report build/contact257-r1/report.json --output build/contact257-pins-r1.inc
python -B tools/contact257_pins.py --report build/contact257-r2/report.json --output build/contact257-pins-r2.inc
```

Compare both fresh reports/source files and both generated pin files, then
compare the generated pins with the `git show HEAD:tests/car_contact_pins.inc`
blob. This avoids treating checkout CRLF conversion as a numerical mismatch. Neither a failed
execution nor an old output counts as agreement. Run the repository's default
and Release retail C++ gates, plus the Switch shader/NRO/object census gates.
Switch compilation/census proves portability of the compiled core; desktop
fixtures do not prove execution on Switch hardware.

## Closed integration gates and remaining acceptance

No authenticated live-car admission/cadence or final kinematic publication
episode is available in PR308 or this work. Retail hull preparation is now
executed under explicit synthetic pair poses. The original effects tail is
still omitted by the witness and is not assumed inert. The hard callback's
non-opposing branch requires caller-authenticated physical bounds; optional
config and alternate/race modes remain
outside the callable contract. A pre-existing gate with both actors already
state1 is supported under the witness contract; broader repeated-contact record
creation/reset/lifetime remains unimplemented. Unsupported paths return a named callback gap
rather than inventing controller/handling state.

Before production integration, capture a correlated original contact and matched
no-contact episode with real body/shape identities, hulls, pair admission/order,
preparation, callback effects and the subsequent refresh/publication phase.
Then implement the necessary unsupported callbacks and real prepared-pair
adapter. Final velocity, yaw and integrated pose are still unavailable: the
core modifies momenta and collision state while keeping raw velocity caches
and positions unchanged. Real-race acceptance, Switch hardware determinism and
owner event0 acceptance remain open on #257.
