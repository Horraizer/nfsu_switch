# Original PC world recorder

This mode records relevant driver, AI and outer physics call boundaries for a
natural drive. Unlike the decision replay recorder, it does not run a shadow
emulator or wait for the host inside a game callback. Do not run the Physics
or AI instrumented recorder at the same time. Entry signatures refuse an
already-intercepted function rather than chaining unknown instrumentation.

The supported executable is the PC 1.4 USA file pinned by
`tools/original_recorder.py`. The host authenticates its file hash, image base,
PID and creation time. The user starts and drives the original. The recorder
neither launches the game nor supplies gameplay input. Retail files are only
read. Function entry interception is temporary and restored at a clean stop.

## Start and stop

Use the **NFSU World Recorder** desktop shortcut, press Start, then play the
original and press Stop. The recorder can wait for the game. Its GUI remains
separate from the game. A headless capture may already be armed by the current
task; use that capture instead of opening a second recorder.

Sessions are saved under Documents/NFSU-Recordings/world-TIMESTAMP-ID. Stop
after the desired drive, or exit the original. A normal Stop drains all
published native records before removing listeners. If the process exits
first, the final in-flight tail is explicitly unconfirmed. Delivered records
are kept in either case. The directory and a hash/CRC-verified ZIP remain.

For full world traces the disk-size ceiling is configurable with
`--max-gib` (see `--help` for its default and allowed range). The GUI uses the
same default and displays recorded MiB against the ceiling. This is a raw
recording-file limit, not a reservation of system or graphics memory.
Archive creation and verification stream large members in bounded chunks;
the raw binary tape schema is unchanged. Stop after the desired drive and
allow packaging to finish before closing the recorder.

There is no 300-decision sampling limit. Capture is bounded by the time and
byte ceilings in `tools/original_world_recorder.py`; reaching a ceiling ends
the recording. Native ring overflow, writer backpressure, sequence gaps and
read failures are visible. A stopped or incomplete capture is not reported
as a complete replay.

## What is recorded

- Human, rival and traffic actor identities, car types, positions, velocity,
  native speed, RPM, actual gear, requested gear, steering and pedals.
- Before/after outer physics and driver calls, with original frame, 4000 Hz
  clock, dt bits, caller, registers and ordered nested calls.
- Common driver path points, roads and road points; AI demand speed and
  duration; ordinary rival controller, action, route and strategy state.
- Human drag input events, gear requests and the launch-window producer.
  Engine and gearbox state is included when the actual physics1 owner exists.
  Other physics selectors are identified, not populated with a guessed model.
- Ordinary human steering/pedal and transmission input callbacks are recorded
  separately from the drag AI-driver input callback. The native human driver
  owns its actor at +0x26C; the AI driver owns it at +0x1F0. Recorded vtables
  identify which implementation is actually driving each car.
- Performance levels, unique items, handling category, AI context, selected
  physics/controller implementations and relevant RNG state.
- An independent 60 Hz read-only poll of the whole actor list, including
  traffic outside the instrumented actor's scope. Cross-frame samples and
  missed polling intervals are counted.
- Initial and stage-change global/context memory, plus periodic related
  actor/driver blocks. Unsupported or unreadable state is marked by validity
  bits; the decoder does not treat its reserved zero bytes as observed data.

Native callbacks use integer-only C code and bounded memory rings. They do
not call JavaScript or wait for a disk write/host reply. Readability checks
preserve the thread's LastError. Serialization and disk writes run outside
the game thread. Callbacks on a different thread are counted and excluded;
the selected world thread is identified by an outer controller/driver or
physics call, not by the first input event.

The exact enabled boundaries and signatures are stored in each session's
`instrumentation.json`. The inner 46AA40 rigid integrator is deliberately not
intercepted. A synthetic comparison found that its scratch lanes can consume
stack bytes changed by an entry hook, including a raw rigid-state difference
and masked floating-point exception flags. Whole physics calls are observed
instead. That failed probe remains evidence; it is not an accepted component
replay or a bitwise-equivalence claim.

## Files and analysis

- `metadata.json`: provenance, recorder hashes, schema and limitations.
- `instrumentation.json`: actual boundary addresses, signatures and enabled flags.
- `calls.bin`: contiguous little-endian records of 40 unsigned 32-bit words.
- `snapshots.bin`: little-endian state records of 1344 unsigned 32-bit words.
- `samples.jsonl`: whole-world polling, including traffic and cross-frame flags.
- `memory.bin`: original-recorder `<dII>` elapsed/address/size blocks and bytes.
- `events.jsonl`, `summary.json`, `manifest.json`: errors, stop completeness,
  stream counts and hashes.

`tools/original_world_report.py --session SESSION --out FRESH_OUTPUT` checks
call sequence/parent/return relationships and exports native state and input
events to JSONL. It preserves duplicate positions and both boundary epochs.
Use original clock, paths, heading, poses and velocity to measure lane-change
delay, lateral motion, gear latency and acceleration. Compare the same car,
performance packages, unique items, difficulty and track; do not fit only the
final elapsed time. The legacy `speed_mps` key is retained from the poller;
raw velocity, coordinates and speed are all retained for unit verification.

Call cycle spans include listeners and nested calls. Instrumentation changes
timing. This mode is not an FPS benchmark, an all-function/instruction trace,
or a complete deterministic world replay. It does not claim that every force,
collision service, resource access or other thread can be replayed from it.

## Validation

Run `tests/original_world_recorder_tests.py` for binary tape integrity,
missing/duplicate/truncated data refusal and field decoding. Run
`tools/original_world_smoke.py --retail RETAIL --out FRESH_OUTPUT --kind all --host`
for isolated copied x86 fixtures and the production host lifecycle. Fixtures
never start the retail game. The ordinary controller and driver comparisons
check raw actor/driver state, controlled caller registers and architectural
FPU payload with and without observation at both tested precision settings.
The unhooked rigid case is a negative control, not a full-physics proof.

The host lifecycle checks attachment, polling, native batches, binary output,
drain/detach, restored entry bytes, continued fixture execution, ZIP integrity
and offline call pairing. A natural original drive is still needed to confirm
coverage of the selected event, player, rivals and traffic.
