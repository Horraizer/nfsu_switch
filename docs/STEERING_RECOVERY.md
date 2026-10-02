# Steering recovery, issue #251

This is an opt-in experiment on the reviewed #250 base, not promotion of a
new default or evidence that the entire car matches retail. The baseline
controls, tyres, inertia, gearbox and replay pins remain the reference.

## Input identity and trace

The local, read-only retail executable is `speed.exe`, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
The steering multiplier is CarTypeInfo record +0x380 (setup +0x270): the
240SX record in `Global/GLOBALB.BUN`, index 18, offset 1736752, length 0xC90,
SHA-256 `212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B`.
The whole bundle SHA-256 is
`39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`.
Numeric tables follow the owner's policy in PR #169 comment 3860. They are
rebuilt and checked by the native comparison; no retail binary is tracked.

Verified call path:

| Address | Behavior |
| --- | --- |
| 0x00461692..0x004616A0 | Passes raw steer to the object at controls +0x1F0 |
| 0x00460930 | Updates input history, runs target/slew, saves last raw input |
| 0x00460970 | Previous word to signed degrees; limit times raw times setup +0x270; positive cap; slew unless bypass; sub-degree snap; truncation to word |
| 0x00460A80 | Speed lock, throttle/brake terms, input-history gain, shift reduction, heading/velocity assist and retained-limit branch |
| 0x00460DE0 | Raw delta/dt curve, rate history, speed response and input-history gain |
| 0x00460E70 | Constructs 0.55 s input history and 0.15 s rate history at 60 samples/s, capacities 33 and 9; initial limit 45 degrees |
| 0x00584360, 0x00584180 | History capacity and zero-initialized allocation layout |
| 0x00584500, 0x005844B0 | Ring sum/count/average, timestamp expiry, zero-time sentinel and clear retaining the prior average |
| 0x00583EA0 | Uniform curve: unrounded truncation index, binary32 fraction, negative-fraction quirk |
| 0x00583F20 | Pair-table shift interpolation |
| 0x00585D70 | Advances the cumulative time at 0x0073AD3C; it is not dt |

Normal lock is the base speed curve times
`1 + 0.8 * response(speed) * (1 - 0.5 * (digitalThrottle + 1 -
0.5 * (analogThrottle + brake)))`, then the input-history gain. All constants
and table samples preserve their stored binary32 values. The rate is
`110 * response(speed) * rateHistory * rateGain(abs(inputHistory))`.
The target uses raw input; history changes gain/rate rather than replacing it.

During the first second after a valid engine +0x14C shift timestamp, lock is
multiplied by `shiftCurve(t) + (1-shiftCurve(t)) * (1-speedRatio)` with
`speedRatio = min(1, forwardSpeed/(0.7*predictedMaximumSpeed))`.
Zero and 0x7FFFFFFF timestamps skip this branch. The predicted maximum comes
from 0x00580240, which consumes a cached acceleration curve; recovering that
engine predictor is separate from testing this steering consumer.

Assist uses heading minus velocity direction only above forward speed 1.
It enters above slip 7 degrees and total speed 13.410818; an active flag clears
below slip 5 or total speed 17.88109. These seemingly unusual thresholds are
the actual comparisons, not a conventional hysteresis invented from names.
Opposite-signed raw input/slip selects the assist curve when the flag is set
or slip exceeds lock. Same-signed input retains at least the previous limit
when absolute input history is >= 0.5. Its decay constant at 0x006B738C is
exactly zero. The native comparison tests all these branches and equality
neighbors. The assist device predicate chooses descriptor 0x00705F20 or
0x00705F34; it is not inferred from a button-versus-stick classification.

Byte 0x00736514 selects alternate speed lock and bypasses history gain,
shift suppression, rate slew and the sub-degree snap. Its locally verified
writer at 0x0040664A depends on device lookup arrays and byte 0x00736515;
the hardware meaning is unresolved. The predicate at 0x004607A0 returns false
when object +0x54 or global 0x007361A8 is set, otherwise true when global
0x00735FA0, object +0x55 or global 0x007361A4 is set. Those flags' hardware
meaning is unresolved too. Whole-text linear disassembly is not evidence of
a writer: compiler padding can decode as spurious references.

## Reproduction

Use standalone Python 3.8+ with Unicorn 2 installed and the desktop toolchain
on PATH, from the real checkout path. Build `nfsu_steering_probe`, then:

```text
python tools/steering_compare.py --root <ASCII-retail-root> --probe build-release/nfsu_steering_probe.exe --out out/251-native-new
```

The output directory must be new. The script hashes the executable, maps its
PE sections, executes only verified functions with an instruction ceiling,
initializes histories through the traced layout, and runs the real math
helpers. It does not stub arithmetic, load imports or launch retail. A supplied
cached maximum-speed context isolates steering from engine prediction.
It rebuilds the numeric tables, sends fresh contexts to the compiled port,
requires every output row, checks intermediate values and angle words, and
brackets the entire operation with full retail tree manifests. Both successes
and failures retain evidence in the ignored scratch directory.

The comparison covers signed speed, positive/negative inputs, turn-in,
sustained steer, release/reversal, shift progression, throttle/brake channels,
both predicate branches, bypass mode, assist thresholds and history expiry.
Retail uses x87 extended arithmetic; the portable code uses binary64 with
explicit binary32 stores. Tolerances are in the script and results belong in
tests or source-pinned board evidence, not as current numbers in this document.

## Remaining work

The isolated steering consumer is implemented and compared. Race integration,
reset/replay tests, full default/Release/Switch gates and owner driving are
still required. No unmeasured hardware mapping or predictor has been promoted.
