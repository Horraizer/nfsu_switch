# PC event9 comparison and world recorder checkpoint

## Scope and holds

Owner requested an original-PC profile for event9 and comprehensive relevant
observations of calls, steering, lane changes, acceleration and traffic.
SOLO, Git/index/ref/board-write and independent-review HOLD remain in force.
No delegation, fetch, commit, push, merge, board post or review verdict.
The current task changes PC tooling/documentation and one backed-up PC save;
the r87 game/NRO sources and sealed package are unchanged.

## Profile

Profile1 autosave was backed up with every file in the original save directory.
`build/profile-event9/root-pc-event9-install-r1.json` gives the backup path,
before/after hashes, exact changed body bytes and codec evidence. Profile2,
cfg/opt/Profiles.usr, bank and appearance were preserved. The current career
Civic has engine, gearbox and tyres level1; other packages and unique items
are zero. Events0..8 are won, opening event9. Use Hard to match the completed
r87 runs, whose live setup logged difficulty2.

The authenticated original4AB200/4AB120 codec roundtrips the original body
unchanged. The candidate changes only event8 status and current ride unique
flags. Original41DD10 computes its checksum; an independent native deserialize
checks the result before atomic installation and file hash readback. Retail
files remain read-only. Actual profile loading still requires the owner.

## Recorder

Sources are `tools/original_world_recorder.py`, `original_world_agent.js`,
`original_world_report.py`, `original_world_smoke.py`; documentation is
`docs/ORIGINAL_WORLD_RECORDER.md`. Tape refusal/decoding tests are in
`tests/original_world_recorder_tests.py`.

Native integer-only listeners record chronological relevant boundaries into
bounded rings. No shadow emulator or host acknowledgement wait runs in the
game thread. Outer physics/driver/controller state includes controls, path,
action, demand, setup/category, engine/gearbox where actually present, RNG,
and implementation identities. Human steering/pedal/shift callbacks use the
real +26C actor owner, distinct from AI-driver +1F0. Readability checks preserve
LastError. Whole-world polling supplies traffic outside a current call scope.
Records retain incomplete/gap/unsupported flags; no complete-world parity claim.

The inner46AA40 probe produced a raw rigid-state and masked-FPU-status
difference. It is excluded from production instrumentation, not declared an
accepted replay. Early failed probes remain under build/world-recorder-r1.
Two export heads are frozen snapshot-first/call-second so a concurrent native
producer cannot export a snapshot ahead of its call record. The smoke harness
ack cursor was also corrected to avoid skipping a newly arriving message.

## Validation

From the real Unicode workspace, Python312 with -B:

- `tests/original_world_recorder_tests.py`: `Ran 4 tests in 0.008s`, `OK`.
- `tools/original_world_smoke.py --retail RETAIL --out build/world-recorder-r1/smoke-r13 --kind all --host`:
  exit0; physics negative control, actual AI driver and controller/human-input
  scene pass at both tested precision settings. Raw actor/driver/caller/FPU
  comparisons and no lost batches pass. Production host attach, polling,
  native tapes, drain/detach, original entry restoration, continued fixture
  execution, verified ZIP and offline call pairing pass. All eight real human
  input callbacks are observed. See that run's report, host summary and export.
- Installed-copy --help smoke from the real workspace: exit0, source readback
  and ASCII hashes match. `root-install-world-r2.json` records the final copy.
- ASCII/HEAD/index validation is recorded in
  `build/world-recorder-r1/root-ascii-world-r1.json`.

No C++/CMake or Switch game source changed in this task; their gates are not
rerun for this Python/JavaScript utility/profile preparation.

## Durable state and next step

Completed r87 logs/receipt are copied and hashed by
`build/world-recorder-r1/root-task-evidence-r1.json`, which also pins sources
and the unchanged HEAD/index. Do not overwrite sealed r87 or these records.

The separate installed copy is Documents/NFSU-World-Recorder, with desktop
shortcut NFSU World Recorder. Earlier recorders are untouched; the first
installed world sources are preserved in source-before-r2. The initial waiting
capture was stopped through its own signal before the human-input update.

`build/world-recorder-r1/root-world-armed-r2.json` is the current capture
identity/readiness receipt after rearming. It names PID plus creation time,
status/log paths and its exact stop signal. Never act on PID alone. Do not run
the AI or Physics instrumented recorder concurrently.

The owner must launch the original, load profile1 and drive event9 Hard,
preferably all three tournament rounds. Stop this capture via its exact
signal or exit the game. Then preserve/hash the session, check actor/track/
input/physics-selector coverage and gaps, export with original_world_report,
and compare native player/rival/traffic timelines against the completed r87
logs and port behavior. Natural event9 runtime and full world replay parity
are not yet verified. Do not retune rivals to match only a final elapsed time.
