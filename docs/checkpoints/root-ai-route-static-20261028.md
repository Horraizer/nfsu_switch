# Event8 static path-selection audit

CHECKPOINT
role: worker
agent: Codex root
updated_utc: 2026-10-28
assignment: Owner asks to inspect existing event8 data and trajectory code without new diagnostic material
phase: static audit complete; implementation remains open
branch: codex/draw-distance-original; index/publication HOLD remains active
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; existing accumulated integration, no fetch authorized
worktree: build/day20261008-root-combined
saved: local-only audit in build/root-ai-route-static-r60.py and build/ai-route-static-r60/root-report.json
ownership: Codex root; static review, no independent acceptance or production fix

## Existing owner evidence

The prior inventory overlooked build/pc-profile-sync-r1. It contains the
preserved original event8 archive and root-original-live-r1 / routes-r1 /
routes-r2 files. This corrects the earlier broad claim that no event8 data
was available. The two route logs have a constant original clock, so they
are retained planned-path snapshots and not a moving complete-lap trace.
The scalar log contains a short clock advance and does not restore that lap.
Input hashes were checked against root-capture-preserved-r1.json.

## Confirmed category mismatch

Original450210 reads the individual ride performance category at ride+424
and stores it in context+394 when establishing its ordinary descriptor.
The owner's retained event8 configs and path category bytes independently
show category3 for the ordinary paths of both Golf actors and the Focus.
This performance/path category is distinct from difficulty context.

IntroAiPlanner::load sets every context to category2. observe_human updates
the human context; drag lane handling updates drag contexts. There is no
equivalent ordinary-rival performance-category publication. The ordinary
path extension therefore indexes driving_offset[2], and speed publication
indexes speed[2], instead of these original actors' category3 columns.

The report compares the authenticated track1005 authored lane offsets and
speed bytes at the road/point references in the preserved original paths.
The columns differ at every recorded active point of all three rivals.
This directly establishes a wrong input selection in production. Final
lap time and the contribution of other gaps are not established by it.

Source locations: src/intro_ai.cpp load context initialization, plan path
extension category and ordinary speed publication; src/ai_path.cpp lane
lookup; src/ai_action.cpp ai_action_publish_speed.

## Other concrete wiring gaps

The runtime sets path.rebuild=true on rival plans. Original45AE10 checks
the strategy virtual+1C and controller+4BC; its other branch copies the
published driver path, trims consumed points and extends the retained tail.
The port component implements this retained path branch, but the ordinary
runtime does not select it. This is distinct from cursor-cache refresh.

Recovered strategy and action-owner components remain disconnected from
IntroAiPlanner. Per-action lifecycle and speed restrictions cannot be
established merely by matching the path-candidate chooser in isolation.
The three port decisions also read a frozen path snapshot. The original
world/actor publication ordering needs separate integration verification.

Random junction differences across separate original runs are not by
themselves a defect. The fixed category and unconditional rebuild findings
are source-level differences; they are not inferred from visual preference.

## Validation and limits

Command: python build/root-ai-route-static-r60.py
Result: exit0; preserved input hashes and production source freeze checked;
native executable authenticated; original instructions disassembled only;
saved config/path categories compared with retail authored track columns.

No original or port game process was launched, attached or controlled.
No capture, recorder installation, C++ source change or NRO was produced.
The existing production source baseline and Git index stayed unchanged.
ASCII scans cover the new script, reports and checkpoint changes; the HEAD
tracked path/blob scans retain their empty/exit1 expected result.

Next: implement individual rival road category publication and the original
path/action lifecycle, keeping difficulty context separate. A code change
requires the repository's full C++ gates and later runtime acceptance.
