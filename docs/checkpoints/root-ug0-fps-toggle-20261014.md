# Temporary FPS toggle for combined UG0 build

Owner requested L3+R3 to toggle the existing FPS counter and graph before
sending the combined build. Solo work on codex/draw-distance-original from
a7e883ddf21541783e23c2f4e6e29af74c935e1c, in the existing owned tree.

- Toggle is sampled once per presentation loop, including countdown/pause.
- Both stick clicks must be released before another toggle. Holding or
  releasing only one stick cannot toggle repeatedly or activate rear view.
- R3 alone retains rear view. The chord filters only stick-click bits from
  live driving input; axes, throttle, nitrous and other buttons are retained.
- Existing 90-interval FPS/frametime graph is reused. Toggle clears history
  and its previous timestamp, excluding the hidden period from measurements.
- Overlay is off by default in the built NRO; the existing CLI override stays.
- No chord in panel/no-HUD or AB measurements; recorded input remains intact.
- No GPU queries, readbacks or additional per-frame logging were added.

Validation, evidence under build/ug0-fps-toggle-r6:

- cmake --build build --target nfsu_race_hud_tests
  nfsu_race_minimap_hud_tests -j 4: PASS after fixing a test enum spelling.
  Original failed compile retained in native-build.log. Final HUD rebuild
  and test-placement correction in native-build-final.log.
- ctest --test-dir build -R
  '^(nfsu_race_hud_tests|nfsu_race_minimap_hud_tests)$' --output-on-failure:
  2/2 PASS, hud-tests-final.log. Includes both press orders, held chord,
  partial/full release, rear-view preservation, disconnect and HUD capacity.
- SDK configure tag ug0-fps-toggle-r6 / nfsu_switch_race_nro build: PASS.
- ctest --test-dir build-switch -R
  '^nfsu_switch_race_(nro|embedded_shaders)$' --output-on-failure:
  2/2 PASS, sdk-package.log. Actual build default FPS overlay is OFF.
- ASCII byte/path and git diff --check validation performed before commit.

NRO and exact local source identity are sealed in package.json in that
evidence directory. Includes r5 graphics optimization plus earlier held
traffic/lights fixes. No upload, push, merge or publication in this task.
Hardware toggle, rear view and combined visual/performance checks are pending.
Next action: owner-requested Netloader deployment with fresh readiness.
Release cleanup backlog: remove this temporary L3+R3 diagnostic shortcut
before the public release, as the owner requested.
