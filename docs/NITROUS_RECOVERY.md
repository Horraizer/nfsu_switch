# Nitrous runtime recovery for issue 314

This is a reproducible recovery checkpoint at base
`ec21749671603a1505a63148feb3806d0a1c5059`. It changes no vehicle input,
dynamics, race controller, HUD readiness, audio or effects. Issue #314 remains
open: bounded executable witnesses are not an integrated event0 race.

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

## Remaining original and integration evidence

The input dispatcher calls the resource start at `0x00431C48` and the release
stop at `0x00431CB1`. The start is suppressed by `0x0041F600` and
`0x0042AB30`, then by availability `0x00431900`. The suppression helpers read
mode bytes and global overrides. They are not speed/RPM/gear gates in these
bounded instructions. This does not exclude restrictions earlier in the
input dispatcher or elsewhere in the engine.

Required before production integration:

- Authenticate the complete event0 input-dispatch entry, input IDs and actor
  reachability, including pre-GO and finish transitions. These isolated helper
  contexts cannot authenticate physical controller binding.
- Trace the live engine wiring and the second conditional torque global to
  establish the ordinary event0 setting, rather than assuming its default.
- Execute original pause, car reset, retry and finish lifecycles with explicit
  initial state. The initializer's preservation of depleted charge makes a
  guessed `reset()` refill observably unsafe. Existing recovery explicitly
  leaves resource reset on introductory retry unknown.
- Establish any replenishment writer or absence through the complete resource
  lifecycle. No helper tested here replenishes charge during ordinary update.
- Connect the supported authoritative state to the actual event0 candidate,
  replay serialization, controls and read-only integration events. Then run
  default/Release retail gates and Switch gates on that implementation.
- Obtain original and Switch evidence for reachable HUD meter, correct sound
  and effect. A decoded tuning table, forced presentation readiness, or a
  synthetic boost schedule is not this acceptance.

No production formula has been installed from these partial contexts. The
smallest missing input is authenticated event0 execution/lifecycle evidence
for the above callers and live globals, or a separately reviewed recovery of
those paths. This branch provides the executable recovery to continue that
work without inventing runtime behavior.