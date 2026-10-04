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

### Original phase and engine producers

`tools/nitrous314_bridge.py` executes original stage selector
`0x0042D470..0x0042D4E1`, including its table at `0x0042D4F0`.
It reads the stage object through `0x007361F0`. This object is distinct
from race status at `0x0073619C`, which the input dispatcher reads.
The receiver is the player/view object registered in `0x007361BC`, not
the human actor's RealController. Original main-loop suffix
`0x00447783..0x004477A6` iterates that registered list and calls the
selector. The bounded loop witness uses matching current states and
therefore omits allocator-backed input re-registration. The complete
main-loop imports and stage producers remain outside this witness.

For stage values4/5/6/8/10, receiver `+0x4AF` selects state3; otherwise
`+0x4AE` selects state2 or4. The original state table registers nitrous
for these three states. Finish `+0x58` selects state8, and the original
index into `0x00700B3C` can override it with state5. The flags and stage
are explicit witness inputs, not assumed production defaults.

The live-engine constructor `0x0046C7C0` derives the nitrous descriptor
from physical engine idle and redline, not maximum RPM: nine points,
origin at physical `+0x00`, endpoint at `+0x04`, scale8 divided by their
difference, and table `0x006FFF34`. Caller `0x00476BD9..0x00476BF1`
passes physical owner `+0x54` plus `0x1F0` and stores the engine at
owner `+0x48`. The executed constructor prefix ends at `0x0046C8E0`
before its maximum-torque helper and remaining object setup.

Original physics suffix `0x00477637..0x00477684` connects owner `+0x58`
resource to owner `+0x48` engine. It invokes resource active twice at the
same world tick. On an inactive-to-active engine transition it stores
the original rounded world seconds at engine `+0x88`; the second active
result commits engine `+0x84`. Held activation preserves that timestamp,
release disables boost, and past exhaustion commits depletion. Neither
this suffix nor the portable adapter refills charge.

`NitrousEngineState` implements that bounded state bridge;
`NitrousCurveDescriptor` implements the constructor's idle/redline
descriptor. Callers still supply the actual mode flags and appropriate
original engine/base torque units. There is no application caller or
full engine-braking/damage/gearbox pipeline wired by this adapter.

```text
python tools/nitrous314_bridge.py --root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --out build/nitrous314-bridge01
```

Counts and expected values live in the executable tests. The portable
comparison also checks the original constructor descriptor and resource
to engine bridge, including signed clock-word and exhaustion boundaries.

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
- Reach the bounded live engine bridge through the complete actor/physics
  caller and downstream units. Trace the second conditional torque global to
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
Game startup and indirect/aggregate writes remain an investigation gap, not
proof of no writer.

`tools/nitrous314_image_init.py` now measures actual initial image words with
Windows `CreateFileMappingW` using `PAGE_READONLY|SEC_IMAGE_NO_EXECUTE` and
`MapViewOfFile` using `FILE_MAP_READ`. The authenticated original code and
raw-backed held-mode word are positive controls. The initial override words
at `0x007361A0`, `0x007361A4` and `0x007361A8` are zero in this OS mapping.
No original entrypoint, import, startup or event0 instruction is executed.
This closes the initial-image measurement gap; it does not establish the
ordinary event0 value or exclude later indirect/aggregate writes. The
portable caller still supplies the actual runtime override.
The non-executing mapping contract is described by
[CreateFileMappingW](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw)
and [MapViewOfFile](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile).

```text
python tools/nitrous314_image_init.py --root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --out build/nitrous314-image-init01
```

The public implementation is independently reviewable. It does not satisfy
working event0 boost, original presentation or console acceptance. No issue
closure or production readiness is claimed.
