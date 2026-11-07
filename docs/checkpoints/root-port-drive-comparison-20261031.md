# Recorded original commands through the port runtime

## Scope and checkpoint

Owner requested comparing with the port drive, then specifically running the
port using the existing original recording. Solo, worker, local-only work in
build/day20261008-root-combined. Git/index/ref/board, independent review and
console-send holds remain. Existing game C++, CMake, SDK sources, r87 logs,
r88 archives and NRO remain unchanged. No retail write or new owner drive.

Source recording and component-only comparison are indexed by
root-world-recording-analysis-20261031.md. This checkpoint adds an actual CPU
RaceRun execution. Scratch runner/source/results live in
build/world-recorder-cap-r1. This is a measurement, not a gameplay patch,
review verdict, hardware test or claim that the full drag now agrees.

## Runtime replay

root-port-drive-r1.cpp links the existing production libraries. It loads the
real event9 round0 grid/course, Civic with the recorded engine-a/gearbox/tyres
levels, Hard2, three authored rivals and traffic. RaceRun::simulate/commit
run the planner, drivers, vehicle/track/pair/traffic simulation and recording.
No original output position or speed is written into the port's evolving car.

The PC trace contains analog gas; Switch ZR is digital. A scratch friend input
adapter binds recorded8A to a calibrated analog action without an extra floor.
Original4618B0 applies454F30 while neutral: the adapter reproduces that callback
before the ordinary port neutral control. Accepted gearbox request changes
become R pulses; A6/A7 press/release become left-axis direction. This checks
accepted requests, not the missing physical shift-button callback chain.

The first run resamples commands at the port's normal60Hz. Countdown duration
is aligned with the recorded prefix. A separate experiment supplies recorded
active physics intervals through the existing external-frame seam. Warm-up
remains at the port cadence; neither experiment restores the original's entire
private body/wheel/controller/history or RNG checkpoint. Initial poses and
warm-up output differ, so speed differences do not alone isolate a physics
instruction. Native-dt planner/outer-clock details are not a hardware mode.

Final standard run: port-drive-final-r1/input.txt, drive.log, comparison.json.
Active-interval experiment: port-drive-final-native-dt-r1 with the same files.
Source hashes, matched setup, repeat results and zero-gas negative control are
in port-drive-final-r1/validation.json. Actual state lines and final sequence
hash repeat identically. Removing gas changes the hash and leaves the player
at rest, proving that recorded input is consumed. Earlier port-drive-r1/r2
development failures and bridge experiments are preserved, not counted as
successful runs. The Easy experiment is not a matched baseline.

root-run-port-drive-r1.py builds/runs a fresh named output directory from the
real Unicode workspace. root-analyze-port-drive-r1.py compares game-time
observations. root-confirm-port-drive-r1.py reruns the same binary/input and
executes the negative control. root-plot-port-drive-r1.py renders early speed
and RPM comparisons before the lane command's downstream interactions.
Counts, times, differences and hashes belong to these result files rather
than duplicate documentation pins.

## Confirmed omitted setting

Original4B5DF0 reads the campaign difficulty from75F240 into78A3FC. It separately
reads event+148 (active event object+59E8) into78A40C. Original422410 then writes
table6B7C20[index] to the rival driver+1FC, and45FC90 clamps throttle by that
field. The table and every recorded rival driver agree on the event's limit.

The original memory tape contains Hard2 at both75F240/78A3FC and index0 at78A40C.
The port grid also reads event9 raw+148 as0, but RaceRivalPlan::throttle_cap
remains its default1; held-grid rival throttle is also uncapped. The observed
original cap is therefore missing in this production adapter. This is not
the difficulty enum and must not be implemented as a new Easy-only multiplier.
The temporary commentary that called78A40C the difficulty was wrong and was
corrected after inspecting both original globals and the builder. The
port-drive-easy-r1 experiment does not establish campaign parity.

Authenticated decompilation/xrefs are preserved under
build/profile-event9-pc-r2/root-native-004B5DF0.txt,
root-native-00422410.txt, root-native-0045FC90.txt and root-xrefs-0078A40C.json.
No production throttle-cap fix is included in this comparison task.

## Observed differences and remaining work

With the accepted owner requests replayed, player speed/RPM diverge before the
first lane command. An active-dt experiment does not remove the early gap.
Later collisions and stochastic rival shifts differ, so later offsets cannot
be assigned solely to acceleration or the throttle cap. Keep the initial
warm-up/body differences visible. The numeric path target matches proven in
the preceding checkpoint do not imply matching the evolving race trajectory.

Next implementation: carry event+148 through the runtime into retained rival
driver/grid throttle limits, authenticate all three table choices and run the
required production gates. Separately isolate the first launch divergence
using the saved input and pre/post engine/gearbox states; do not fit final
times or request another drive as a substitute for this available evidence.
Full original body/wheel checkpoint restoration needs additional captured
coverage before it can be claimed; the current input replay already works.

## Validation and custody

No required game build is invalidated: production source matches the sealed
r88 freeze. The scratch runner compiles and runs against those libraries.
Final Bash HEAD/working-tree ASCII and HEAD/index receipts and the authored
automation/path/NRO integrity receipt are rerun under world-recorder-cap-r1.
Source capture, saved logs and previous seals are preserved. Publication and
independent acceptance remain on hold. This checkpoint is local only.
