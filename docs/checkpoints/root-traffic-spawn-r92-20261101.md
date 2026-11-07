# r92 traffic placement and direction admission

CHECKPOINT
role: worker
agent: Codex root solo
assignment: Owner reports traffic facing/stopping against each other in the
  first event9 drag-tournament race; inspect placement and travel direction.
phase: source2 validated; local r92 candidate prepared; hardware pending
branch: codex/draw-distance-original; Git/index/ref/board/publication HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: r91 source3 and delivered NRO archived; baseline source verified unchanged
worktree: build/day20261008-root-combined
saved: local only; build/fidelity-r92, original console log snapshot retained
verified: r91 driving run finished first; presentation summary in original log.
  Actual4420B0 gate and5999C0/599B20 caller order recovered from speed.exe.
decisions: Source point+84 bits25/26 gate forward/backward traffic spawning.
  Existing header lane availability alone admits racer-only points. Restore
  the original point gate without changing body/handling/control coefficients.
remaining: Targeted and full mandatory validation, fresh production replay,
  local NRO, independent review/hardware acceptance unverified.
next: Build target probes and check admitted roads/directions/blocked pairs.
blockers: none for source/test work
ownership: root solo; no new console send authorized by this observation request

## Evidence and rejected hypotheses

Native helpers/decompilation are root-native-004420B0.txt and callers
root-native-005999C0.txt/root-native-00599B20.txt. The original placement helper
442590 converts reverse lateral offsets to the forward basis used by4422B0;
456E90 correctly uses the backward basis for reverse lane selection. Do NOT
blindly flip the reverse basis in lane_point: that early hypothesis was not
accepted as a fix. Header RouteKind also does not distinguish the main-road
traffic corridor from racer-only geometry here; no kind filter was added.

Original captured traffic paths in world-20261031T161241Z-8209ab17 use the
main traffic corridor. The unmodified production replay admitted racer-route5
because only header lane_enabled was tested. On that route, captured raw
point spawn bits deny both directions. Main traffic roads retain the actual
admission bits. root-road-flags-r1.log is the metadata witness.

The observation-only replay preserves the baseline nonblank state output and
sequence/step hashes. An initial comparison wrongly included CRCRLF-created
blank lines; it is not evidence of a changed simulation. Fixed60 artificial
round2 input reproduces an opposing panelvan/sedan queue on racer-route9,
with both parked under the finite obstacle corridor. This is a related
reproduction, NOT the owner's observed first-round coordinates. Its late
44-second planner refusal is a failed diagnostic run, not a gate or a pass;
the failure predates production edits. Do not hide or count it as acceptance.

The finite corridor remains a documented adapter; original strategies8/3/18
and complete teleporter/admission/RNG lifecycle are not restored by one gate.
Current fix only authenticates direction-specific spawn-point permission.
No arbitrary route/lane blacklist, coefficient fitting or forced despawn.

## Additional authenticated drag admission rules

5999C0 calls599B20, which scans table slots0..98 rather than every loaded
route (the ordinary teleporter is a different producer).599C40 additionally
admits only points with ten authored points remaining in the travel direction.
Native instructions include strict forward index<count-10 and backward
index>=10. The native forward return only defines AL: its upper EAX bytes
are not a boolean. The fixture deliberately compares AL, not padding bits.

The existing spacing verifier now also executes real4420B0 for all high flag
bytes, individual lower bits/complements and both normalized directions, and
real599C40/4420E0/432E00 at boundary counts/indices without semantic hooks.
The existing retail CTest regenerates/compares these fixture arrays too.
RaceSession passes actual immutable drag mode to the traffic owner; there is
no event9-only list, no arbitrary forbidden-road list and no motion retuning.

Targeted pre-mode-patch integration checks passed all three tournament traffic
cases. The missing-point-gate negative control triggered the intended test
assertion, not a crash. Fixed60 observation runs preserve baseline simulation
for a nonblank-line comparison. After point gating, no stopped close pair
remains in the reproduced artificial round2 queue. Its late unrelated planner
refusal still occurs and is explicitly NOT a completed driving acceptance.
Native drag table/margin checks and full gates are the next validation step.

## Final source candidate ready for complete gates

Both targeted test passes are preserved separately. The second includes the
native endpoint function plus current drag-mode propagation. All three direct
production tournament-with-traffic checks pass with actual new activation
point flags, native road-table range and endpoint margin asserted. The native
test/tool does not modify any retail input. Before edit, the negative control
failed the intended forbidden-activation assertion, not a crash.

Root-source-freeze-r1.json now anchors complete Default/Release gates and
SDK build/shader/NRO checks. No source/pin edits while these run. The r91 NRO,
source archive and console log remain untouched. Traffic-only fixes: no
player model, powertrain, steering, tuning, audio or renderer changes.
A local r92 package may be assembled after all gates pass; no send yet.

## Required gate1 failure: existing single-racer diagnostic UB

Source1 Default completed all tests; Release failed full-career stage97 with
exit3221225477 and no failure message. This is NOT a pass. The isolated normal
retry passed but printed impossible statuses for absent actors. A fresh
build-r92-checked with _GLIBCXX_ASSERTIONS deterministically failed the actual
RaceRacerTiming vector subscript; GDB backtrace places the access in main,
not a traffic function. tests/career_races_tests.cpp always read four statuses
although time-trial stage97 has one timing entry. Its long periodic diagnostic
also assumed three physical rival sessions. Both loops now use actual counts;
status access also uses at(). No game physics/traffic change was made for this.
Source1 negative assertion/GDB and source2 short/long checked confirmations are
root-checked-event97-r1.json/log, root-checked-event97-gdb-r1.log and
root-checked-event97-r2.json. Full source1 gate receipts remain preserved.
The original diagnostic is backed up in before-r2/tests/career_races_tests.cpp.

Full Default/Release and SDK source2 gates must now run again from
root-source-freeze-r2.json. The production changes remain those of source1;
only the confirmed test UB was corrected. No test verdict or output hash was
loosened. Do not package source1 as fully accepted or count its failed matrix.

## Completed source2 acceptance and local package

At ea4d472 with local r92 source frozen by root-source-freeze-r2.json,
required configure/build/plain full CTest passed Default327/327 and
Release332/332, retail fixtures included and no skipped retail test. SDK
configure/build/shader/NRO checks passed14/14. Exact commands/results are
root-build-validation-r2.json, root-build-release-validation-r2.json and
root-switch-validation-r92-r2.json. Source1 failed Release is preserved and
not counted as a pass. Source2 differs from source1 only in the confirmed
single-car test diagnostic fix. No production fix was added for that crash.

root-driving-source-preserved-r2.json proves the r91 vehicle, player powertrain,
ordinary driver/planner and race-run source hashes remain unchanged. This
says nothing about identical later encounters or random streams. Spawn-point
flags and drag admission are the game changes; all10 changed source paths
relative to r91 are in root-source-scope-r2.json. The source remains dirty
and local-only under the owner's Git/index/ref/board/publication holds.

root-final-traffic-observation-r1.json records the fixed60 observation cases:
first/second rounds completed; no stopped opposing pair was observed. The
related third-round input's pre-existing late planner refusal remains an
explicit failed-diagnostic boundary, not a completed-run pass. Native scalar
fixtures, real tournament activation checks and the missing-gate negative
assertion are the admitted code evidence, not full-world equivalence.

Source2 local packaging is out/nfsu-r92: NRO, TEST-NOTES.txt and manifest.json.
The guarded helper refuses packaging unless all complete gates and source
hashes match. Exact source archive is root-source-r92-r2.zip; r91 and its
console log are preserved. No r92 console delivery, independent review or
hardware confirmation has occurred. Original traffic strategies8/3/18 and
whole teleporter/RNG/path geometry remain separately documented limits.
Next: owner-requested console check of traffic placement in the first drag.
