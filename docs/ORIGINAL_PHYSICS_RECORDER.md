# Original single-car physics recorder

This is a separate instrumented diagnostic mode. The existing read-only
`NFSU Original Recorder` remains unchanged. Start the new desktop shortcut
`NFSU Physics Recorder`, click **Start recording**, drive the original PC game,
then click **Stop and save ZIP**. Provide the resulting `physics-*.zip` from
Documents/NFSU-Recordings. Launching the recorder before the game is supported.
No upload, game input, save modification or retail file modification occurs.

Unlike polling, this mode temporarily instruments the original process using
Frida. It changes timing and must not be used as an FPS benchmark. Only the
authenticated PC1.4 USA executable documented in ORIGINAL_RECORDER.md is
accepted. No administrator access is required when both processes run as the
same Windows user. Multiple speed.exe processes and a reused PID are refused.

## Installation

Use a complete 64-bit Python3.10+ installation with Tkinter. Install
`frida==17.5.1` into that Python, then run from the repository root:

```text
python tools/original_physics_recorder.py --install
```

The user copy contains the Python launcher, its JavaScript agent and the
unchanged polling recorder module. Installation adds a separate shortcut.
Start/Stop needs no terminal or debugger UI. Closing the window requests a
stop, drains complete records, removes instrumentation and packages the files.
The game can continue running after Stop. If it exits during capture, delivered
complete pairs are preserved and the summary marks an incomplete stop.

## Exact first boundary

The hook brackets entry/return of original `46AA40`, restricted to caller
`477DF2`, actor role1/id0, physics selector1, and matching actor/physics links.
The system argument is physics+8; rigid state is physics+20. Each record has
one sequence number, thread, original frame/clock, track, mode, race stage,
car type, raw driver controls, actual solver interval, x87 control word and
SSE control register. Memory evidence includes:

- Full rigid component before and after the integration, including force,
  torque, momentum, cached velocities, orientation and snapshot orientation.
- The referenced local mass/inertia properties, including alternate inertia.
- Runtime damping tables, math dispatch and timestep/gravity globals used by
  the bounded calculation. Missing data is an error, never synthetic zero.

This first mode verifies **rigid integration with supplied forces**. Engine,
gearbox, wheel, suspension and contact force producers are not yet replayed.
The controls provide context; they are not sufficient to reproduce a complete
VehicleModel step. Each recorded step is reinitialized independently. Chaining
the isolated integration outputs is not a complete race replay because the
omitted producers change the next step's forces.

The post state precedes outer `40BD40` orientation normalization. Comparison
must use that same epoch, not the later public actor pose. Another component
changing the body inside this boundary will produce a visible replay failure,
not agreement obtained by substituting the captured answer.

## Files and integrity

Each new session contains metadata.json, steps.jsonl, events.jsonl,
summary.json and manifest.json. The archive is read back and checked against
SHA-256 and byte lengths before publication. Raw files survive packaging errors.
There is a bounded host queue, acknowledged batches, a step limit and a byte
ceiling. A gap, duplicate, malformed record, failed read or queue overflow stops
capture with an explicit reason. Completed pairs before an error remain useful,
but do not make the whole recording complete. No game assets are packaged.

## Autonomous replay and comparison

Extract the archive without modifying it. From the actual repository checkout:

```text
python tools/original_physics_replay.py SESSION --retail RETAIL --out FRESH_OUTPUT --build build --compiler c++
```

Unicorn2.1.4 is required for replay. Capstone5.0.7 is required by the existing
authenticated PE reader. CMake and the same C++ toolchain used by the build
must be on PATH. The replay refuses a changed executable, damaged input,
empty recording or reused output directory. It rebuilds the port vehicle
library before compiling a fresh local probe against that library.

For every pair it executes original instructions on the recorded input,
with no semantic stubs, and compares all rigid bytes except the two isolated
component-list links. It then feeds the mapped input to the real port
`player_rigid_body_integrate` and reports field differences with an explicit
absolute/relative tolerance. Non-default precision, off-diagonal local inertia
and unsupported drive-force controls remain unsupported, not passing cases.
The report identifies the capture, executable, library and probe by hashes.
Neither native replay nor a passing port comparison proves complete vehicle,
traffic, AI or whole-race parity. Switch performance requires Switch evidence.

## Validation and remaining runtime acceptance

Run the focused Python tests and the live generated x86 fixture from the
Unicode workspace path:

```text
python tests/original_physics_recorder_tests.py
python tests/original_recorder_tests.py
python tools/original_physics_smoke.py --retail RETAIL --out FRESH_OUTPUT --build build --compiler c++
```

The smoke constructs an isolated PE32 process from the owner's authenticated
executable. It does not launch the retail game or distribute its instructions.
It checks entry/return pairs, precision capture, original instruction replay,
port comparison, verified ZIP, restored entry bytes and continued execution
after detaching. A generated fixture is distinct from acceptance on a driven
retail race. The first real recording remains the required runtime acceptance.
