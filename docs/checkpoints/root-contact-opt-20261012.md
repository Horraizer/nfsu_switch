# Contact query optimization, 2026-10-12

Role: owner-directed solo implementation. No workers or review sessions.
Parent: 40c946865747a04393f2c56ec1a7871cfe802fb8.
Owned tree: build/day20261008-root-combined.
Publication: local only; no push, merge, NRO deployment or hardware run.

## Hardware baseline and decision

The owner completed ui-standings-r1 on Switch. The frozen log and report are
under build-artifacts/draw-distance-20261008-root, named
ui-standings-r1-hardware-run1-snapshot.log and
ui-standings-r1-hardware-run1.json. The sparse physics probes identified
vehicle track queries as the first CPU investigation, not planner policy.
Presentation-return intervals are not GPU timestamps or display scanout.

## Change

query_track_contact_fan shares the validated polygon list only when both
endpoints are strictly inside one quadtree leaf. The quadtree partitions are
exact powers of two; adjacent closed leaf squares cannot meet such a segment.
The original traversal runs for boundary/cross-cell rays. All scalar argument,
world, length and cell-limit checks remain. A bad cell/index invalidates the
cache. Dense lists spill without truncation. The cache lives only for one fan
against an immutable world; there is no cross-frame state or global cache.

VehicleTrackSession transforms the same bounded hull points in the same
order, batches the queries, then consumes contacts in the original order.
Triangle arithmetic, seam recovery, ties, impulses, controls and step cadence
are unchanged. The original scalar API is retained for wheel queries and as
the differential reference. Fan buffers are bounded by the constructor's
existing maximum of 256 hull points. No new per-ray heap allocation.

## Measurements and verification

At parent plus this patch, PC Release, same 6000-tick shared RaceRun fixture:
- Paired order: baseline, candidate, candidate, baseline. Mean physics time
  across the two runs each: 0.467670 ms to 0.314891 ms (32.67% less).
- All four complete sequence hashes and both rendered frame hashes agree.
- Diagnostic instrumentation found body queries dominate. Its source snapshots
  and log are saved in build/contact-opt-r1; instrumentation is removed from
  production. An origin-arithmetic cache experiment was slower and discarded.
- Fresh native targets: nfsu_intro_grid_viewer, nfsu_track_contact_tests,
  nfsu_race_session_tests built successfully.
- ctest --test-dir build -R '^nfsu_(track_contact|race_session)_tests$'
  --output-on-failure: 2/2 PASS.
- Fan checks compare every contact field bitwise with scalar results, including
  unchanged non-hit sentinels, shared edges, changing origins, zero length,
  nonfinite endpoints, caps, invalid worlds, dense spill and late bad indices.
- Evidence: build/contact-opt-r1/cells-ab.json, result.json, paired logs,
  final-build.log and final-tests.log. Baseline executable is retained there.
- Full native/retail matrices omitted per owner minimal development checks.
  Switch speedup and stack/runtime acceptance remain unmeasured. Do not
  extrapolate this PC percentage into Switch FPS or claim stable 60 FPS.

Reproduction from the owned tree, with native toolchain on PATH:

```
cmake --build build --target nfsu_intro_grid_viewer nfsu_track_contact_tests nfsu_race_session_tests -j 4
ctest --test-dir build -R "^nfsu_(track_contact|race_session)_tests$" --output-on-failure
build/nfsu_intro_grid_viewer <retail-root> build/contact-opt-r1/<fresh-prefix> 6000 profile
```

## Owner-reported behavior, separate from optimization

- Opponents feel stronger than the original highest difficulty. The event
  descriptor currently selects difficulty index 1. IntroAiPlanner consumes
  that context but leaves catchup_scale at 1; full adaptive setup/live strategy
  is not integrated. This is not proof of the reported cause or original Hard
  equivalence. Next: compare the event's original difficulty/catchup inputs
  and request speeds before changing any performance/grip constants.
- Neutral throttle before GO remains missing. Confirmed source cause:
  RaceSession::advance_impl's held-grid branch clears inputs/telemetry and
  skips engine updates along with body physics. Next: recover the original
  countdown engine/clutch/gear transition, retain fixed grid pose, drive RPM
  and sound from engine state, verify throttle/release and GO handoff on PC.
  This was already reported earlier; it is not a newly discovered request.

Next optimization acceptance: a later same-route Switch comparison of this
unchanged-behavior patch. Keep the two gameplay fixes separate from the timed
baseline so performance changes remain attributable. Owner need not run the
Switch during the current PC work.
