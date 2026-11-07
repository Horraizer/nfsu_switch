CHECKPOINT
role: worker
agent: Codex root
assignment: Owner clarified that the human player's R press is sometimes lost in drag
phase: awaiting-evidence
branch: codex/draw-distance-original; publication/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: Local sealed fidelity-r85 freeze-r3, not console accepted
worktree: build/day20261008-root-combined
saved: local only; this checkpoint and build/fidelity-r86
ownership: Codex root, active

No delegation, original game launch/recording, Git/index/ref/board mutation
or console upload is authorized. Retail inputs remain read-only. Preserve
the sealed r85 source archive and package; do not overwrite its evidence.

Original461E70 distinguishes controller mode1 (increment requested1D4) from
mode3 (actual actor3D8 plus1). This controller mode is independent of clutch
phase. The port's VehicleGearbox::manual currently always accumulates the
requested gear. Need verify the authentic drag setup caller before changing
behavior; two actual button edges in ordinary mode1 are not alone a defect.

DriveControls consumes one pending shift edge; RaceSession clears the shift
on later physics slices. These source checks do not establish the complete
live pad path or exclude button bounce. Native setup/request traces and an
end-to-end regression are the next step. Required gates follow any fix.

## Bounded investigation result

- Static drag career/quick-race setup4B5DF0 forces78A387=1 for race kind1;
  constructor4602C0 passes the player setting to461520. Mode3 is a separate
  selectable behavior, not a justified replacement for the native default.
  4BECA0 toggles1/3 under the drag/network setting; this is not proof that
  every drag starts in3. Do not silently change the default to prevent skips.
- root-native-player-shift-r1.py executes actual461520,461E70,46E5B0,
  46DB60 and46DFA0 with no hooks. A single held press requests only the next
  gear; two edges before clutch disengagement queue another gear, and the
  physical commit skips the intermediate gear in original code too.
  Its result is build/fidelity-r86/root-native-player-shift-r1.json.
- root-player-shift-r1.py builds a scratch probe against the sealed r85
  libraries, then runs the actual RaceRun, pad controls, human road planner,
  driver and physics on all three tournament rounds. Every single-held case
  commits one gear; every two-edge case can skip. Its moving human initial
  state is supplied, not an original natural drive. The first two frames
  release buttons to respect the authentic connection latch.
- Native and port probes use different declared car/engine inputs. Their
  frame numbers are not a timing-equivalence comparison. Only the queued
  request/commit mechanism is established by these observations.
- root-observed-shift-r1.json inspects the preserved r84 console trace:
  no multi-gear request or physical commit was logged there. This sparse
  historical trace does not identify the owner's current symptom or prove
  the absence of a real single-press input fault.
- An asynchronous clarification asks whether the owner means a skipped
  intermediate gear, a missed press or the displayed number. No response
  has arrived yet. Do not blame repeated owner input or claim their report
  reproduced: only the explicit two-edge scenario has been reproduced.
- No production code changed and no replacement NRO was built or sent.
  The next step depends on the clarification; if a single press skips a
  gear, capture/inspect actual processed edges and request transitions.
- Separate inspection note: original461F40 mode1 stops down requests at
  neutral1; the compatibility manual path can reach reverse0. This does
  not explain an upshift skip and has not been changed in this task.

## Owner clarification and input correction

The owner clarified that presses sometimes are not accepted, rather than a
physical gear skipping after one press. This supersedes the earlier ambiguity.

- race_main polled padUpdate per display but only passed the newest button
  snapshot into RaceRun when a world tick ran. A whole tap on zero-tick
  display polls, or a release between two world samples, could disappear.
- libnx padUpdate itself reads one HID state. The native history reader
  returns bounded newest-first states; reading only one also loses a short
  tap that began and ended between render polls. Primary sources:
  https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/pad.c
  https://github.com/switchbrew/libnx/blob/master/nx/source/services/hid.c
  Installed libnx headers were checked for the exact APIs and state layout.
- Added DriveButtonQueue and Switch DragPadInput. They preserve chronological
  R/A transitions until a simulation tick consumes them, while analog axes,
  gas and other buttons stay current. Native HID history is read for the
  single active standard controller. Merged sources use snapshot queuing.
- Pause/source change starts a current baseline and blocks held R/A until
  release. Disconnect cancels pending presses. Duplicate history samples
  never replay an action. A fixed-capacity queue reports overflow; native
  history gaps are also counted. Nothing can recover overwritten HID states.
- Added nfsu_drag_input_tests against the actual Switch helper with declared
  HID API stubs. It covers missing tap/release witnesses, zero-world-tick
  polls, short HID taps, ordered R/A, holding, catch-up, pause, reconnect,
  common controller styles, merged fallback, bounded overflow, fresh analog
  input, and a recovered R reaching actual VehicleModel clutch/gear phases.
- The focused target passed after both initial implementation and the added
  physical-gear regression. Production code now differs from sealed r85;
  its old full gates do not validate this correction. Full gates and a new
  SDK build are required before packaging. No upload is authorized.

## Frozen r86 gates running

- Source snapshot: build/fidelity-r86/root-code-freeze-r1.json. Relative to
  sealed r85, only CMakeLists.txt and switch/race_main.cpp changed, with four
  new implementation/test headers and sources. No gearbox physics changed.
- Host tool session66732 runs root-final-gates-r86-r1.py: configure/build/
  CTest for Default, then Release, with retail data configured in both.
- SDK tool session96026 runs root-switch-build-r86-r1.py: fresh SDK Release
  configure, NRO build tagged fidelity-r86, then the shader/NRO checks.
- Do not edit production code during these runs. Results/logs are named
  root-default-release-validation-r86-r1.json and
  root-switch-validation-r86-r1.json in build/fidelity-r86. Neither complete
  gate result exists yet. Prior r85 results do not cover this new helper.
- All Git/index/ref/board writes, independent review, delegation and upload
  remain on HOLD. The sealed r85 package and evidence have been preserved.

## Final frozen validation: 2026-10-31

- Host session66732 completed all six phases successfully. SDK session96026
  completed all three phases successfully. Default and Release included the
  configured retail fixtures; Release also ran retail-full. None skipped.
- Exact commands, exit codes and full logs are preserved in
  build/fidelity-r86/root-default-release-validation-r86-r1.json and
  build/fidelity-r86/root-switch-validation-r86-r1.json. Every command
  returned0. The source snapshot is root-code-freeze-r1.json.
- Host commands, with Strawberry tools on PATH in this actual worktree:

```text
cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"
cmake --build build -j 5
ctest --test-dir build --output-on-failure -j 5
cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 5
ctest --test-dir build-release --output-on-failure -j 5
```

- SDK output directory retains its inherited suffix: build-switch-fidelity-r86-r3.
  Its tag is fidelity-r86 and its source is r86 freeze-r1, not an r3 source
  revision. The SDK command JSON records the actual directory and options.
- Local finalization uses root-finalize-r86-r1.py. Success is recorded only
  by build/fidelity-r86/root-final-r86-r1.json, which identifies the source
  archive and out/nfsu-r86.zip and verifies their NRO bytes, tag and input
  diagnostic marker. It also verifies complete registries, frozen source,
  ASCII and unchanged Git HEAD/index. Read that record before using a package.
- The automated tests establish the two reproduced input-loss paths and
  corrected delivery, not the exact cause of every owner-reported missed
  press. Live console acceptance and independent review remain pending.
- Next external check: one drag drive with short R taps and separated
  release/repress. Preserve DRAG_EVENT and DRAG_INPUT output. History gaps
  or queue overflows must be reported rather than claimed recovered.
- No original game was launched, no recording requested, no delegation,
  Git/index/ref/board write or console send performed. Publication remains
  on HOLD. The older r85 package and source archive remain intact.
