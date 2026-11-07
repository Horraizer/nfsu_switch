CHECKPOINT
role: worker
agent: Codex root
assignment: Owner requests manual drag shifting with right stick up/down
phase: awaiting-evidence
branch: codex/draw-distance-original; publication/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: Local sealed fidelity-r86 freeze-r1, console acceptance pending
worktree: build/day20261008-root-combined
saved: local only; this checkpoint and build/fidelity-r87
ownership: Codex root, active

Right stick up requests one upshift, down one downshift. A return through
the center rearms the gesture; holding or threshold jitter cannot repeat it.
R/A remain alternate buttons. R3 hold-to-look-back and L3+R3 diagnostics stay
independent. Apply the gesture inside the existing Switch drag HID/transition
queue so a short flick between display/world polls has the same protection.

This is an owner-requested Switch binding, not a claim about original PC
stick assignment. Preserve original gearbox requests/clutch physics. No new
console upload, Git/index/ref/board write, delegation, original game launch
or independent review is authorized. Keep sealed r86 archives/evidence intact.

## Implementation and frozen validation

- DragPadInput maps positive right Y to the existing up action, negative Y
  to down. Separate activation/release thresholds prevent jitter repeats;
  an observed center rearms a new deflection. Direct opposite-side jumps
  without center release but do not rearm. R/A and stick merge as one action.
- The gesture is consumed chronologically from native HID history, or from
  display snapshots in the existing merged-source fallback. No new replay
  format or gearbox-physics change. Entry/resume/source change blocks a
  held deflection until center. R3 remains independent and tested.
- Extended nfsu_drag_input_tests: up/down, hold, threshold jitter, center,
  direct sign changes, short history/zero-tick flicks, resume/source changes,
  R+stick merging, R3 and fallback. Both button and stick short-tap paths
  reach a physical gear commit in VehicleModel. Focused target passed.
- Source freeze: build/fidelity-r87/root-code-freeze-r1.json. Only production
  changes versus r86 are switch/drag_input.h, switch/race_main.cpp (hint)
  and tests/drag_input_tests.cpp. Documentation describes this port binding.
- Host tool session37282 runs both independent build directories concurrently
  through configure/build/CTest, with separate logs and retail fixtures.
  Machine inventory confirmed headroom for two5-test jobs and the SDK build.
  root-default-release-validation-r87-r1.json is written after both finish.
- SDK tool session10643 builds build-switch-fidelity-r87-r1, tag fidelity-r87.
  Result: root-switch-validation-r87-r1.json. No complete gate result yet.
- Do not edit production source during gates. Finalizer is prepared at
  build/fidelity-r87/root-finalize-r87-r1.py and must not run prematurely.
  No console upload or Git/board publication is authorized.

## Final validation: 2026-10-31

- Host session37282 completed all six required phases, and SDK session10643
  completed all three. All commands returned0. Both host configurations ran
  the configured retail fixtures; Release included retail-full. None skipped.
  The expanded nfsu_drag_input_tests passed in both configurations.
- Exact commands and logs: build/fidelity-r87/
  root-default-release-validation-r87-r1.json and
  root-switch-validation-r87-r1.json. The tested source is freeze-r1.
- Host commands, from the actual worktree with Strawberry tools on PATH:

```text
cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"
cmake --build build -j 5
ctest --test-dir build --output-on-failure -j 5
cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 5
ctest --test-dir build-release --output-on-failure -j 5
```

- Finalization uses root-finalize-r87-r1.py. Only its success record,
  build/fidelity-r87/root-final-r87-r1.json, establishes the final local
  out/nfsu-r87.zip and source archive. It checks source immutability, full
  test registries, ASCII, unchanged Git HEAD/index, NRO tag/diagnostic marker
  and package bytes. Earlier NRO/source archives remain preserved.
- Remaining acceptance: actual Switch stick direction, gesture feel and the
  owner's missed-press symptom. Next hardware check is one combined r87
  drag run using up/center/down, holding, quick flicks, alternate R/A and R3.
  r87 includes r86, so testing an older r86 NRO first is unnecessary.
- No console upload, original game launch, delegation, independent review,
  Git/index/ref/board mutation or publication occurred. These remain on HOLD.

## Owner-authorized console send: 2026-10-31

- After confirming readiness, the owner explicitly requested sending r87.
  This lifts only the NRO transfer HOLD for this artifact. Git/index/ref,
  board, review and delegation restrictions remain unchanged.
- Sender: build/fidelity-r87/root-send-r87-run1.py; target192.168.52.56.
  It verifies the sealed NRO SHA256, tag, original source freeze and validation
  result before starting nxlink with network logging in a hidden process.
- Progress/receipt: root-upload-r87-run1.json. Logs:
  root-nxlink-r87-run1.log and root-nxlink-r87-run1.err.log. Inspect the receipt
  and actual console build tag before claiming delivery. Preserve PID and
  creation_filetime; do not kill/reuse another process based on PID alone.
- The new event9 profile archive is separate. An NRO transfer does not install
  career.0/career.1; do not claim the console save was replaced by this send.
  Hardware behavior remains unaccepted until the owner completes a drive.

- Send completed. Console log confirms fidelity-r87 and entry into event9
  with Civic and level1 engine/gearbox/tyres. This is observed console state,
  not evidence that nxlink installed the profile. Transfer receipt now records
  confirmation separately from unaccepted handling/behavior criteria.
- Capture process PID88428, creation_filetime134379153568255838; live output
  is root-nxlink-r87-run1.log. Keep it running for the owner's drive. The next
  step is read the completed drive's DRAG_INPUT/DRAG_EVENT and race results.
