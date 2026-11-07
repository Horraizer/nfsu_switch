# Drag lane-change source audit

role: worker
agent: Codex root
assignment: Owner asks why drag lane changes differ; source-first, no new drive
phase: analysis-complete; implementation and natural parity not claimed
branch: codex/draw-distance-original; Git/index/ref/board and review HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: local-only; this checkpoint and build/drag-lane-audit-r1
ownership: Codex root, active

No original game was launched or controlled. No delegation, production C++/
Switch change, NRO replacement, Git mutation or board write. Retail files are
read-only. The owner cannot drive now; the audit does not depend on a new drive.

## Confirmed source differences

### H1: player retained road horizon remains fixed

IntroAiPlanner::State::plan initializes both road horizons to a fixed value,
then computes speed-dependent values only for rival indices. Human drag uses
the fixed branch. Original drag vtable6C8D20 has update45D800 and virtual50
462390. Original458B90 computes the speed-dependent horizon through that
actual vtable. Original5A5730 uses it for extending an existing compact chain,
whereas an empty chain uses5A57F0. Do not conflate initial and retained horizons.

`root-horizon-native-r1.py` executes unchanged458B90/462390 with the real drag
vtable, file globals and supplied actor speeds. The only synthetic code stores
ST0 for observation; no semantic hooks. Its JSON records results, exact input
identity and call sites. This confirms a producer/caller mismatch. It does
not prove a particular lane-change duration: minimum cell count, path capacity
and retention may mask the changed horizon in individual frames.

### I1: lane-axis transitions have no input queue

The r87 DragPadInput/DriveButtonQueue retains the selected R/A transitions and
right-stick shift gesture. It intentionally takes other buttons and axes from
the latest frame. RaceRun then reduces the current calibrated left axis or
digital steering to one held direction. A left flick completed between world
consumptions can therefore disappear even when native HID history saw it.

The scratch C++ probe includes the actual production queue header. It submits
a left-axis flick and return to center before consumption, then a shift press
and release. The lane command is absent while the shift edge survives.
`root-queue-result-r1.json` pins that header and the actual command/output.
This proves the port transport gap, not the original PC device polling rate
or that every ordinary-duration lane command is lost.

Original4605E0 processes press/release events separately. With the default
held-control mode it updates500,504,508 and dirties4BC. Release selects neutral
mode while the exclusion interval can keep the destination across the old
lane until the new lane center is reached. A consumed short press is therefore
different from seeing only the final centered state.

### O1: lane selection and context refresh have a different order

Original45D800 handles lane/exclusion state against the retained context,
then calls462430 ->44B1B0, which runs the common context/path update.
IntroAiPlanner::prepare_context first updates/locates the road context, then
calls drag_lane_step. On unchanged straight-road geometry these may coincide;
at a route, lane-mask or context transition they need not consume the same
road state. This is a confirmed ordering difference. A concrete natural
divergence and its contribution to the owner's symptom are not yet measured.

## What already agrees in bounded checks

The original is not a fixed-duration lane animation: held modes walk reachable
lanes, release uses nearest permitted lane, and steering/vehicle physics follow
the published path. Current drag_lane_step contains these broad rules, the
travel-distance input gate and the lane-center exclusion release.

Commands run from the actual workspace:

- `python -B tools/drag_steering_native.py RETAIL`: exit0,
  `PASS original full drag driver/input distance gates, cases=30`.
- `build/nfsu_ai_driver_tests.exe`, toolchain on PATH: exit0,
  `4848 checks, 0 failures`.
- `python -B build/drag-lane-audit-r1/root-horizon-native-r1.py`: exit0;
  actual drag-vtable horizon witness saved, semantic_hooks empty.
- `python -B build/drag-lane-audit-r1/root-run-queue-probe-r1.py`: exit0;
  actual queue-header reproduction saved. Its supplied flick is not a natural
  PC or Switch run.

Ghidra evidence is from the already analyzed copied executable on port8092.
Use only the functions whose decompiled entry matches the requested entry;
the458E60 query returned containing458DC0 and is not evidence for any finding.
Original opcode/vtable dumps and authenticated executable hash are preserved
in this audit directory. No coefficients were retuned to fit an impression.

## Next work and limits

Before claiming fidelity, connect the original speed-dependent human retained
horizon, preserve lane input transitions and restore/prove the manual/common
context update order. Add sustained/release/fast-reversal and route-transition
comparisons spanning input, lane state, raw action path, driver path and wheel
word. Keep source-equivalent producer tests separate from natural vehicle
trajectory acceptance. No complete per-step human lane-change timeline or
whole lateral-physics parity is established by this audit.

Natural PC event9 data remains useful later to measure command latency,
crossing time, lateral speed and recovery, but is not needed to establish H1,
I1 or the code order in O1. Preserve sealed r87 artifacts and prior recorder
sources/receipts. No new NRO is prepared in this read-only audit.
