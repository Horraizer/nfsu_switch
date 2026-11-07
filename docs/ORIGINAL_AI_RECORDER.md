# Original rival AI recorder

Start the desktop shortcut **NFSU AI Recorder**, click **Start recording**,
play the original PC game, then click **Stop and save ZIP**. Starting before
the game is supported. Results are `ai-*.zip` under Documents/NFSU-Recordings.
The polling and single-car physics recorders remain separate modes. Run only
one instrumented recorder against a game process at a time.

This diagnostic mode temporarily instruments the authenticated PC1.4 USA
executable (identity in ORIGINAL_RECORDER.md). It does not write retail files,
change a save, send inputs or upload a capture. It briefly pauses the game
thread to discover memory dependencies. It is unsuitable for FPS measurements.

## Captured boundaries

- `462430`: the linked controller update of role2/id100..102 rivals. This
  contains the original action scheduler, strategy, route and catchup callers.
- `45E970`: the linked driver update, including retained steering and speed
  history and the final steering, gear, throttle and brake publications.

Production capture requires racing stage4 and a matching actor/object link.
Controller calls additionally require actor controller kind3. Unsupported
controller families are not silently treated as ordinary rivals.

The assembly entry and return shims save general registers, EFLAGS, full x87
and SSE state, and the original SEH head before entering diagnostic callbacks.
The callbacks have their own SEH chain; captured TEB bytes substitute only the
four saved native SEH-head bytes, without writing the live TEB. Real thread
environment addressing is retained during replay.

While the original thread waits at entry, an isolated x86 emulator runs the
authenticated original instructions. Every demanded non-code memory page is
read from that paused boundary and saved. This includes pointed-to controller,
action, strategy, path, neighbor, route, driver-history and shared RNG data
actually consumed by that call, rather than a guessed fixed object prefix.
Code is read locally from the authenticated executable, not packaged in ZIPs.
No AI return or output from the real call is fed into this shadow execution.

The game then executes its real update. Every non-stack memory byte written
by the shadow is read at return and compared to the native result. Dead local
stack bytes are excluded because the return shim reuses that space. Caller
output memory above entry ESP is retained. Callee-saved registers and returned
ESP are also checked. Nested scheduler, path, catchup and RNG producer entry
order is included. CPU-undefined flags and unused volatile registers are not
claimed as output parity.

## Coverage and refusal

Sampling is per rival and per boundary, at most once every two seconds of
original clock time, with an overall record limit and byte ceiling. Invocation,
frame and clock fields expose gaps. Each saved call is replayed independently;
the records are not a complete chronological simulation of an entire race.
Other threads are not suspended. A shared-memory change can invalidate the
native comparison; it cannot turn a mismatch into a successful replay.
A controller entry can take the original retained-state branch rather than
make a full decision. Count captured calls separately from full strategy
updates; sampling a controller does not imply a new action was selected.

A host timeout releases the original thread through a native watchdog. Missing
pages, external execution, CPU clock instructions, instruction/time ceilings,
failed reads, invalid outputs and native mismatches remain explicit failures
or unsupported records. They are never zero-filled or replaced with captured
answers. An early no-op with no semantic memory output is not an acceptance
case. Complete packaging and complete native reproducibility are separate
summary fields. No claim of full port AI parity follows from packaging alone.

## Installation and replay

Use complete Python3.10+ with Tkinter, `frida==17.5.1` and `unicorn==2.1.4`:

```text
python tools/original_ai_recorder.py --install
python tools/original_ai_replay.py SESSION --retail RETAIL --out FRESH_OUTPUT
```

Replay verifies all manifest entries, capture identity and record ordering.
It executes original instructions using only saved input pages and the owner's
executable. Uncaptured dependencies fail. The output reports reproducibility
per decision. The recorder never changes the port runtime. Use the separate
offline component consumer below to compare its captured inputs with the port.

## Port comparison

```text
python tools/original_ai_port_compare.py SESSION --retail RETAIL --out FRESH_OUTPUT --build build --compiler c++
```

Run from the actual source checkout with its configured host build and compiler
on PATH. The build must belong to that checkout. The consumer rebuilds the
current AI library, compiles a fresh standalone probe and records source,
library, probe and capture identities. It never starts or instruments a game.
Output must be a new directory outside the retail installation.

An optional read-only observer watches function entry/return during unchanged
original instruction execution. It extracts cadence44B56D, path publication459980,
target45FB30, heading errors45E370, pedals45FC90, forward path continuation45C370
and path fitness459B30. An isolated
whole ordinary driver45E970 comparison also restores the four relevant retained
histories from entry memory. Ring cursors, cached averages and reset sample
counts are preserved; no fictional warm-up samples are replayed. The actual
port driver computes its controls and resulting histories from those inputs.
The history snapshot API adds no work to a normal gameplay tick.

Only a call whose full original memory output, callee registers and producer
order reproduce is admitted. A raw mismatch stays in the report and is excluded
from component acceptance, even when its differences appear to be padding.
Missing observer-only memory is reported as unsupported; it is never mapped,
zero-filled or borrowed from another frame. Some native branches do not read
every field required by the whole port API, so a native-reproducible call can
remain unsupported for whole-driver comparison while its consumed components
are still compared. Active override/hold/reset and finished-driver branches
outside that API are refused explicitly.

Intermediates are from offline original replay, gated by the actual native
entry/return capture. They are not separately captured native intermediates.
The target/path/pedal checks use their actual component boundaries: road
selection, offset-policy production and regulator-history production are not
implicitly covered by a component that consumes their outputs. Whole driver
comparison covers its history updates as well. No position integration, whole
controller decision or chained race equivalence follows from these checks.
Sampling gaps and unrecorded world interactions remain visible limitations.

`report.json` separates native failures, unsupported mappings and actual port
differences. Exit0 requires all three sets to be empty. Exit1 can therefore
mean that compared port outputs match but one native record is inadmissible;
read the report rather than treating either code as whole-game acceptance.
The original capture, its summary and the raw replay report are never rewritten.

The optional `--speed-publication` attempts the44C550 speed consumer with a
complete original route table. This can remain unsupported when the capture
contains only the route cells actually read by the original call. It does not
invent the rest of a route or borrow pages from another sample. The normal
component report does not imply that this optional stage was compared.

The recorder uses the existing physics-mode host lifecycle, ZIP integrity,
authentication and process-creation-time checks. Installation adds the AI
shortcut; it does not replace the read-only polling shortcut.

## Validation

```text
python tests/original_ai_recorder_tests.py
python tests/original_physics_recorder_tests.py
python tests/original_recorder_tests.py
python tools/original_ai_smoke.py --retail RETAIL --out FRESH_OUTPUT --watchdog
python tools/original_ai_smoke.py --retail RETAIL --out FRESH_OUTPUT --scene
```

The live fixture is a generated PE32 containing authenticated original
instructions and explicitly constructed synthetic state. It is not the retail
game, contains no driven-race acceptance and must not be distributed. The
scene variant exercises a constructed original scheduler/path/catchup scene.
The real owner recording comparison is tracked in
[the capture checkpoint](checkpoints/root-ai-capture-r63-20261028.md). A live
fixture pass alone still cannot establish driven-race or complete port parity.
