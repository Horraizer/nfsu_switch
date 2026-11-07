# Cross-mode physics omission audit

CHECKPOINT
role: worker
agent: Codex root solo
assignment: Owner asks whether r89 applies in other events and requests a
  further original-code physics audit, looking for omitted branches.
phase: investigating
branch: codex/draw-distance-original; remote freshness not queried under HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: held r89 source archive and root-source-freeze-r3.json
worktree: build/day20261008-root-combined
saved: local-only; build/fidelity-r90/root-entry-r90-r1.json and this checkpoint
verified: r89 has complete Default/Release retail gates and SDK checks; it is
  preserved unchanged as the baseline, not evidence of complete native parity.
decisions: Solo work, original files read-only. No Git/index/ref/publication,
  board, independent-review or console-send action while existing holds apply.
remaining: Verify cross-mode consumers; authenticate further omissions from
  original speed.exe before correction. Do not tune coefficients from a plot.
next: Trace original46DB60/4607A0 policy and compare the coupled drivetrain,
  engine and wheel-update consumers against the shared port implementation.
blockers: none for static analysis or supplied-state native execution
ownership: root active

Evidence and before copies belong in build/fidelity-r90 with unique names.
Do not overwrite r89 artifacts or immutable owner recordings. A complete
world-state restoration is not inferred from supplied-state leaf comparisons.

## Authenticated omissions and implemented corrections

Original4B5DF0 sets78A344 for drag and78A345 for drift. The earlier r89
comment calling78A344 a campaign flag was imprecise: its observed value was
correct for recorded event9, not a general career-mode predicate. Ordinary
race, drag and drift remain distinct. With the unsupported alternate/device
globals cleared,4607A0 is true in drift and false in ordinary/drag.

Original46DB60 selects descriptor+64 feedback in that assisted context and
uses driver-written gearbox+74 to change phase/throttle behavior under the
handbrake. The port omitted both. Supported drift now publishes those inputs
through VehicleModel; ordinary/drag defaults remain separate. Before the fix,
the supplied-state witness failed its assist/override comparisons. A separate
46E940 equality witness rejects the old <= half-quality boundary; equality
belongs to the .85 tier. Tables are gear_policy_native.inc and its adjacent
shift_boundary_cases, reproduced by root-policy-native-r1.py.

Original4784E7 calls479C30 before low-support torque4791F0. The port omitted
this call completely. The original adds a corrective BODY TORQUE, not extra
downforce or engine power, for front-driven setups with four outer-published
contacts, positive front longitudinal forces and speed below its cutoff. It
is disabled by the assisted-mode predicate. The restored consumer uses live
body axes/velocity, not the earlier wheel-axis snapshot. The original scalar
matrix dispatch is explicitly selected in root-fwd-native-r2.py; no force,
cross-product or math helper is replaced. front_drive_torque_native.inc
checks the complete output torque, including unchanged gate controls.

The production player powertrain suite passes the new supplied-state
comparisons. An actual recorded-command replay is saved in
build/world-recorder-cap-r1/port-drive-r90-r1; this demonstrates an effect on
the real RaceRun pipeline, while retaining the existing private-state,
initial-grid and cadence limitations. It does not establish complete live
native acceleration parity. All r89 output and source archives are preserved.

Unsupported manual-clutch policy3, additional device/alternate-world globals,
nonzero owner-state differential bias and complete shared-world RNG ordering
remain separate gaps; they are not inferred from mode names or coefficient
tuning. Native automatic downshift reference mode uses constructor+0xC8,
not the sampled up-offset; the existing -100 path is not changed merely for
looking suspicious. Original front/rear final-drive use is likewise retained.

Next: verify runtime dispatch controls, finish required Default/Release retail
gates at a frozen final source, audit ASCII/custody and prepare the local NRO.
No console sending, Git write, board post or review verdict is authorized.

The runtime dispatch regression now exercises ordinary, drag and drift inputs
with and without handbrake. It confirms the actual VehicleModel consumer,
not only direct calls to the recovered helper. All new supplied-state cases
pass after restoring the override's phase2 multiplier as well. Earlier failed
before/intermediate receipts remain preserved rather than overwritten.

Recorded-input evidence demonstrates the front-drive torque effect without
retuning handling. All source/table coefficients remain original-derived.
The proving-ground drive hashes for front-driven cars therefore change; the
car preview frame hashes remain pinned. Update only measured drive-state pins
after checking the changed set against drive layout and preserving before data.

The first two full matrix attempts stopped during linking: the separately
compiled gear-phase observer and runtime-oracle wrappers still declared the
older gear-update signature. Both wrappers now forward the new policy flags.
Those attempts did not run CTest and are not gate passes. SDK builds and
packaging/shader checks passed, but are not substitutes for the missing host
matrices. Run the final-source matrices after the observer targets link.

The recorded actor's gearbox owner is actor+0x40; its differential state is
zero in both saved full-memory samples (root-owner-context-r1.json). Also,
actor+0x20 points to setup77C800 in this recording, authenticating the COM
height producer consumed by479C30 against the already checked handling fields.
This does not prove uncaptured intermediate states or alternative owner modes.

## Three-fix source3 sealed; remaining aggregate callbacks

Both full source3 matrices pass without skipped retail checks. SDK source3
checks also pass. Exact receipts are root-build-validation-r3.json,
root-build-release-validation-r3.json and root-switch-validation-r90-r3.json.
provisional-r3 contains an authenticated source ZIP and NRO before further
changes. This local, unsent baseline is not full physics acceptance.

Remaining supported callback479290 is absent in the port. It runs in drag
after low-support torque, at support>=4, actor+38==0 and abs464ED0 slip<5deg.
Original429CC0 projects actor80 world linear acceleration onto forward; scale1.5,
clamp[-3,4] normally /[-3,7] with engine+84 nitrous flag, then negate and
project onto actor row+A0. Original584E00 adds that torque. The supplied-state
table root-drag-body-native-r1.inc was generated with no semantic hooks.

The heading dependency is already recovered correctly as player_owner_heading
(478AA0 integer/table path), but not declared in the public helper header.
root-heading-compare-r2 matches every recorded human post-physics heading.
Using player_velocity_angle on forward instead is wrong on many samples;
that substitution currently also exists in the drift-force producer. A new
cached owner heading must be set with owner outer publication/reset and used
with CURRENT velocity in464ED0, rather than reusing an old slip or converting
both angles with the velocity algorithm. Preserve its contact/history epoch.

42A860 is another aggregate callback. Human6C8AD0 virtual80 points to425050,
which returns0, so its steering branch is inactive for that class. AI6C8C18
virtual80 points to460150; inspect this before declaring the callback unused.
Relevant decompilation and42A860 disassembly are saved. It gates speed1..30,
body-upZ>=.1, support>=3 and nonzero virtual80; first damps angular velocity
using706490 and mass, then a steered-direction/normalized-XY-velocity torque.
Do not wire this universally to human steering just because it looks plausible.

Actor80 was initially misidentified as angular velocity in scratch variable
names. Original426930 authenticates its producer as (velocity - pre-world
velocity)/world_dt, after the contact branch. Angular velocity is actorD0.
The supplied-state torque bits remain valid; root-owner-native-r2.py names
and tests the actual producer, with no semantic hooks.

Next: finish these bounded caller/producer mappings and authenticated supplied-
state callbacks, implement supported ones, then rerun gates at final source.
All holds remain; the original recording and r89/provisional-r3 are preserved.

## Source4 producer and motion-epoch corrections

Original4267A0 saves pre-world velocity;426930 produces actor80 only after
the selected physics/contact branch. Added that linear derivative to the
world owner's once-per-frame post-pass for all four racers. Nested rival
advances do not publish it early. The existing camera uses the same cache.
Port terrain resets retain the existing explicit zero-derivative adapter;
this is not an authenticated native respawn policy.

Restored479290 on the supported drag branch, including support, state, slip,
normal/nitrous clamps and additive side-axis torque. root-owner-native-r2.py
executes the native derivative slice without semantic hooks; its combined
fixture also retains the previous native drag-torque outputs. Targeted
powertrain and race-session tests pass in root-target-r4d-*.log. Earlier
target attempts had a wrong test ground class/name and missing SI conversion;
they are retained and are not reported as successful validation.

Source3 passed live rigid axes/velocity to479C30, although the original reads
published actor70/90. PlayerOwnerMotion now retains the actual publication
epoch independently of each derivative, including heading and angular rate.
Reset, outer publication and queued continuation replace the motion cache;
contact refresh reads the published actor's cache. The selector3 publication
adapter also maintains the new fields. No extra physics heading conversion
is needed for the existing scalar slip cache.

Original479350 reads actor heading through478AA0/564FA0, current published
actor velocity through564DB0, cached angular velocity and actor up axis.
The former port converted the live rigid forward vector with564DB0 and used
live rigid yaw/up. The drift caller now consumes the correctly retained
actor motion; drag keeps priority over drift in the mixed diagnostic context.

The regression test observes all four actors after the shared contact queue,
and the drift test distinguishes rigid motion from retained actor motion.
The added caches are copied by VehicleState histories; the existing byte
trace remains a partial observable record, not a full restore checkpoint.

The car probe in root-pin-probe-r4a-retail.log still renders every original
preview identically. Only the same eleven front-drive drive pins move after
the corrected479C30 epoch; root-drive-pin-refresh-r4.json records the refresh.
Full source4 validation and the new recorded-input drive are pending.

42A860 remains an explicitly open special-AI callback.460150 gates it on
driver220 and retains a signed90-degree direction at222.45E970 consumes220
as special mode6 and applies an override; the port's ordinary forward state
does not yet represent220/222. Native human virtual80 returns zero. This
does not justify applying the callback to ordinary human steering, nor a
claim that special AI recovery matches. Its writer and controller context
still need authentication. Alternate world globals also remain outside the
supported zero-default campaign context. No console acceptance is claimed.

## Source4 final freeze and newly isolated launch traction omission

Source4 is frozen in root-source-freeze-r4.json. Both host build phases pass;
their full CTest phases are still running. SDK root-switch-validation-r90-r4
passes all phases. Do not mutate frozen source while this matrix runs.
The final target run root-target-r4f passes powertrain and race-session tests,
including tilted native drag torque and original477B3D nitrous grip fixtures.
The latter restores drag-only0.6 grip through the inclusive half-second window.

port-drive-r90-final-r4 completes the recorded-input world drive. Its exact
repeat and zero-gas control pass in that folder's validation.json. The plot
root-launch-comparison-r90-r4.png shows that full acceleration still differs;
these fixes do not establish trajectory parity or close the RPM discrepancy.

New concrete cause: original479640 scales EVERY wheel178 when gearbox68
(shift_quality, NOT feedback_scale) is positive. It uses the class table
6F100C+(drive_family*27+ride424)*4 and factor1+min(1,quality/limit), with
native reciprocal-then-multiply order. This scaling is absent from the port.
Its existing player_drag_shift_boost changes the engine multiplier only.
The owner recording has quality0.5 during launch; ride77C7A0+424 is class3
in both full-memory samples. root-shift-grip-context-r1.json authenticates
that class producer.57F2B0 derives424 from performance-area thresholds;
RaceSession already supplies VehicleInput.drag_category from those classes.
Do not confuse gearbox68 with feedback_scale atE4, which the recording omits.
An exploratory print at engine1C was not a valid feedback-torque decode and
must not be used as evidence for engine feedback.

Next: while source4 gates finish, produce an original479640 first-branch
fixture with no semantic hooks (synthetic actor role0 skips the separate
active stateful traction-policy branch). Authenticate all four class limits
and three drive families. Seal source4, then implement shift grip scaling
in the outer wheel-owner-grip producer, preserving its order after the
nitrous window and before component stepping. Use existing drag_category;
no ABI field addition is needed. New source5 needs fresh full host/SDK gates,
recorded-input comparison and controls. No source5 change is made yet.

## Source5 implemented; source6 base-grip correction pending

Source4 was sealed in provisional-r4 after both full host gates and SDK pass.
Source5 adds player_shift_grip_scale and the original479640 first-branch
fixture. The C++ operator and runtime wiring across all drive families and
classes pass root-target-r5a. VehicleInput.drag_category already carries
the recovered performance class, so no new ABI field was added. Native
6F100C rows are authoritative; the AWD class3 limit is1.4, not1.5.

Source5 is frozen in root-source-freeze-r5.json. Release CTest is running.
Default BUILD FAILED before CTest: libnfsu_track_stream.a could not be removed.
The manual whole-drive link was reading all Default archives concurrently
with the full build. This is a real failed run, not a successful gate or
source defect. Do not manually link any Default archive while its full build
runs. Final source needs a fresh complete Default/Release matrix regardless.

port-drive-r90-shift-grip-r5 completes; launch speed increases as expected,
but the first-launch RPM now drops below the original, and later acceleration
still differs. port-drive-r90-native-dt-r5 uses captured physics intervals;
its early discrepancy is similar, so changing the cadence is not a fix.
It diverges into later contacts and is not a complete restored-world replay.

Another concrete common omission is original477775..4777BF: physics78 is
the base grip, multiplied by6FFDD8=0.85 for owner3C0==0 and !assisted.
The port uses a fixed1 instead. All sampled human physics78/7C values are1;
root-live-grip-context-r1.json records them. Full memory authenticates owner
mode0 at both sampled boundaries in root-owner-modes-r2.json. This establishes
the supported owner0 case, not uncaptured alternative modes.

root-base-grip-native-r1.py executes the exact original scalar/stack-store
slice with no semantic hooks. Its fixture is still in scratch only, preserving
the running source5 freeze. Source6 must apply the original base multiplier
before rear-grip, nitrous and shift-grip stages. It is a shared selector1
ordinary/drag correction; drift's assisted baseline remains1. Expect ordinary
drive/race regression pins to change, including RWD cars. Never restrict it
to drag merely to keep old pins green. Refresh only derived test pins, with
recorded outputs and unchanged static previews; then run final full gates.

The rest of479640's stateful traction policy (owner3C8/CC/D0/D4) is still an
open, separately identified context. Initial and10-second samples show its
mode0/scale1; intermediate activation is not captured. Do not claim it never
runs. The special-AI42A860 context also remains open. No full native-physics,
independent review, Switch runtime or publication acceptance is claimed.

## Source6 checkpoint, 2026-11-01

Common base grip now uses original477775 mode0 factor0.85. Native fixture
base_grip_native.inc verifies48 cases without semantic hooks. The production
front-wheel test covers ordinary, drag and drift priority. Together with shift
grip this brings new r90 supplied-state rows to1676; this is not a full-world
parity claim. Targeted powertrain and race-session tests pass in root-target-
r6a logs. All20 proving-ground dynamic pins changed; all20 static preview
pins stayed identical. The subsequent INTEGRA dynamic sequence is refreshed
from a separate recorded run. Source6 complete gates are the next boundary.

The source6 recorded-command run completes1584 frames. Its first second is
closer to the recording, but later acceleration remains different. Detailed
dyn-r6b confirms linear damping is0, angular damping1 and alternate yaw1,
matching native flags: do not patch continuation damping on speculation.
Native force is higher near1.25-1.75 seconds despite similar crank feedback;
this is an unresolved wheel/drivetrain interaction, not proof of bad inertia.
Original4280A0 is a timed recovery/immunity predicate using actor884/888;
it is not a drag/drift flag. Remaining479640 stateful differential policy
still needs authenticated producer/consumer wiring. The driver-motion probe
has no valid human role rows and cannot prove an absence of driver mutation.

Source5 Default build failed when our manual runtime linker read an archive
that CMake was replacing. Its CTest never ran. Source5 Release and SDK passed;
provisional-r5 truthfully records the failed Default matrix and is not accepted.
No manual link may overlap the final Default build. Holds remain in force.

## Source7 stateful traction, 2026-11-01

The common0.85 correction changes the scripted driver commands, not only state
hashes. Source6 Default/Release runs failed old-recording comparisons and were
cancelled before further edits; root-source6-cancelled-r1.json records the owned
process identities. provisional-r6 preserves its exact source and successful
SDK NRO but explicitly says NOT_ACCEPTED. No stopped/failed matrix is green.

The complete original479640 supplied-state branch now has2940 cases in
traction_policy_native.inc, including actual4280A0/464ED0/454F30 execution.
Original705E44 points to six slip/scale knots at6FFEBC. Activation stores
scale=curve/ride_limit, mode=trunc((1.5-curve)*6+wheel_parity), half-second
timer and quiet0. Positive slip, speed, angle, timer/quiet boundaries and
role/immunity bypasses are covered without semantic hooks. The standalone
prototype matches every output bit. The original46E020 mode1/2 differential
consumer has1296 additional no-hook cases, including contact/lock masks,
neutral/reverse and three drive families. Other modes retain speed-based bias.

Production source7 carries owner mode/timer/scale/quiet, the preceding prepared
wheel158 load and wheel160 slip, updates grip once per outer selector1 call,
uses world73457C interval when supplied, and passes mode into both torque
distribution stages. Queued contact continuation copies owner policy from the
published actor instead of rewinding it with rigid state. Selector3 AI and
assisted drift do not gain this selector1 algorithm by analogy. The new
suppression input represents the4280A0 gate; normal current racers usefalse.
Its original timed actor884/888 producer is not yet a restored live port
subsystem. Alternative owner3C0 modes and special-AI42A860 remain open.

First source7 targeted powertrain run passes native rows but fails the added
world-interval consumer witness: that witness manually sets forward speed1
while retaining the stationary reset slip angle. Correct the supplied cache
coherently, then rerun. Race-session target passes. The first manual whole-run
link produced no COMPLETE output while some non-target archives still had
the old VehicleState ABI. It is an UNKNOWN runtime result, not a physics
comparison. Rebuild all Default dependencies before fresh runtime replay.
Next: targeted consumers/oracles, fresh recorded-command replay, regenerated
scripted-driver recordings and derived pins, then complete frozen gates.

## Source7 consumer confirmation and recordings

All5912 new r90 native supplied-state rows now pass production tests. The
world-interval witness is corrected with a coherent nonstationary published
angle; powertrain and race-session tests pass in root-target-r7d logs. Steering
and stability pass; vehicle tests report one changed mixed telemetry pin.

A fresh full-archive recorded-command run completes in port-drive-r90-traction-
r7b. Its analyzer compares every racer with the owner recording. Player speed
and accepted-gear progression are much closer to native after restoring the
stateful grip/differential, without changing inertia. Late rival divergence
remains and cannot be called matched from this fresh-grid/RNG run. The failed
first r7 artifact remains preserved and is not used as comparison evidence.

A further cross-event wiring omission was confirmed: ride424 was calculated
only by set_drag(true). The RaceSession constructor now derives it from the
installed handling for ordinary racers too. A high-power ordinary-class witness
and drag equality pass. The field name drag_category is retained for API
compatibility; the original class is shared by479640.

Both revised scripted-driver recordings finish with zero resets and unchanged
retail manifests. Outputs and provenance are in regenerated-r7; prior files
are preserved in before-r7. Copying these recordings requires refreshing their
input hashes, dynamic state/render pins and final sample ticks. This is an
expected fixture regeneration following verified common physics corrections,
not evidence of native whole-world parity. Full final gates remain pending.

## Recorded-interval control remains unaccepted

Source7 fixed60 replay is close to the owner player recording, but the native-
interval control in port-drive-r90-native-dt-r7 again accelerates too slowly.
Do not present the fixed60 sample as complete native physics parity.
Passive dyn-r7b-fixed/native reproduces each original run hash exactly. The
owner traction activation differs: fixed60 enters mode3, the interval control
mode4; later they retain mode2 versus mode3 before quiet expiry. This changes
the differential branch and longitudinal force. The early lateral force of
the owner recording has the interval-control sign. No tuning or forced phase
selection is justified from these partial observations.

Eliminated proposed causes by source/data: RaceSession already publishes
world_frame_seconds and world_clock; human solver count already uses the
original variable count; early original physics calls are250 unique frames
in two seconds, with no duplicate callbacks and coherent interval sums. Do
not patch these existing producers on speculation. First dyn-r7 scripts fail
C++ compilation due to an escaped-newline bug; r7b scripts correct it and
preserve the failed outputs.

The interval control still begins from a fresh grid with fixed60 warmup. It
does not restore an atomic original car/wheel checkpoint or shared RNG. The
continuous tape omits actor408/40C/410/414 and per-wheel158/160/178, so it cannot
show the actual native transient phase. These are the precise next recording
fields needed for a definitive trajectory/step comparison; no new owner
recording is requested now. Existing full memory is sparse/asynchronous.

Pointer correction: actor18 is NOT the selector1 physics system. The initial
wheel-coverage-r1 probe used that wrong pointer and is INVALID evidence.
Actor34 and SNAP header10 independently identify physics3BF0868. The corrected
coverage report is root-initial-wheel-coverage-r2.json. Keep this distinction
when continuing the audit. All production snapshot-based physics78/grip reads
use the proper SNAP physics block; no production change follows from this
annotation correction.

Remaining local authorized work: refresh derived pins/input hashes against
verified regenerated-r7 and race-capture-r7 outputs, preserving invariants;
full frozen Default/Release gates, SDK build/checks, ASCII/held-state checks
and final local candidate. Console, review, Git and publication holds remain.

## Source7 final gate entry

The source now carries verified regenerated stock/performance inputs and all
related replay hashes, extent/timing verifiers, default/glow/tuned/shaded/wet
frame pins. Every effect capture has identical state/trajectory/results; the
physical upright/contact/minimum-speed assertions remain unchanged. Lap-boundary
samples follow the measured new crossing. source7 input and pin provenance is
recorded in the refresh receipts, including native-overlay smoke and synthetic
demo/upgrade regressions. No test was weakened to accept an altered result.

The independent CLI and RaceRun stock result agree. The upgraded RaceRun
replay completes twice with identical state/step hashes with logging on/off.
The doc-only camera frames have11 fresh matching printed/written Default
outputs; Release agreement remains pending. Failed doc-r7 used the wrong
executable name; successful r7b uses nfsu_probe. All failures are preserved.

The final freeze includes tools as well as C++ and test sources, since the
replay validator changed. Run full Default/Release configure/build/CTest and
SDK checks before any candidate claim. No archive linker may overlap Default
build. After gates: Release document-frame agreement, actual final replay
repeat/zero-gas control, ASCII/raw-path scans, held-state/retail checks, local
source/NRO seal. Do not send or publish. Native full-world parity, transient
initial wheel state, additional owner modes, live timed gate producer and
special-AI context remain unaccepted; this is a source correction candidate.

The47AA40 zero-support sleep protection was audited against42AA70 and is
already present. No proposed change was made.42AA70 checks support or a valid
body-pair supporting face; it is not a producer of owner3C0.479160 supplies
strength to engine/body/aero/tyres and remains in its known strength1 context;
it does not justify guessing the unrecorded actor404 producer.

## Source7 verification follow-up

Source7 full matrices were not green: Default reported obsolete acceleration
hash/transition expectations and gameplay linear-filter image pins. Release
also reproduces those failures. The Default/Release document-camera streams
and all11 written frames match, and the final owner-input replay repeat and
zero-gas negative control passed. These do not establish original-world parity.
The SDK build succeeds;13 of14 checks pass, with the remaining check retaining
old stock/performance replay input identities. The prepared SDK-only r8 guard
script must NOT run: host matrices are not green, and additional host test
expectations now need updating before a new complete matrix run.

Diagnostic copies in remaining-harvest-r8b and remaining-native-r8b omit only
obsolete golden comparisons for harvesting; production source and semantic
assertions remain frozen. Preserve failed r8 diagnostic compile/launch attempts
and source7 test failures. Refresh no physics constants to fit these results.

## Source8 final gate entry

Source7 Release completed with10 failed tests; its incomplete Default run was
cancelled using proven PID/start identity before edits. Both cancellation
receipts are retained; r1 aborted without termination because CIM truncates
start times to microseconds, and r1b used checked100ns/microsecond equivalence.
The SDK/source7 NRO is preserved as NOT_ACCEPTED in provisional-r7.

Source8 refreshes the remaining portable regression expectations, including
all19 proving/circuit outputs, upgraded acceleration/transitions, mixed CLI
telemetry, current576-step selection operands and texture poses/images. New
current-prefix cases execute actual native requests3/2/3, retaining the old34
records. No mass/inertia/torque/ratio coefficient or physical acceptance bound
was fitted. The existing34m/s sustained-speed assertion moved out of the
shortened mixed steering/brake demo into a30-second straight drive; the demo
still requires pedal causality against the matched zero-gas timeline.

The grounded native collision harness formerly overwrote6FFDD8 with1 to match
the old port boundary. That override is removed. Eight fresh unmodified-native
grounded queue cases now match the port exactly, alongside the22 existing
non-grounded cases. The fixture continues to exercise actual response and
continuation. The first targeted retry passed9/11; only the newly added speed
test lacked the normal released-control connection prefix, and one old wall
contact timing expectation remained. Correcting those test inputs/expectations
then passes the last5/5 targets, including all texture pairs/native references.
Retail manifests remain unchanged. Full frozen Default/Release and SDK gates
must now run on source8, followed by a freshly linked owner-input replay,
ASCII/held-state scans and a local unsent source/NRO seal. The earlier r8
SDK-only refresh script is obsolete and must never run. No source8 production
physics differs from source7; course regression constants and test/oracle
sources differ. Full original-world parity remains unaccepted for the exact
limits already recorded above.

## Additional read-only producer audit during source8 gates

root-owner-producer-candidates-r2.json is a linear-write candidate list, not
proof that each matching displacement belongs to a car. Ghidra decompilation
then authenticates431020/456BA0 copying actor400 from participant+22; other
400/404 candidates such as4854E0/486032 are different object types. Participant
configuration and supported current mode0 are distinct from race difficulty.
Do not connect those candidate writes to physics by displacement alone.

427E60 and427F70 set actor884 to the world clock and actor888 to2 seconds
only when their caller flag requests it; false clears the start marker.
4280A0 rejects0/7fffffff starts and uses strict elapsed-time<duration. The
specific live caller/respawn adapter remains unaccepted, not a universal
2-second suppression assumption.45384C writes driver220 and actor88C, while
460150 consumes driver220/222. Their active caller context is still needed
before enabling the special42A860 assist in normal rivals. These findings
add source locations to the existing limitations; they do not change the
frozen source8 candidate or claim those integrations complete.

Sparse context confirmation is preserved in root-recorded-policy-context-r3.json.
The two available original memory times retain normal owner mode/strength,
zero transient traction state, no damage mask, no active timed start and no
special-actor flag. These support the declared normal context; they do not
supply atomic wheel checkpoints at GO or prove special contexts irrelevant
in other events. No live process or retail file was changed for this audit.

## Source8 local verification completion

Full Default/Release configure, build and plain CTest completed with no
failures or skipped retail checks on root-source-freeze-r8.json. The SDK
configure/build/package checks also passed. Exact commands and exit codes:

- `cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"` -> exit 0
- `cmake --build build -j 3` -> exit 0
- `ctest --test-dir build --output-on-failure -j 3` -> exit 0
- `cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release` -> exit 0
- `cmake --build build-release -j 3` -> exit 0
- `ctest --test-dir build-release --output-on-failure -j 3` -> exit 0
- `/usr/bin/cmake -S . -B build-switch-fidelity-r90-r1 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r90 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF` -> exit 0
- `/usr/bin/cmake --build build-switch-fidelity-r90-r1 --target nfsu_switch_race_nro -j 2` -> exit 0
- `/usr/bin/ctest --test-dir build-switch-fidelity-r90-r1 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"` -> exit 0

Fresh source8 owner-input CPU replay, identical repeat and zero-gas negative
control pass. Source7/source8 STATE and COMPLETE lines match. Native leaf
and native queue evidence remain limited to their declared held-input and
controlled-geometry contexts. The variable-interval original-input control
still differs; omitted atomic initial wheel/owner fields, live timed-gate
producer, additional owner modes and special-AI behavior are not accepted.
No original acceleration result was fitted by changing physical constants.

The local candidate is out/nfsu-r90; its source archive and exact NRO hashes
are bound in root-final-validation-r90-r8.json. No Switch runtime, independent
review, commit/push/PR, transfer or publication was performed. Git/index/ref
and board holds remain. Next external evidence is an atomic native initial
state including wheel158/160/178 and actor400..414, followed by variable-step
replay; ordinary owner recording currently does not contain those states.
