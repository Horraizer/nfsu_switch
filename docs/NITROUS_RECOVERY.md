# Nitrous runtime recovery for issue 314

This branch supplies a portable resource, torque and enabled controls contract
from authenticated original consumers. Issue #314 remains open: no production
event0 caller supplies this contract yet. RaceRun continues to use the existing
diagnostic controls and vehicle path. HUD readiness, audio and effects remain
pending their original and hardware evidence.

## Authenticated execution

`tools/nitrous314_recovery.py` requires standalone Python 3.8 or newer and
Unicorn 2.1.4. It authenticates the retail `speed.exe` with SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
It maps original PE sections, executes only the listed instructions and their
bounded arithmetic helpers, enforces an instruction limit and verifies the
return or endpoint. No game process, imports or OS calls are executed.
Synthetic object contexts are stated in the output. These are observations of
original instructions under explicit inputs, not recordings of original play.

From the actual checkout directory, with dependencies available to that
standalone interpreter, run the following against a fresh output directory:

```text
python tools/nitrous314_recovery.py --root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --out build/nitrous314-run01
```

The output directory must not exist. Both input manifests include every retail
file's hash, size and timestamp, and every directory. The tool checks their
byte equality even after a failing witness. `witnesses.json` records each
explicit input, operation, output and reached instruction. Expected values
live in this executable test; this document does not copy its numeric pins.

The existing `tools/event0_trace.py` can reproduce readable disassembly with
`--range 464DC7:464E6A --range 465430:4655AD --range 46D057:46D0C4`
and the same retail root, using another fresh output directory. Exploratory
range disassembly is a search aid; executed witnesses establish the bounded
observations below.

## What the executed consumers establish

The initializer at `0x00464DC7` reads a preexisting resource capacity. A valid
nonzero capacity bypasses initialization, including when remaining charge is
zero. The fresh path writes a capacity and remaining charge converted from the
installed tuning float at sub-record `+0x2A0`. It derives the step from that
capacity and the installed count at `+0x2A4` when the count is positive.
The conversion uses original constants `0x006CC9FC` and `0x006CCA38`:
raw resource and world-clock fields are in 1/4000-second ticks.

The resource's four words at object `+0x3B0..+0x3BF` therefore have executable
roles: capacity, step, remaining charge, and activation timestamp. The timestamp
uses zero and `0x7FFFFFFF` as inactive sentinels. A zero world clock is a real
edge case for this representation; the witness reports it explicitly.

`0x00465490` permits activation only with a positive, valid remaining field
and no active timestamp. `0x004654F0` refuses a second activation. With the
retail-initial value of global `0x007000E8`, activation records the world
clock and copies remaining charge into the step without immediately deducting
it. The alternative global value instead immediately subtracts the step; that
alternative is tested but is not claimed to be the event0 configuration.

`0x00465430` reports active through the exact remaining-duration boundary.
After that boundary it calls `0x00465530`, which deducts elapsed time, clamps
negative remaining charge to zero, and clears step and timestamp.
`0x00465530` also performs that deduction when called directly on release.
A stopped world clock yields no elapsed consumption. This supports the
paused-clock contract described in `EVENT0_SETUP_HUD.md`; it does not prove
that every original pause/finish caller reaches the same operations.

The live engine consumer at `0x0046D059` evaluates the original nitrous curve
at `0x006FFF34`, multiplies it by installed tuning sub-record `+0x2AC`, and
adds the result to the base torque. It is an additive term, not a guessed
multiplier on throttle or the main torque curve. `0x0046D057` bypasses the
term when the preceding test of engine `+0x84` is false. The executable
witness includes this inactive branch as a no-nitrous torque regression.

The subsequent live instructions conditionally modify the combined torque
using `0x0078A344` or `0x007361A8`, activation time engine `+0x88`, and original
constants `0x0070002C` and `0x006B7238`. The tool executes both flag outcomes
and clock conditions. Ordinary event0 clears the drag flag, as recovered in
`EVENT0_SETUP_HUD.md`; the second global's complete initialization/writers and
meaning are not authenticated here. The legacy consumer at `0x004755E0`
uses engine `+0x194`, whereas the live engine uses `+0x84`. Applying a legacy
formula to the live object without tracing that bridge is not demonstrated.

## Dispatcher and lifecycle continuation

The input dispatcher calls the resource start at `0x00431C48` and the release
stop at `0x00431CB1`. The start is suppressed by `0x0041F600` and
`0x0042AB30`, then by availability `0x00431900`. The suppression helpers read
mode bytes and global overrides. There is no speed/RPM/gear predicate in
this executed nitrous branch. This does not exclude restrictions in callers
or elsewhere in the engine.

`tools/nitrous314_lifecycle.py` executes dispatcher entry `0x00431960` for
the IDs selected by tables `0x00431CC0` and `0x00431CF8`. It covers press,
release, duplicate press, empty resource, missing car and each suppression
predicate. Its explicit mode context takes the original no-effect branch
and disables the sound flag; no effect or sound call is stubbed.
The tool also executes registered callback wrapper `0x0042ABB0` through
the dispatcher. Authenticated state-transition tables at `0x0042D97C`
select registration block `0x0042D74C` for controller `+0x278` states2/3/4;
its instructions register both nitrous IDs with this wrapper and the
controller context through `0x00574020`. The full callback allocation/list
machinery and event0 state producers are not executed by this witness.

The tool executes world-clock update `0x00585E00`: zero world delta does
not advance `0x0073AD34`. It executes constructor `0x00464860` through its
resource-clearing prefix, then separately executes the resource initializer.
Wheel-transform and intervening reset setup are omitted and identified in
the report. Existing initialized charge survives the initializer; a freshly
cleared resource initializes full.

Authenticated retry dispatch gives `RC_RESTART_RACE` message8, route
`0x00422BD4`, teardown `0x00422130` and reload `0x00421F00`, whose call at
`0x00421FC7` reaches actor instantiation `0x00422A70`. The constructor calls
reset `0x00464C80` after clearing the resource. This is an authenticated
static call chain and executed fresh-resource operation, not execution of
the complete world teardown/reload. Car reset's existing-object initializer
preserves charge.

Finish consumer `0x0042C1F0` is executed in an explicit nonselected-place
context: first finish latches its world frame and stops the resource,
committing elapsed consumption; later calls return without consuming again.
Reaching it through the complete event0 finish producer remains pending.
The tool executes the caller suffix `0x004352EA..0x004352FC`, including
the actual call at `0x004352F7`; earlier lap/place eligibility is omitted.

```text
python tools/nitrous314_lifecycle.py --root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --out build/nitrous314-lifecycle01
```

## Portable implementation and comparison

`include/nfsu/nitrous.h` and `src/nitrous.cpp` expose resource operations,
explicit dispatcher context, a read-only snapshot, transition events and
the live additive torque consumer. Callers supply world ticks, installed
tuning, the live curve descriptor, activation time and actual mode flags.
The implementation does not select an event0 value for an untraced global.
`nfsu_nitrous_tests` pins the recovered numeric curve's byte identity.
The combined-torque modifier uses authenticated live-consumer constants
under the original mode/time predicates. Native interpolation just below
its origin is retained and covered by executable comparisons.

`NitrousControls` in `include/nfsu/nitrous_controls.h` owns the input adapter.
Owner comment8529 selects L. Enabled configuration removes every L driving
binding, so pressing it cannot also shift down. Disabled configuration keeps
the supplied diagnostic table, including default L shift-down. Held, pressed
and released requests follow the existing slot/connect latch conventions;
these platform rules are not an original Switch mapping claim. No application
selects the enabled adapter yet. Changing modes resets the input latch; the
caller must also perform its appropriate resource lifecycle operation.

`tools/nitrous314_compare.py` launches a fresh portable probe and compares
output against original instruction execution, including a seeded sequential
resource exercise and torque across curve/time/mode boundaries. It rejects
crashes, stderr, empty/incomplete output and stale report directories.
Pointer-presence and unmapped-ID routing remain lifecycle-tool witnesses:
the portable resource API has no actor pointer or original event ID.

```text
python tools/nitrous314_compare.py --root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --probe build/nfsu_nitrous_tests.exe --out build/nitrous314-compare01
```

The comparison brackets the retail manifest and authenticates speed.exe.
Counts and expected states live in executable tests. Run the comparison on
both default and Release binaries beside the complete repository gates.

## Remaining original and integration evidence

Required before production integration:

- Authenticate event0 callers of the now-executed dispatcher and actor
  reachability, including pre-GO and finish transitions.
- Trace the live engine wiring and the second conditional torque global to
  establish the ordinary event0 setting, rather than assuming its default.
- Execute complete retry teardown/reconstruction and the finish producer,
  beyond the bounded operations and static call chain above.
- Establish any replenishment writer or absence through the complete resource
  lifecycle. No helper tested here replenishes charge during ordinary update.
- Connect the public state/input contract to an actual event0 candidate,
  its live engine descriptor and replay serialization. Controller issue293
  has a research seam; it has not supplied a production caller.
- Obtain original and Switch evidence for reachable HUD meter, correct sound
  and effect. A decoded tuning table, forced presentation readiness, or a
  synthetic boost schedule is not this acceptance.

Direct absolute-write and raw pointer candidate searches found no authenticated
writer for `0x007361A8`. This BSS address has no raw-backed PE bytes; absence
from raw data and synthetic harness zeros do not establish a production value.
Indirect/aggregate initialization remains an investigation gap, not proof of
no writer.

The public implementation is independently reviewable. It does not satisfy
working event0 boost, original presentation or console acceptance. No issue
closure or production readiness is claimed.
