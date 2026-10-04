# Prepared two-car contact core

Issue #257 implementation is deliberately callable before actor integration.
`nfsu_car_contact` reads no file or clock and changes no production driving path.
It consumes separate body snapshots and prepared geometry from a caller that
has already established pair admission. It does not construct a hull or infer
admission from witness flags, distance or solver success.

## Implemented boundary

The source-addressed original subset is:

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
- The witnessed hard-hit callback branch with opposing velocity directions,
  identity orientation, four wheels, null optional actor hooks, normal-mode
  globals and race-event bypass. It resets each eligible state timer, changes
  state, applies the callback's two impulses to A before B, consumes the supplied
  world PRNG state and projects the final arms after the callback record gate.
- Final B then A application order at `0x0046C5D2/0x0046C5E4`.

The low-level calculation and application functions are usable independently.
`resolve_prepared_car_contact` provides a transactional bounded step. Caller
admission false leaves both bodies and the record unchanged. Non-closing pairs
also leave them unchanged. Without explicit `ContactCallbackContext`, a hard
state-changing hit returns `needs_state_callback` and does not partially apply
its hit or momentum. The same result names an unsupported callback branch or
previous record gate. A caller must not interpret it as a solved collision.

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
compare the generated pins with the tracked test include. Neither a failed
execution nor an old output counts as agreement. Run the repository's default
and Release retail C++ gates, plus the Switch shader/NRO/object census gates.
Switch compilation/census proves portability of the compiled core; desktop
fixtures do not prove execution on Switch hardware.

## Closed integration gates and remaining acceptance

No authenticated real-car hull/admission/preparation episode or final kinematic
publication is available in PR308 or this work. The original effects tail is
still omitted by the witness and is not assumed inert. The hard callback's
non-opposing branch uses `0x004663A0` eligibility and actor+0x20 data not supplied
by these bounded fixtures; optional config and alternate/race modes remain
outside the callable contract. Existing record-gated repeated contact is also
not implemented by the bounded step. Those paths return a named callback gap
rather than inventing controller/handling state.

Before production integration, capture a correlated original contact and matched
no-contact episode with real body/shape identities, hulls, pair admission/order,
preparation, callback effects and the subsequent refresh/publication phase.
Then implement the necessary unsupported callbacks and real prepared-pair
adapter. Final velocity, yaw and integrated pose are still unavailable: the
core modifies momenta and collision state while keeping raw velocity caches
and positions unchanged. Real-race acceptance, Switch hardware determinism and
owner event0 acceptance remain open on #257.
