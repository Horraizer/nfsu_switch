# Engine sound restoration, 2026-10-21

CHECKPOINT
role: worker
agent: Codex root solo owner integration
assignment: Owner requested Ghidra connection and remaining r23 fidelity items;
  engine sound first. No console send authorized yet.
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local-only research in build/engine-audio-r24; r23 artifact unchanged.
verified: Separate GhidraMCP backend on loopback8091 imported an authenticated
  speed.exe copy. Analysis completed with10043 functions. Actual MCP calls
  and decompilations saved in root-ghidra-engine-r*.json. The bridge's first
  run_analysis request timed out while analysis continued; not counted as a
  completed analysis until analysis_status confirmed it.
decisions: Native523C30 has throttle/brake gate timers and torque normalization;
  523B30/527180 shape load/RPM through curves, and523E30 advances shift states.
  Important correction: player subclass5234C0 overwrites the generic maximum
  torque normalization with160. Do not substitute the raw curve maximum.
remaining: Recover parent update order, complete native sequence witnesses,
  implement production adapter, validate host/SDK, and finish other owner items.
next: Trace runtime scheduler and gear-event/curve transitions before editing.
blockers: none for local research; hardware listening remains required.
ownership: root active; no delegation, publication or merge. Preserve staged
  GLM integration and existing MERGE_HEAD. Do not overwrite sealed r23.

## First implementation boundary

EngineEnvelope ports the load latch, ten-sample filter, RPM/load shift curves
and original frame order. Bounded original math matched bit-for-bit across
all seven states; native-r6 includes the selected balance/accent producer.
Focused host audio checks passed before the following input correction.
No console or full required-gate claim yet.

A follow-up traced the INPUT producer instead of accepting its old name:
478FA9..478FB8 publishes gearbox+70 to CarState+3AC (biased actor+3EC), not
crankshaft Nm.46E020 accumulates the initial clutch torque plus coupled
corrections there. Production now preserves that output-only accumulator;
legacy replay bytes/hashes exclude it. The player divisor is literally160,
not a license to multiply the input by1000. The earlier Nm wording was wrong
and is corrected here. Native-r7 will exercise the accumulator/export too.

The player gain method5235A0 also applies the authored +3C channel balance
and the +20 upshift accent curve. Those are connected before the ABK graphs;
category/camera/spatial gains and scripted prerace revving remain separate.
The first new accent test caught a premature reset during successive shifts;
removing that reset restored original behavior. Failed runs remain in logs.

## Parameter producer precision correction

The first full required r8 Default run was cancelled before completion, not
accepted as a pass. Final parameter inspection found a concrete old defect:
EngineAudioGraph multiplied bank RPM/load scales in double.5236D0 on the
007F game-thread precision multiplies in binary32 before truncation; full
load often became1023 instead of1024 in the port. A direct original producer
probe found mismatches on the current control sequences. The native sequence
fixture now includes5236D0 parameters, and production uses the same scalar
conversion. The graph differential tool explicitly selects game-producer
precision while retaining the historical audio312 harness default for old
research. All required host gates must restart after this correction.

The cancelled runner and its descendants were identified by exact command,
executable and creation time before stopping; the process receipt remains in
root-required-cancel-r8-processes.json. Retail inputs were never modified.

## Final engine control boundary before required matrix

Focused native sequences and actual retail PCM checks passed after the input,
balance/accent and parameter precision corrections.32 engine/exhaust banks
also matched original Player controls with game-producer precision and the
bank callback harness's separate FP environment (banks-r1/report.json).
Native-r11 additionally covers the complete balance lookup phase range.

Next frozen required runner is root-required-host-r11.py, covering configure,
build and full CTest in Default and Release. SDK follows on the same runtime
sources. r8 remains cancelled, not a pass. There is no new console result.
Other owner items are still being researched; original knockout investigation
found permanent ghost flag8A8 as well as timer884/888 and the minimap gate.
Do not equate that with deleting the actor object without tracing consumers.

## Required matrix completed; preserved r24 boundary

At ea4d472 with the dirty source identities in
build/engine-audio-r24/root-source-receipt-r11.json, required Default and
Release configure/build/full CTest completed successfully. Both plain CTest
runs include the configured retail fixture and no skipped test. Exact commands,
exit codes and log hashes are in root-required-host-r11.json. The SDK script
root-sdk-r11.sh and its log confirm all selected package/shader/source checks.
Observed process completion was exit0 for both runners. The independent native
fixture verification native-r12 passed without rewriting test fixtures.

The source-facing test is nfsu_race_audio_tests; its original sequence and
balance fixtures are generated and verified by tools/engine_envelope_native.py.
Actual bank controls are reproduced by tools/engine_audio_native_check.py.
This is local implementation evidence, not an independent review or console
listening acceptance. Scripted prerace revving, camera/category/spatial gain
ownership and the port's final mastering/mixer remain explicit boundaries.

Owner has added acceleration, tachometer and traffic contacts as the next
combined scope. No runtime source changed while the required matrix ran.
Preserve the separate engine-native-r24 binary before continuing that work;
sealed career-polish-r23 is unchanged. Do not send either without the owner's
resumed console instruction. No commit, push, merge or publication performed.
