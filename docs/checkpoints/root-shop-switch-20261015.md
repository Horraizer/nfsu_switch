# Resident starter browser and welcome, 2026-10-15

Owner scope: remove the loading screen between starter cars and preserve the
live menu/music beneath Underground welcome. Solo implementation and targeted
validation per owner instruction. Console upload remains explicitly deferred.
Base: 0911289dcf8705199b619ddebaa2c07085512be1 (nos-traffic-r3).

## Reference and implementation

Starter shop reference: video_2026-10-13_08-22-28.mp4, SHA256
4d81b71a3450d205e450896aecb279364329edc331fe7066ec453e21554cf4d5

Welcome reference: video_2026-10-12_16-46-38.mp4, SHA256
D61D80B7531D24A7EB9FD9F80FDBFBE4F8BEBF011D983D544FFF34F9FCD4AFDD.
Fresh contact sheets and original-code evidence are in build/menu-live-r1 and
build/career-audit-20261013. Retail files remain read-only.

The bounded five-car starter roster is prepared before opening its renderer.
Garage and upload table remain resident; selecting a car changes draws only.
Original outgoing/incoming translation arithmetic was recovered from 4B01C0,
4B0459 and 4B0549, with bounded original x87 samples in native-motion.json.
Body, wheels, shadow and lamp sprites share that travel. Latest selection is
coalesced during departure. Original camera choreography and complete original
rapid-input policy are not claimed; the existing menu camera remains.
This is a starter browser, not a streaming solution for the full unlocked shop.

Welcome keeps music playing on entry and cancellation. Playback stops for the
actual movie handoff. Its authored Init keys now animate entry. The original
reference strongly darkens the scene; no arbitrary dimmer reduction was made.
Desktop composition retains the scene. The reported console black screen has
not yet been reproduced or confirmed fixed on hardware.

## Validation

Commands ran from the owned tree with the configured retail root:
- cmake --build build --target nfsu_career_tests nfsu_frontend_music_transition_tests -j 8
  PASS; logs native-build.log and verify-build-r2.log.
- build/nfsu_career_tests.exe <retail> build/menu-live-r1/browser browser
  PASS: all five selections, rapid changes, resident upload identity and bounds.
- build/nfsu_career_tests.exe <retail> build/menu-live-r1/frontend
  PASS; frontend-r2.log. First diagnostic assertion incorrectly required equal
  entry/final vertex counts; zero-size animated quads are legitimately omitted.
  The failed frontend.log is preserved; corrected assertion checks change/bounds.
- ctest --test-dir build -R '^(nfsu_career_retail|nfsu_frontend_music_transition_retail)$' --output-on-failure
  PASS 4/4 including retail before/after fixtures; targeted-tests.log.
- Python native-motion.py: authenticated original bounded arithmetic completed.
  This is not a whole-function or whole-camera parity claim.
- Python overlay-check.py: actual frontend fragment shader, emitted atlas/vertices
  and blend modes compared with CPU output. PASS, gpu-overlay.json and
  welcome-gpu.png. Desktop GPU composition is not a Deko hardware test.
- cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=menu-live-r1
  PASS; sdk-configure.log.
- cmake --build build-switch --target nfsu_switch_race_nro -j8
  PASS; sdk-build.log.
- ctest --test-dir build-switch -R '^(nfsu_switch_race_nro|nfsu_switch_race_embedded_shaders|nfsu_switch_gpu_sample_contract)$' --output-on-failure
  PASS 3/3; sdk-tests.log.

Full matrices were not repeated under the owner's targeted-check instruction.
Native/browser screenshots were inspected. Hardware heap/peak and continuous
music, Accept/Exit and repeated selection still require the owner console run.
No upload, remote push, merge or publication was performed for this batch.
Artifact and source identities: build/menu-live-r1/delivery.json.
