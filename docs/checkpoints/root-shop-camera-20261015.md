# Starter browser camera, 2026-10-15

Owner requested original camera movement before sending the combined menu build.
Solo scope on codex/draw-distance-original, based on
55b45a29c45a49c166bbca7902c7c5c04922df03. No remote push or merge in this batch.

## Recovery and implementation

Original authenticated speed.exe SHA256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Owner shop video is the reference identified in root-shop-switch-20261015.md.

The previous menu-live-r1 used the main-menu saved camera in the starter shop.
It now uses the browser constructor pose, a preparation turn before departure,
the outgoing eased camera transition and incoming car-target tracking. Incoming
completion restores the shop view. The camera state follows constructor486C93,
setters487CC0/487DA0/487E20, interpolation4870C0 and view builder487680.
The existing generic camera/scene renderer receives the explicit view. Main-menu
saved camera is unchanged. Floor/cube reflections and lamp sprites consume the
same moving camera; the browser also uses its original45-degree FOV publication.
There are no extra scene uploads or render passes when switching cars.

Native arithmetic evidence is reproducible with:
python tools/frontend_browser_camera_native.py --retail-root <retail> --output <fresh-json>
It executes original constructor/setters/update/view conversion without camera
arithmetic stubs. OS/render submission is excluded. Sample car displacement is
explicit caller input, not a claim of whole-game motion capture. Fixtures in
frontend_browser_camera_samples.inc cover preparation, departure, arrival and
return. Tests compare scalar state, view basis and translation, not screenshots
against themselves. This does not implement user-controlled shop orbit input or
claim all frontend camera modes. Rapid selections keep the preceding batch's
coalescing policy, not complete original input-state parity.

## Validation and delivery

From the actual owned workspace, with the configured retail root and native
runtime on PATH:
- cmake --build build --target nfsu_career_tests -j 8: PASS.
- cmake --build build --target nfsu_frontend_tests -j 8: PASS.
- build/nfsu_frontend_tests.exe: PASS, includes all25 native camera samples.
- build/nfsu_career_tests.exe <retail> build/menu-camera-r2/browser browser:
  PASS, all five cars and rapid selection keep resident uploads/draw bounds.
  Departure, arrival and final frames inspected in browser/camera-strip.jpg.
- ctest --test-dir build -R '^(nfsu_frontend_tests|nfsu_frontend_retail|nfsu_career_retail)$' --output-on-failure:
  PASS5/5, including readonly retail fixtures.
- cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=menu-camera-r2: PASS.
- cmake --build build-switch --target nfsu_switch_race_nro -j8: PASS.
- ctest --test-dir build-switch -R '^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders|nfsu_switch_gpu_sample_contract)$' --output-on-failure:
  PASS3/3. Targeted checks only, per owner instruction.

Logs and immutable NRO are in build/menu-camera-r2; delivery.json records exact
source and binary identities. Hardware visual/music/memory verification remains
pending. Next: confirm current Netloader readiness, send this exact NRO and
capture the console log while the owner checks repeated car selection and
Underground welcome entry/cancellation. Prior no-upload instruction is superseded
only by the owner's new intent to send after this camera change; readiness must
still be established. No console acceptance is inferred from SDK packaging.
