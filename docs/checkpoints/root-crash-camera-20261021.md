# Crash camera integration checkpoint

CHECKPOINT
role: worker
agent: Codex root
assignment: Owner request to implement original crash camera, slow time and normal-view restoration.
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local only; r25 sealed source receipt verified before this assignment.
verified: Original request gates documented in docs/CRASH_CAMERA.md; r25 host and SDK gates passed before this task.
decisions: Solo work; preserve original requests, road-side selection, world-time scale and support/age return. No commit, push, merge, publication or console send.
remaining: Recover camera15/16 mover and original time integration; connect runtime; native and integration evidence; required host/SDK gates; hardware acceptance.
next: Trace4854E0 mover and420110 consumers; inspect race timing and route adapters.
blockers: none for local implementation; hardware acceptance follows a resumed owner instruction.
ownership: root active; no delegation.

## Mover and time-domain findings

Original447600 passes420110's scaled delta to43A8A0 and then420B40.585E00
reads the same73457C for world-tick accumulation. All simulation owners must
receive scaled dt; render cadence and420110's fallback timeout use unscaled dt.
At scale.4, selector1 uses two physics slices rather than four; selector3 and
props likewise recompute their slice counts from the supplied interval.

Case15 uses fixed road-side eye tracks and a constant20-degree FOV track;
case16 changes side and uses20-.25*i over32 FOV samples. Config vertical.2
and horizontal+/-.4 are perspective composition offsets.4854E0's randomized
+/-26..44 parameter is focus-distance spring input, not view roll. Earlier
verbal interpretation as a camera tilt was corrected after instruction review.
Native matrix translation proves the eye is the fixed roadside point; mover
+30 is a look-at target one unit away, not the rendered eye.

Read-only Ghidra evidence r9-r12 lives under build/engine-audio-r24; plaintext
and scratch probes are in build/crash-camera-r26. Scratch r1-r12 either
refused undeclared helpers or hit the lazy D3DX Win32 resolver, not passes.
r13/r14 execute the original constructor/update up to renderer47D070 with
explicit supplied renderer registration and original scalar D3DX binding.
They are diagnostic observations, not a portable/native comparison yet.

Initial crash_camera.h/.cpp and the CMake source entry are local WIP only.
They are not connected, built or accepted. Predictor tyre-point production,
road side/progress mapping, frame lifetime and native comparisons still need
completion. Existing ai_road_nearest already implements original5A6150 and
can be reused. The real human planner context is slot3; its current60-unit
horizon is too short for the camera's75-unit gate and must not be confused
with a valid camera road. Preserve the live driver path/policy when preparing
camera-only lookahead. No guessed layout or test pin is accepted.

Next: portable/native mover and road/predictor checks; finish source/context
adapters and scaled world dt across player, AI, traffic, props and clocks;
connect both apps; exercise impact/landing/timeout/reset/finish restoration.

## Portable kernel boundary (still WIP)

The initial module compiles as part of nfsu_race_session (target build r1-r3).
A native4854E0/485C00 vs portable view comparison covers both event variants
on translated synthetic world positions. First comparison exposed a required
binary32 store of eye+unit followed by world-eye subtraction. That store was
restored. Current view-native-r3 still has two small direction differences;
this is not a passing comparison or acceptance. Do not relax the tolerance
without identifying the original operation that explains the difference.
The native mover +30 is a target, and native view-matrix Y is negative the
portable up vector. The rendered eye is the selected stationary side point.

Additional exact findings:429E23 samples int((speed/3)*horizon), not a fixed
sample rate. Its already-airborne sentinel is indeed -count*horizon; retain
that odd source behavior. Predictor point production still needs matching
original wheel-matrix0 and body-to-world transformations. Camera constructor
start/end fields are actor percentage810 and a projected endpoint percent,
not compact-road distances. The source needs a progress_at callback. Camera
endpoint uses end-2 only when end-first>=5. Fields and callback are WIP in
crash_camera.h/.cpp; they are not connected to a live RaceSession yet.
## Runtime integration in progress

Local r26 now contains camera kernel, live RaceRun/desktop entry wiring and
common dt plumbing through selector1, selector3, traffic, props, steering,
world ticks and the chase view. No build or test verdict is implied: source
has only passed the targeted r9 host compile. Camera gates are opt-in for
legacy recorded diagnostics; both live apps request the port policy128.
The normal retail profile intensity is still not recovered and is not claimed.

The stationary-eye mover comparison view-native-r6 passed its supplied
case set without relaxing the2e-6 basis tolerance. Native observation exposed
Catmull-Rom interpolation5829E0/5828E0 even for duplicated eye/FOV points;
retaining its four weighted binary32 additions removed the two differences.
This is a bounded kernel comparison, not road, full-game or GPU acceptance.

Remaining: exact roadside constructor comparison, tyre-matrix predictor
production, explicit-frame publication tests, shared-clock integration tests,
return/reset/finish/rear-view evidence, full Default/Release/SDK gates, seal.
No console send, publication or Git index/history operation is authorized.

## Freeze before required gates

Runtime and targeted tests now compile. nfsu_crash_camera_tests passes the
actual queued head-on contact, scaled owner clock and three physical rivals,
route publication, return and particle-transition cases. Race particle
regression also passed with its unchanged constant-dt frame pins.
Original mover view-native-r6, predictor predict-native-r2 and corrected
road-native-matrix-report are passing bounded comparisons. Earlier failed
attempts remain in place and are not passes. Road construction now uses the
endpoint and original raw-width virtuals, rather than the initial guesses.

Next: mandatory host Default/Release configure, full builds and full CTest;
Switch build/package/source gates; current-source ASCII checks; frozen source
receipt and local NRO artifact. Console appearance, frequency/intensity
comparison and DoF remain explicitly unverified. Solo local work; no index,
history, remote or console-send mutation has been performed.

## Real-course acceptance correction

The first source freeze was rejected during real-course PC diagnostics, and
host required runr1 was cancelled (its partial results are not a gate pass).
SDK r1 passed for that earlier source only. Controlled real-course placement
exposed the terrain query order:42C6D0 raises the eye before querying terrain.
That order is fixed. The subsequent CPU frame still showed an occluded view.
Tracing457300 revealed it checks intersections with road-width strips through
457180, not the generic collision ray initially used by the port adapter.
That initial visibility adapter has been replaced by the original strip
predicate; its native differential and corrected rendered evidence follow.
Natural auto-driver probe found no camera request and is not activation
acceptance. The supplied-pose probe exercises the complete live planner,
three rivals and traffic but is not a natural collision trajectory. These
cases stay distinct from the real queued synthetic head-on unit case.

## Corrected rendered integration

road-visible-report-r1 passes the actual457300/457180 predicate over the
initialized native chain, including clear and blocked inputs, without a
replacement visibility callback. The real-course CPU frame then exposed a
second distinct adapter error: drive_render_camera consumes heading/pitch
words, while the initial implementation only changed forward/up vectors.
The cinematic now updates both, and nfsu_crash_camera_tests exercises the
renderer adapter and R3 hold/release on restoration.

The controlled-r4 PC image visibly shows the road-side shot looking at the
provided airborne car. Return-r2 proceeds to the restored chase frame and
normal world dt. The placement/trigger is explicitly supplied; real queue
activation is covered separately by the synthetic head-on test. Earlier
occluded frames and supplied-pose failures are retained as rejected evidence.
Native floor lookup first allows heights below query.z+1; the terrain adapter
now uses that admission bound. Local source is frozen again for required
runr2. No console send or publication has happened.

## Final time-consumer correction and freeze r3

A search of runtime time consumers found RaceResultStatistics still deriving
distance from display frequency. It now uses RaceSession::world_frame_seconds;
the targeted statistics fixture catches the former overcount during slow time.
The expanded crash camera test passes. Required host runr2 was cancelled for
this source correction and cannot be reported as a completed gate. SDK r2 is
also superseded. Final required runr3 uses eight independent CTest processes;
its Default/Release commands and results remain the acceptance record.

Final bounded comparisons: view-native-r7, predict-native-r3 (including
surface-launch trajectory cases), road-visible-report-r1 and the final
constructor comparison against road-visible-native-fixture. Controlled PC
return-r3 asserts the three selector3 owners and component clocks match the
human clock, then reaches ordinary chase and normal dt. Earlier natural run
found no request; the rendered activation evidence remains supplied placement.
No further runtime edits are planned while the required gates run.

## Local handoff

phase: awaiting-evidence
verified: Required runr3 completed Default and Release configure/build/full
CTest with no failed or skipped retail checks. SDK runr3 completed build and
all selected package/shader/source checks with exit0. Commands and verbatim
results are bound by hash in artifacts/VALIDATION.json and LOCAL_EVIDENCE.zip.
The final ASCII source/path check and Bash HEAD blob/path/mode check pass.
The exact runtime freeze is root-source-freeze-r3.zip with its source receipt;
final documentation changes do not alter that tested runtime.

artifact: build/crash-camera-r26/artifacts/nfsu-crash-camera-r26.nro
saved: Local source/archive/NRO only. No staging, commit, push, merge, review
approval, publication, Netloader transfer or retail-file write was performed.
remaining: Owner-authorized Switch comparison of contact activation, roadside
framing, slow time, support/timeout return and held/released R3. Original DoF
is outside the implemented renderer boundary documented in CRASH_CAMERA.md.
next: Use the sealed r26 NRO for the console comparison when the owner resumes
console transfer; preserve r25 for rollback and collect that launch's log.
ownership: Codex root, worker, solo; local implementation/gates finished,
hardware acceptance pending. No delegated worker or reviewer was used.
