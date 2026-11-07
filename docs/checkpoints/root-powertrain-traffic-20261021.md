# Powertrain, tachometer and traffic contact audit

CHECKPOINT
role: worker
agent: Codex root solo owner integration
assignment: Owner added acceleration, tachometer and traffic collisions to
  the ongoing Ghidra original-code restoration. Continue sequentially.
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local-only dirty integration. Audio implementation and required matrix
  are recorded separately in root-engine-envelope-20261021.md and ignored
  build/engine-audio-r24. Keep sealed r23 untouched and do not send yet.
verified: Read the historical powertrain/tachometer/contact checkpoints and
  current production sources. Historical bounded arithmetic is not full live
  car/setup/clock/ground/contact equivalence. Do not repeat the old guesses
  from VEHICLE.md's early approximation as claims about today's solver.
decisions: Sound first; then acceleration and physical/published/HUD RPM as
  one input-to-output chain, then all ordinary traffic contact directions.
  Attribute concrete original-code divergence before changing constants.
remaining: New matched original/port launch/coast/shift/boost trajectories,
  complete tachometer source/consumer checks, original traffic reaction and
  coherent native/runtime validation. Preserve the earlier knockout/locked
  upgrade/unique-menu fidelity items; those are not silently accepted.
next: Use original control and publication functions to pin selected-car
  trajectories and trace traffic continuation/material/ground/setup inputs.
blockers: none for local investigation; hardware acceptance is still pending.
ownership: root active; no delegation, commit, push, merge or publication.

## Initial native audit

Fresh race_powertrain_check captures plus race_powertrain_native comparison
passed for selected UG0 inputs. This checks original engine/export/cutoff/HUD
arithmetic with portable-selected handling; it does not prove tuning selection,
full body/ground trajectories or live input timing.

The old full coupled-constraint witness covered only stock rear-drive240SX.
A scratch extension ran actual stock CIVIC, INTEGRA, ECLIPSE, SKYLINE and240SX
records through original drive-class selection, wheel/engine/clutch/gearbox
kernel and portable helpers. All supplied cases matched exactly. Evidence:
build/powertrain-traffic-r25/layout-r1/comparison.json and per-car reports,
including unchanged retail manifests and original/portable source copies.
This is isolated state coverage, not full consecutive driving equivalence.

Ghidra472830/471D60 shows that the original hull-cache dirty gate compares
origins within.001, not orientation. The port's same gate is intentional;
do not change it merely because stationary rotation initially looked suspect.
A fresh randomized hull witness is in progress to check world-coordinate and
orientation inputs beyond the historical near-origin examples. First scratch
compile used a nonexistent helper name and failed; r2 corrects the helper.
No production change follows from that failed probe.

Next: finish generalized contact geometry checks and matched consecutive
powertrain/tachometer sequences before selecting a concrete runtime fix.

## Traffic producer/controller/clock correction

The generalized hull comparison completed without differences on supplied
rotations and translated world coordinates (hull-r2). This rejects a geometry
rewrite as the immediate fix; it does not prove every possible contact.

Confirmed production divergences from5754B0 and45E970:
- The ordinary mover published tangent-only horizontal V and zero W. Original
 575727..57581D normalizes the completed position difference to mover speed,
 publishes its magnitude, and derives yaw W from wrapped heading change.
- After583E80 selects physics3, controller4 chooses mode4. The port continued
 mode3 forever, used racer-curve lookahead instead of mode-specific linear
 lookahead, and supplied all wheels supported. Mode4 retains histories,
 uses real support/direction gates and its own steering/pedal coefficients.
- The post-impact component wrapper discarded held driver gear requests.
- Collision state/recovery and ordinary engine cutoff used component age in
 some callbacks while pair records used73AD34 world time. A later-spawned
 traffic actor therefore could not share the same recovery interval.

Implemented explicit publication and driver helpers, native traffic3/4
lookahead, held gear-command forwarding and shared world-clock inputs.
Standalone existing physics fixtures retain their explicitly selected local
clock; actual RaceSession/traffic now supply the common timer, constant over
all substeps and resumed contacts. Initial spawn orientation/road-height and
traffic planner admission remain explicit port adapters, not silently claimed
original terrain/spawn equivalence.

Native tools/traffic_impact_driver_native.py verifies original45E970 histories
across activation, air/recontact, braking, reverse movement and disable inputs.
It also verifies575727..57581D publication on supplied old/new road positions.
Fixtures live in tests/traffic_impact_driver_native.inc and
traffic_publication_native.inc. Generator r1-r3 refused missing original code
ranges for short-path helpers; they are not passes. The final r5 run executed
without hooks. Early C++ test attempt failed aggregate braces; next comparison
exposed the missing lookahead and low-target branch, which were corrected.

Targeted root-tests-driver-r6.log passed all selected checks, including the
original world contact episodes and a live traffic adapter check that
explicitly distinguishes the old supported-wheel driver and mismatched clock.
This is not hardware collision acceptance. A full mandatory matrix must follow
this new source boundary; r24's passed gates are not reused as r25 acceptance.

## Original crash camera conditions

Owner additionally requested the side-camera trigger conditions. They are
recorded in docs/CRASH_CAMERA.md. The original post-transition gate plus
common cinematic gate were executed on controlled inputs: camera-gates-r2
passed threshold/equality, role, body-kind, modes, view count, road eligibility,
option and timer boundary cases. The road-eligibility result is supplied and
the GPU constructor is observed rather than executed; those boundaries are in
the report. The first probe lacked the active-view registry and failed an
unmapped read; it is not counted as a verdict.

No camera implementation or console acceptance is claimed. Acceleration/RPM
arithmetic evidence remains bounded to selected inputs and does not establish
full original body/terrain trajectories. Next: required matrices, final native
fixture verification, seal combined NRO, then pending owner hardware control.

## Additional camera path and validation boundary

Read-only426930/429CF0 analysis found a separate airborne event6 request,
terrain-clearance/duration prediction and option-dependent curves.426930 also
returns to normal on support recovery and can transition event6 to event7
while a crash persists.420110 owns the fallback timeout callback. These are
recorded in docs/CRASH_CAMERA.md with evidence and the distinction between
native-executed contact gates and instruction-checked additional branches.
No camera runtime implementation was added.

Runtime source boundary root-source-receipt-r7.json is frozen while the full
Default/Release matrix runs. SDK root-sdk-r7.log completed its required gates
with exit0 observed. The host matrix is still in progress, not a pass.
root-source-overlay-r7.zip preserves the complete current source tree and
receipt before these documentation-only additions. No console send, commit,
push, merge, publication or delegation is authorized or performed.

## Required-matrix failure and attributed pin correction

The r7 Default full run failed only nfsu_car_selection_retail: the RX7,
S2000 and350Z drive hashes changed. No frame pin moved. A link-wrapped
legacy-clock run reproduced all three previous full-drive hashes; a current
run reproduced all three new ones. Original46CA90's cutoff portion, stopped
before downstream sound46CCC0, matched every recorded current invocation.
Evidence: clock-audit-r1/current.csv, legacy.csv, both pin logs and
clock-audit-r1/native-r4/report.json. The original reads common73AD34 at
46CC5E/46CC8C; it does not read component age. Only the three attributed drive
pins were updated in tests/car_selection_tests.cpp with that reason.

Scratch native r1/r2 refused undeclared downstream helper execution; r3 had
zero arithmetic differences but its assumed call-count assertion was wrong
because internal preparation adds calls. These are not passes. Final r4 uses
all actual current calls and a fresh evidence subdirectory. Retail before/after
manifests match. This verifies cutoff with supplied trajectory inputs, not a
complete original body trajectory. No speed/torque/difficulty coefficient was
changed in response to the hash differences.

The unfinished r7 Release run was cancelled after proving the known Default
pin failure; it is not a Release pass. PID plus creation-time identities were
recorded in root-cancel-r7-processes.json before stopping the owned gate.
A fresh r8 matrix must run after this test-source boundary. Runtime sources
remain identical to root-source-receipt-r7.json; only docs and the three test
pins differ. No local NRO is accepted or sent on the strength of r7.

## Local validation and recovery handoff

At base ea4d472c9c399265370fa10be94736b1121771ad plus the uncommitted source
receipt root-source-receipt-r8.json, the full required host matrix r8 passed.
The precise commands and exit results are recorded verbatim below; each
CTest command was plain full CTest, with the standalone toolchain on PATH
and no skipped retail checks:

- cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT="E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"
  Result: exit0, PASS.
- cmake --build build
  Result: exit0, PASS.
- ctest --test-dir build --output-on-failure
  Result: exit0, 100% tests passed, 0 tests failed out of 246.
- cmake -S . -B build-release -G Ninja -DNFSU_RETAIL_ROOT="E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release
  Result: exit0, PASS.
- cmake --build build-release
  Result: exit0, PASS.
- ctest --test-dir build-release --output-on-failure
  Result: exit0, 100% tests passed, 0 tests failed out of 251, retail-full included.

Host receipt: root-required-host-r8.json, with checksummed logs per command.
SDK r7 configure/build and its full selected source/shader/package gate passed
with exit0 observed. Runtime source identity against the host r8 boundary is
recorded in root-sdk-source-reuse-r8.json; differences are docs and attributed
host test pins, not SDK inputs. Bash HEAD path/blob/mode ASCII checks passed;
current source bytes, new paths and private automation ASCII checks passed.
Scoped git diff --check passed. Main checkout tracked state remains untouched;
its two pre-existing untracked rev234 text files were preserved.

The local seal target is artifacts/nfsu-traffic-clock-r25.nro under this
checkpoint's build/powertrain-traffic-r25 evidence directory. The seal script
requires all six successful host records, source identity, SDK evidence and
original native comparisons before creating it. seal.json, SOURCE_RECEIPT.json,
VALIDATION.json and LOCAL_EVIDENCE.zip are the final artifact records. Sealed
r23 and r24 remain untouched. No console send, commit, push, merge, publication
or delegation was performed. No independent review is claimed.

Owner hardware acceptance is still pending for traffic impacts, sound and
complete driving feel. Acceleration/RPM comparisons cover supplied states,
not complete original terrain/body trajectories. Traffic terrain orientation
and spawn adapters remain explicit; full original trajectory equivalence is
not asserted. Crash-camera conditions are documented, not implemented.
Next: obtain a resumed owner instruction for a console run; use the sealed
r25 candidate and record runtime evidence. Do not repeat the cancelled r7
Release run as a pass or use its known pin failure as the current verdict.
