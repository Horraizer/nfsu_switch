# Neutral throttle correction, 2026-10-12

Owner report: tachometer falls faster than the original after grid revving.
Parent: 0d43b11ddd09f16271a20a1db0925281b8fccd7f.
Solo work, minimal targeted validation per owner. Local-only checkpoint.

## Confirmed defect

Original human neutral processing at 461C57..461D25 limits throttle using
previous driver1DC and world dt: rise 1.0 per second, fall 1.5 per second.
Raw target above0.6 and published RPM below idle+1000 gives a0.6 tip-in floor.
The port previously sent raw throttle directly during countdown, immediately
removing engine torque after release. Full throttle now decays over about
2/3 second. This is processed throttle, not a fixed RPM decay duration.

Original RPM publication478FBB..47904B already has its falling-RPM limit in
player_engine_publish_rpm. HUD4A0F7F..4A1057 consumes actor3E0 through4A0EA0
and4F6A70. No additional needle smoothing was established or introduced.

The new drive_neutral_throttle runs once per world frame. Its result is held
across solver slices. Countdown and post-GO neutral use the same helper.
Previous applied input supplies history; no state schema changes. Direct
processed-input calls bypass it. No AI policy or first-gear throttle change.

## Evidence and validation

Authenticated PC1.4 USA speed.exe SHA256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
root-grid-engine-r2-compare.py executes original instructions461C57..461D2B,
461D95..461D9D and actual RPM reader424E20..424E49 with supplied memory.
196 boundary, tip-in and release cases at60/120Hz match binary32 exactly.
This is bounded controller parity, not whole-engine or trajectory parity.

Commands from owned tree with native runtime on PATH:

```
cmake --build build --target nfsu_race_session_tests nfsu_race_audio_tests nfsu_intro_grid_viewer -j 4
ctest --test-dir build -R '^nfsu_race_(session|audio)_tests$' --output-on-failure
build/nfsu_intro_grid_viewer <retail-root> <fresh-prefix> 600 grid-revs
cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=grid-engine-r2
cmake --build build-switch -j 4 --target nfsu_switch_race_nro
ctest --test-dir build-switch --output-on-failure -R '^(nfsu_switch_fma_census|nfsu_switch_race_nro|nfsu_switch_frontend_profile_contract)$'
```

Native build PASS; tests2/2 PASS. Actual retail UG0 grid-revs600 PASS,
including launch and four-car rendering. Real SDK build PASS; tests3/3 PASS.
Full matrices omitted per owner. Retail read-only. ASCII helpers executed
from actual workspace. No delegated review, push, merge or deployment.

Earlier failed probes are retained, not counted as passes: original reader
slice endpoint, generated C++ escaping and missing link dependencies. First
native compilation used back() on a C array; corrected to last index. First
session assertion compared release RPM with a later second blip; delayed
throttle invalidated that expectation. It now checks reduction from peak,
retaining stationary grid, cutoff, GO retention and launch checks.

Evidence under root build-artifacts/draw-distance-20261008-root:
- grid-engine-r2-compare-r4/report.json and comparison source/output.
- grid-engine-r2-build-final.log and grid-engine-r2-tests-final.log.
- grid-engine-r2-pc-600.log and fresh frame outputs.
- grid-engine-r2-checkpoint.json for exact source/NRO identity.
SDK logs in owned build/grid-engine-r2/.

Next: owner hardware comparison of throttle release and neutral revs.
Full tachometer/engine fidelity remains unaccepted. Other UG0 polish tasks
remain in root-ug0-polish-20261012.md; no other feature started here.
