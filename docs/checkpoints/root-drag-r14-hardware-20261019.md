# Drag launch r14 hardware follow-up, 2026-10-19

Supersedes the awaiting-hardware status in root-drag-idle-launch-20261019.md.
Owner reports having passed all difficulties and asks for log analysis.
Solo read-only runtime analysis; no code changes, rebuild, upload or delegation.

Build: drag-launch-r14, NRO SHA256 43e9263bb9c9a12a7a5c34784495594c28437fbe84be828f09748476d6c8e4e4.
Frozen hardware capture: build/drag-cold-r14/hardware-run1-final-r1.log,
SHA256 dabdbe2d6d9d5bdb8604553a0cf2e871d6479949abfaa8f599c441f394ed3f11, bytes 259297.
Parsed per-session evidence: hardware-analysis-r1.json in the same directory.
Live process18508 /UTC start2026-10-19T12:57:12.4455694Z was not stopped.
The capture ends with game exiting and socket error0x0 on poll; this is
connection closure after exit, not evidence of a game crash.

Four drag event4 sessions: Easy, Medium, Hard, Hard. Original context sets
are actually selected as0/1/0,2/2/0,3/3/2,3/3/2 respectively. All four human
cars accelerate through first and request second near16.2..16.4m/s. There
is no observed idle trap in these sessions. However all GO engine states
are warm (about4980..6948rpm); the specific800rpm/no-countdown-gas hardware
case was not exercised. That exact cold case remains PC-verified only.

Only final Hard session has a complete logged finish/result:41.64s,
position1/4, no resets, all rivals unfinished when the player finishes.
The other three end without a result dump; last periodic racing samples
are39.43s Easy,41.42s Medium,37.42s first Hard. Do not fabricate their final
places or times. Owner's statement of passing all difficulties is separate
owner-reported gameplay evidence, not a machine-recorded result on each tier.
Final Hard GO quality is-1/OverRev; four subsequent upshifts are.85/Perfect.
No requirement for a perfect launch to win is demonstrated by this run.

Four active-driving frame summaries have mean presentation intervals around
16.6655ms; frame-work maximums18.89,18.94,18.86,19.07ms, no frames above25ms.
Asynchronous GPU mean10.32..10.51ms, maximum12.34ms across these runs, no
invalid/dropped samples. No additional profiling or tests were requested/run.
This is history tied to the NRO/capture above, not a future performance pin.

Acceptance: owner reports all tiers playable/passed; final Hard victory is
independently confirmed in this hardware log. Retain r14 as tested candidate.
Exact original difficulty/timing parity is not established by winning, and
original startup/RNG order and broader campaign/audio/collision debts remain
open. No arbitrary AI slowdown, new tuning, merge or publication performed.
