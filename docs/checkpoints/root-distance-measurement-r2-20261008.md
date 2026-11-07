# Complete free-drive frame measurements

Assignment: owner requires measured slowdowns, not subjective FPS estimates.
Parent candidate: a4a6109 (distance-r1). Same rendering and gameplay policies.

The previous normal nxlink log records only every 120th frame. Its 71 driving
observations contain 25.24 and 26.86 ms frames, but cannot establish whole-run
percentiles, average FPS, total slow counts or a complete maximum.
run1-sparse-observations.json records the log identity and these limits.

The existing all-frame accumulator was discarded when free drive returned to
menu. Emit its final summary for both free drive and race, including failure
exits. Add strict counters above 1000/60 and 2000/60 ms while retaining the
25 ms jitter-tolerant counter. Also accumulate consecutive presentation-return
intervals, including loop/logging overhead, with the same driving-only windows.
Do not bridge countdown, pause or finish; reset on resume or resolution change.
These intervals are not GPU timestamps or physical display scanout measurements.
Exact 16.666667 ms exceedances include timer jitter and are not missed-vblank counts.
P50/P95/P99 use existing 0.25 ms histogram upper edges. Print actual clock readings
and validity flags at run exit. No GPU profiling instrumentation is enabled.

Validation: focused native nfsu_race_frame_stats_tests 1/1 PASS; standalone
-O3 -DNDEBUG test PASS, including reset and exact threshold edges; real Switch
SDK build and shader/package CTest 49/49 PASS (switch-build-r2.log).
Full Default/Release matrices were not rerun; inherited baseline limits remain.
Artifacts and exact identity: build-artifacts/draw-distance-20261008-root/manifest-r2.json.
Next: send after fresh Netloader readiness; owner drives a lap and returns with
Plus. Read RUN_STATS from nxlink-r2.log. Hardware measurements still pending.
