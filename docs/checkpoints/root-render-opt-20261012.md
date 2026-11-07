# CPU draw preparation optimization, 2026-10-12

Owner-directed solo implementation; local only, no deployment or publication.
Parent: 5522e1a405eb8acefed4dc59eca9b69e8162f5de.
Owned tree: build/day20261008-root-combined.

## Changes

Reuse live cubemap command slots and their string storage across frames.
Reuse RaceGpuFrame in the Switch presentation loop to retain that storage.
Stop reflection bounds classification once the intersected rejection mask
is empty; omit projection for explicitly uncullled template construction.
All six cubemap faces still update every frame. Effect quality, resolution,
draw distance, physics and opponent behavior are unchanged by this patch.
A desktop-only command signature covers ordered draws, resources, pipelines,
placements, materials and main/reflection/cubemap frame uniforms.

## Verification

At parent plus this patch, PC Release, three shared UG0 scenes at 600, 2400
and 6000 ticks: all paired sequence hashes, two CPU images and three ordered
command signatures agree. Each preparation mean uses 120 warmed stationary
samples, not a whole-lap GPU or presentation measurement. Total parent stage
means (children excluded): 0.477933 to 0.359542 ms; 0.379742 to 0.333442 ms;
0.530292 to 0.413867 ms. Reduction: 24.77%, 12.19%, 21.95% respectively.
Evidence: build/render-opt-r1/paired.json, summary.json and paired logs.
Baseline executable contains the same host signature probe before production
changes. Fresh output paths and successful completion were checked.

Commands from owned tree, with native toolchain on PATH:

```
cmake --build build --target nfsu_intro_grid_viewer nfsu_track_render_tests nfsu_race_run_tests -j 4
ctest --test-dir build -R '^nfsu_(track_render|race_run)_tests$' --output-on-failure
build/nfsu_intro_grid_viewer <retail-root> <fresh-prefix> <ticks> profile
cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=perf-cpu-r1
cmake --build build-switch -j 4 --target nfsu_switch_race_nro
ctest --test-dir build-switch --output-on-failure -R '^(nfsu_switch_fma_census|nfsu_switch_race_nro|nfsu_switch_frontend_profile_contract)$'
```

Native build PASS and targeted CTest 2/2 PASS. Actual devkitA64 SDK build
PASS and targeted Switch CTest 3/3 PASS. Logs are in build/render-opt-r1.
Initial native compile failed after a misplaced reset edit; fixed before
measurements. Both failed build.log and successful build-r2.log are retained.
Full native/Release/retail matrices omitted per owner minimal-check direction.
SDK helper is root-relative, ASCII and ran from the actual workspace path.

## Delivery and remaining acceptance

perf-cpu-r1 includes the earlier contact optimization as well as this patch.
Artifact identity and exact final source commit are recorded in the local
build-artifacts/draw-distance-20261008-root/perf-cpu-r1-checkpoint.json.
FXAA remains enabled; MSAA and FPS overlay remain disabled.
No claim of Switch performance improvement or stable 60 FPS before hardware
measurement. Next: one owner-commanded same-route Switch comparison against
ui-standings-r1, keeping current gameplay settings for attribution.
Opponent difficulty and neutral throttle before GO remain separate pending
behavior fixes, recorded in root-contact-opt-20261012.md.
