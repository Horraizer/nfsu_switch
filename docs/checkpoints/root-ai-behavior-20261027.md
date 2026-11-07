# Rival behavior investigation

CHECKPOINT
role: worker
agent: Codex root
assignment: Owner reports different rival paths and speed in event8 and drag
phase: investigating
branch: codex/draw-distance-original; publication/index HOLD remains active
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; accumulated local integration, no fetch authorized
worktree: build/day20261008-root-combined
saved: local only; build/root-ai-behavior-r59.py and build/ai-behavior-r59/root-report.json
ownership: Codex root; active investigation, not accepted AI parity

## Findings

The current IntroAiPlanner directly combines road/path planning, default
ordinary speed publication, catchup and its recovery adapter. It does not
call ai_strategy_choose, ai_strategy_blocking or AiActionRuntime. Searches
of production src files find those functions in their implementations only;
the runtime's owner step is called from that otherwise unconnected runtime.
AiActionRuntime itself currently constructs only action4 and action12, so
adding one instance is not a complete scheduler implementation.

The authenticated original constructor was executed again on the declared
synthetic state using controller293_strategy.strategy_case. It creates
action7 for the normal case and action8 for the special drag flag. This is
constructor evidence, not a driven race or proof that either default remains
selected throughout a race. A default action is not the complete scheduler.

Prior component matches therefore do not establish integrated action choice,
route selection, desired speed publication or physical trajectories. The
missing scheduler is a concrete implementation gap. Its contribution to the
owner's event8 speed discrepancy has not been measured; do not claim it is
the sole cause. Drag must be investigated through its own controller,
launch, shift and physical chain rather than blamed on ordinary catchup.

## Recording coverage

All session manifests in Documents/NFSU-Recordings were verified again.
Those recordings cover UG0, event1, event4 drag attempts and event3 drift.
Correction on 2026-10-28: this directory was not the complete evidence
inventory. Earlier event8 files exist under build/pc-profile-sync-r1,
including root-original-event8-capture-r1.zip and preserved route snapshots.
Their route logs contain a paused state rather than a moving complete lap;
the earlier scalar log also contains a short clock advance. The files are
useful for setup and published path inspection, although not a complete AI
controller replay. See root-ai-route-static-20261028.md for the resumed audit.

There is no real ai-* capture in the user's recording directory yet. The
new AI recorder's generated fixture validation is not a live original drive
and is not a comparison with IntroAiPlanner. Its controller462430 boundary
supports actor controller kind3; drag kind2 is explicitly outside that
controller boundary. Driver45E970 is a separate sampled boundary. Do not
describe the current tool as complete drag-controller capture.

Ghidra discovery returned no running instances during this investigation.
Original constructor evidence came from authenticated x86 execution through
the existing local harness, not a claimed Ghidra decompilation.

## Preservation and next step

The audit verified the complete r55 production source freeze with no changed
production file. The Git index hash stayed unchanged. No Git/index/ref,
board, retail file, installed recorder or game runtime mutation was made.

First map an accepted original ordinary-controller capture into the actual
port planner. Compare current action/strategy, selected road/path and target
speed before physics, retaining the original call order and input epoch.
Extend capture to the actual drag-controller boundary separately. Then fix
the first demonstrated divergence and repeat the differential comparison.
No arbitrary speed reduction or difficulty rebalance is an accepted fix.

Unverified: first divergent integrated frame in event8 or drag, whole-race
trajectory parity, hardware behavior, independent review. No C++ production
change was made and no C++ matrix is claimed by this read-only audit.
