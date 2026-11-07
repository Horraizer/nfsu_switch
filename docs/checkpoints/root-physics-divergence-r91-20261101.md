# Original world-step divergence investigation

CHECKPOINT
role: worker
agent: Codex root solo
assignment: Owner asks to establish causes of original/port behavior divergence;
  installation of useful missing tools is authorized.
phase: authenticated corrections validated; local NRO candidate prepared
branch: codex/draw-distance-original; Git/board/publication/transfer holds retained
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: local only; build/fidelity-r91 and this checkpoint
verified: r90 source8 freeze unchanged at entry; Default327/327,
  Release332/332, SDK14/14 remain baseline evidence, not original-world parity
remaining: Original whole-world/AI equivalence, Switch runtime and independent
  review remain unverified/held; do not claim complete native world fidelity
next: Use existing captured AI/traffic state to isolate later encounter
  divergence; preserve frozen source3/NRO and current owner transfer/Git holds
blockers: none for current supplied-state/code investigation
ownership: root active

No new recording, retail mutation, computer use, delegation or console transfer
is required for this initial pass. Preserve r90 output/source packages unchanged.

## Native publication and supplied-control isolation

4789D0 publishes wheel spin2F0, load310, compression340 and contact350 into
actor bytes already captured. The earlier r90 statement that load158 is
unavailable from this tape was too broad: private wheel memory is missing,
but its terminal published load is present. pair374 is a derived skid vector,
not an authenticated per-wheel raw160 substitute. Native calls confirm007F
and SSE settings1F80 with status bits. Raw/private initial histories remain
partly inferred; do not label the seed a complete checkpoint.

root-direct-tape-r1 exports accepted driver1D0/1DC/1E0/1E4 controls and the
actual46E5B0 input request, original4774A0 interval and world clock/RNG.
root-direct-player-r1.cpp executes the current shared VehicleModel against
these inputs, starting from available native GO state and documented calm
wheel-cache assumptions. r1 failed to compile; r1b used an incorrect seed
boost timestamp8C (a shift-table value), so its overshoot is INVALID. Correct
64 timestamp is used by r1c/r2. At22 seconds native-body r2 is172.834km/h
versus172.927 recorded, while r90 reconstructed-input native-dt was159.38.
These are observations at the unchanged r90 source8 freeze, not acceptance
pins or full native parity. Same powertrain/input with port-prepared body
chooses traction mode4 versus mode3 and is already about12km/h behind at10s;
its22s result is invalid for road speed because this no-contact diagnostic
leaves the road. Do not quote its falling speed as acceleration evidence.
Native GO pose is stable across the entire recorded warmup; the port's
loading-time prepare_grid leaves a different pose and residual motion.

## Authenticated missing rest consumer and refresh

Ghidra originals47AC00 and477300 establish more than predicate47AA40:
- mode1 scales rigid P/L and all four wheel spins by0.95;
- mode2 zeros P/L and, when published support>0, wheel spins;
- gravity/body active flags follow the selected rest state;
-477300 requests another46ABD0 component refresh with6F0738=0 when
 47AC00 returns its rest-active result. The refresh includes585810 caches,
 struts/wheels and gearbox before4783C0 force preparation.
Current VehicleModel uses the predicate and zeros P/L for mode2, and already applies
mode1 damping/wheel-spin changes plus cache/hub-velocity refresh. It does NOT
repeat the complete strut/wheel/gear component refresh. The earlier broad
omission statement is corrected here after rereading the actual frozen source.
prepare_grid also deliberately disables rest to settle terrain. This is a
confirmed source omission, not proof yet that it explains the entire replay.
Next: execute controlled native47AC00 consumer cases, implement the matching
normal-context consumer/refresh order, compare identical-input traces and
run required gates only after the production source is stable. Preserve all
r90 source/NRO and failed r91 diagnostic outputs. Original bytes remain
read-only; no Git/index/board/console mutation or delegation occurred.

## Source corrections and native bump oracle

Production now repeats the entire component refresh at dt0 in a bounded
second pass after rest or a full-compression bump callback, before evaluating
force once. Rest damping is applied once; no extra derivative/time step and
no recursive stack frame. Obsolete partial hub/cache-only refresh removed.
Native588AB0 and584E30 reveal a previously missing bump impulse at the current
strut hub, with default6F073C=.2, inverse mass and separate generic impulse
caps. tools/player_bump_native.py executes these original functions and their
actual matrix helper with no semantic hooks.26 supplied-state cases include
axis rotations, inward/outward motion, tilted normals and both caps; bitwise
fixtures live in tests/player_bump_native.inc. Native dot order is Z+Y+X;
a first X+Y+Z implementation differed by1ULP in a large-velocity rotated case
and was corrected from the disassembly, not relaxed. These fixtures do NOT
claim original whole-world/terrain parity. Native public actor330 bump scalar
publication remains outside this physics correction; helper returns it.

New runtime tests distinguish both rest phases from an explicitly awake
control, require zero-time gearbox advancement and preserved crank/time,
and demonstrate a landing bump's immediate momentum/cache and gearbox
refresh effect. All player powertrain tests currently pass. The old isolated
rigid runtime witness now uses ground below its pitched body to exclude the
separate bump callback it intentionally did not model. Full required gates
are still pending. Current source differs from the r90 source8 freeze.
The first full-RaceRun r91-native-dt-r1 links the previous build before the
last partial-refresh removal; preserve it but rerun after the fresh build
before quoting comparison values. No console/Git/board mutation.

## Correction to provisional bump operand interpretation

The first synthetic bump fixtures supplied inverse mass at rigid144. That
assumption was WRONG and those r1 fixtures are not valid handling fixtures.
The recorded Civic snapshot has1.18 at144. Ghidra584B10 confirms144=raw mass,
148=inverse. Production and generator corrected before acceptance; r2 now
executes the actual584B10..584B3D mass-writer prefix for every case and asserts
both fields.26 corrected cases pass bitwise, no semantic hooks. r1 artifacts
and failed assertions are preserved; do not cite them as handling evidence.
The earlier checkpoint's inverse-mass description is superseded by this entry.

Default nonretail exploratory run completed148 cases with8 failing test
executables. These include old replay pins and old observer assumptions that
zero-dt refresh could not retain a nonzero cached strut rate. They are NOT green
gates. Dynamic ramp/drop assumptions also need investigation; no pin has yet
been blindly replaced. Full whole-RaceRun native-dt replay r2 completes the
script, but its speed is ruined after contacts beginning at15.55725sec; it is
NOT matching the original complete world. Sampled original actor/traffic
states are available in samples.jsonl and must be used before blaming input.

## Pre-race traffic omission

Immutable samples.jsonl confirms11 native traffic actors move during stage3;
root-native-prerace-traffic-r1.json records same-identity endpoint positions.
The first isolated full-world diagnostic root-world-contacts-r1 shows NO
active port traffic at GO, then a traffic SUV/rival impact at15.55725s and a
taxi/player impact at17.22225s. This rules out interpreting the later stopped
human speed as an engine-envelope discrepancy. It is not proof of matching
original world initial RNG, pool lifetime or spawn contexts.
RaceSession had wrapped traffic.advance in !race_car_held. It now advances
traffic in the owning world throughout staging/countdown; racer contacts
remain held until GO. Nested rival sessions still cannot advance the pool
again. The existing three tournament traffic regressions now additionally
require same-activation traffic motion before GO. Target tests pending.

Runtime observer updated to compare the actual retained strut-rate field:
dt0 retains the prior derivative even after previous_compression=current.
Deep-drop native/extra-spring controls now compare complete telemetry futures,
not a five-centimeter minimum-height separation invalidated by bump impulses.
Synthetic ramp witness uses the same prepare_grid placement as production.
Unbraked drop requires native supported rest (positive support), not an exact
four-wheel pose already contradicted by47AA40. Geometry penetration and
braked-drop speed/spin/rest assertions remain. Remaining targeted failures
are old derived hashes; production gates and updated replay comparisons still
pending. Do not transfer a provisional r91 build.

## Fresh complete-input replay and validation start

After restoring stage3 traffic, root-run-port-drive-r1.py produced
port-drive-r91-native-dt-r3 with fresh current libraries. At22s its human speed
is173.604km/h versus recorded172.927; at10s123.683 versus122.900. Previously
the production replay stopped after an unrecorded taxi collision; pre-r90
component correction was about159km/h. These are local diagnostic observations,
not full original-world parity: initial world RNG/pool lifetime and PC analog
reconstruction remain different. The accepted-control/native-body diagnostic
is a separate experiment. Do not conflate these or claim complete parity.

All26 corrected mass-writer-based bump rows pass. New runtime/gear/track
physical checks and three traffic path regressions pass, except a derived
synthetic upgraded replay pin from an old linked executable; fresh full build
moved that pin again and it is corrected separately. Captured native rivals
101/102/100 all have actor28=1 and physics1 vtable6C8364; recorded physics1
calls include all rivals. Do not convert drag rivals to selector3. A traffic
path test may accept a native465D00 totalled latch, while no-traffic route
cases retain the strict three-finish witness and traffic cases still require
successful paths plus terminal states/no stuck racers. No arbitrary survival
or collision-strength adjustment was introduced to hide a legitimate latch.

Default/Release full gate runner root-gates-r1.py is starting with a fresh
root-source-freeze-r1.json. No production source/pin mutation while it runs.
No console or Git/board changes. Hardware and independent review remain held.

## Superseded first full gate and regenerated fixtures

root-gates-r1 configured/built Default and Release successfully, then full
CTest found the stationary20m airborne-log fixture now lands during3s grid
preparation, and old event1 input ends one tick before the new native-model
finish. The first pass was explicitly CANCELLED, not passed. psutil7.2.2 was
installed under the owner's tooling authorization to identify and stop only
that runner's ctest descendants. PIDs, start times, commands and actions are
in root-cancel-superseded-gates-r1.json; exact identities rechecked before each
stop. Both incomplete ctest commands returned15. Root caller exited1.

Airborne telemetry fixture now supplies speed1 to use the documented moving
spawn path, bypassing loading-time settling; targeted test passes. The stock
regression was successfully regenerated with stock/upgraded step counts pinned in tests, both
full two-lap finishes/no resets, retail476 files unchanged. Inputs and header
input hashes updated with backups in before-recordings-r1. These are port
regression recordings, not replacements for the owner's original capture.
Stock capture lap boundaries are recorded in the capture receipt. Capture matrix is
running against one fixed executable; do not rebuild it or update the Python
frame constants until that matrix completes. Derived frame/state pins and
retail car-drive pins remain to regenerate; then run new complete gates.
The native bump-debug734540 flag has only two direct code references (reads);
normal default0 remains the supported impulse context.

## Final candidate freeze and pending acceptance

Capture matrix completed all16 jobs. Every image/state/trajectory file hash
was checked against its receipt, and every presentation mode has the same
physical sequence/trajectory. All captured finite trajectory rows have positive
support, the existing upright/alignment criteria and forward progress. Derived
pins refreshed; all20 retail static preview frames unchanged, while all20
physical drive regression hashes changed. Stock/upgrade recording identity
and ROMFS guards updated. Upgraded capture pins live in race_run.h/race_run_tests.cpp and the capture
receipt; no original recording was modified.
Default/Release drive-demo streams and11 document-owned retail frames match
bytewise; four late camera frames changed and are updated with reason.

root-gates-r2.py is now running required configure/build/full CTest for Default
and Release from root-source-freeze-r2.json. SDK runner is independently
building build-switch-fidelity-r91-r2 from the same freeze. No source or pin
mutation during these gates. Older partial/cancelled runs are not acceptance.
Hardware, independent review, publication and transfer remain unverified/held.

## Source3 expected-metadata completion and preserved evidence

At ea4d472 (local r91 modified source; root-source-freeze-r2.json), the full
Default and Release gates completed with failures. Exact command/results are
root-build-validation-r2.json and root-build-release-validation-r2.json.
They are NOT passes. Both configurations produced the same regenerated
race sequence and states stream. Missing expected lap/timing values, one
car sequence, smoke/filter images and circuit fixtures caused the failures.
No engine/gear/inertia/difficulty coefficients were changed to satisfy them.
The comparison CPU renderer is our native-top-level reference implementation,
not a rendering capture from original speed.exe.

Source3 regenerates those regression expectations from fresh outputs.
root-filter-reference-measure-r3.json records the measurement-only scratch
copy with exactly the old pose/image equality checks bypassed; it is NOT gate
credit. Production checks retain both guards, original diagnostic input hash,
exclusive writer and wrong-input refusal. The normal four-target preflight
then passes with the updated expectations: root-final-preflight-r3.json.

The circuit five-metre drop now lands on restored588AB0 bump stops before
its chassis reaches the road. The test no longer incorrectly requires hull
penetration for that landing: it strictly requires zero hull contacts,
impulses, penetration and resets, plus airborne-to-four-road-wheel support.
Both real wall-impact fixtures retain their expected polygon/impulse checks.
Full future-state pins remain. The old source is preserved in
before-circuit-contract-r3.py. Other circuit telemetry pins are refreshed.

An exploratory actor88C read is corrected in root-native-rival-contexts-r2.json:
the native field is one BYTE, not a DWORD. Upper bytes in r1 include padding;
all captured flag bytes are zero. r1 must not be used as assisted-owner evidence.
This agrees with the ordinary owner/strength context used in this replay.
No production policy change was made from that misdecoded diagnostic.

Original bump-operation evidence remains bump-native-r2/receipt.json and its
actual584B10 mass-writer prefix. Native rest refresh negative control remains
root-rest-negative-r1.json/log. r1 mass-writer fixture and partial/cancelled
runs remain explicitly superseded, not reused as confirmation.

Source3 complete gates and SDK packaging are the next acceptance step.
Hold source/pins unchanged while they run. Switch runtime, independent review,
original whole-world/AI equality, transfers and publication remain unverified
or held. Preserve r90 and source2 archives; build local r91 only after gates.

## Completed source3 gates and local candidate

At ea4d472 (local r91 dirty source frozen by root-source-freeze-r3.json),
Default configure/build/full CTest completed327/327 and Release332/332,
including the retail fixture and Release retail-full checks, without any
skipped retail test. SDK configure/build/shader/NRO checks completed14/14.
These are historical gate results for this exact freeze, not whole-original-
world or hardware acceptance. Exact commands, exit codes and logs:
root-build-validation-r3.json, root-build-release-validation-r3.json,
root-switch-validation-r91-r3.json. Earlier failed/cancelled gates remain
preserved, not counted as passes. Frozen source hashes still match.

root-current-replay-reproduction-r3.json proves final production replay
r4 is byte-identical to r3 on the reconstructed recorded input/native dt.
root-rivals-world-comparison-r3.json joins opponents by car/context, avoiding
linked-list actor order. Native/port early speed samples are close; later
independently initialized traffic changes braking/contact outcomes. The
source observation is not a native full-state replay and must not be used
as proof that opponent traffic decisions or final race outcomes match.
root-world-speed-plot-r3.json/png preserve the raw sampled four-car comparison.
No timing/torque/inertia/difficulty values were fitted to the race result.

root-ascii-final-r3.json records Bash HEAD scans, BOM positive control,
working source bytes and forbidden-mode checks; HEAD/index remain unchanged.
Local packaging helper root-package-r91-r3.py refuses to copy the NRO unless
all full gate summaries and source hashes match. Output is out/nfsu-r91:
NRO, TEST-NOTES.txt and manifest.json. The manifest links the exact source
archive and gate records in build/fidelity-r91. No send/publication/commit,
peer review or hardware confirmation is implied by local packaging.
