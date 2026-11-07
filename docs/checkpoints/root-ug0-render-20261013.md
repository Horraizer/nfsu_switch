# UG0 render submission follow-up

CHECKPOINT
role: worker
agent: Codex root, owner-directed solo work
assignment: Optimize graphics while preserving appearance; combine with held r4
phase: awaiting-evidence
branch: codex/draw-distance-original
base: 7a86c9bb822317a00ea8b768e2baefa1bb371a99
worktree: runtime-relative build/day20261008-root-combined
saved: Local source commit; evidence in build/ug0-render-r5
ownership: root, active; no agents or reviewer dispatched

## Change

Split Deko bindings into independent shader, pipeline, vertex-layout, texture,
vertex-buffer and index-buffer decisions. A texture change no longer repeats
unchanged shader/pipeline/layout bindings. Buffer bindings repeat only when
the resource changes. Every pass, command-list chunk and diagnostic boundary
forces complete state again. Uniforms remain per draw, including the live
instance and material data. Main-pass environment sampler switches only when
the target changes between car cubemap and road reflection (or at pass reset).

No draw reordering, geometry removal, shader equation change, resolution,
reflection cadence, light strength or quality reduction. The six cube faces
still update every frame. Traffic/headlight changes from r4 remain included.

## Verification

Evidence paths below are relative to build/ug0-render-r5.

- cmake --build build --target nfsu_deko_draw_bindings_tests
  nfsu_intro_grid_viewer -j 4: PASS, native-build-r3.log. Existing unrelated
  viewer range-loop copy warning remains. native-build-r2.log preserves a test
  enum spelling compile failure; corrected before the final passing run.
- ctest --test-dir build -R
  '^(nfsu_deko_draw_bindings_tests|nfsu_deko_draw_order_tests|nfsu_race_effect_source_tests)$'
  --output-on-failure: 3/3 PASS, targeted-r2.log. State reconstruction traverses
  all manifest variants, individual state changes, untextured transitions,
  geometry switches and pass resets. Uniform payloads are not deduplicated.
- python tests/switch_frontend_sampler_tests.py --root .: PASS, sampler.log,
  including its three negative controls. Updated function delimiter only.
- build/nfsu_intro_grid_viewer.exe <retail root> <fresh prefix> 120 profile:
  PASS, grid-r2.log. Two CPU frame hashes, initial full draw signature and
  simulation sequence match the retained r4 run; parity.txt. CPU raster does
  not execute Deko command recording and cannot validate Switch GPU state.
- Same viewer at 1200 ticks: PASS, drive.log; live/moved and warmed draw
  signatures match. Actual race objects, rival/traffic cars and six reflection
  faces are included. The new host audit counts selected binding API calls,
  not measured GPU timing. It excludes uniforms/HUD/global state and assumes
  opaque-first submission with no streak-history pass.
- Historical audit on this source: start 15770 -> 4727 selected binding calls
  for 3080 draws; moving scenario 14412 -> 4602 for 3076 draws. Static-track
  portion applies only when its cached command lists are re-recorded. These
  reductions are neither total frame command percentages nor FPS gains.
- Real devkitA64/deko3d SDK configure, tag ug0-render-r5, target
  nfsu_switch_race_nro: PASS, sdk-configure.log / sdk-build.log.
- ctest --test-dir build-switch -R
  '^nfsu_switch_race_(nro|embedded_shaders)$' --output-on-failure:
  2/2 PASS, sdk-package.log.
- Modified-file/new-path/automation byte scan and runtime-path smoke: PASS.
  git diff --check: PASS. Canonical committed ASCII scan follows local commit.

Owner requested only necessary tests; full native matrices were not repeated.
No retail writes, push, merge, board publication or console upload performed.
Exact source/NRO identity is in package.json beside nfsu-ug0-render-r5.nro.

## Remaining

Actual Switch GPU appearance and frame-time delta must be checked on the same
portable route. This build also needs the held r4 traffic and light check.
No claim of stable 60 FPS or complete original traffic collision parity.
Next: upload this combined candidate when owner requests it and Netloader is
freshly available; collect the existing timing log during one combined run.
