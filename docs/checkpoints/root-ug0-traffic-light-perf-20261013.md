# UG0 traffic, headlights and performance

- Role: solo worker, owner requested one combined hardware candidate.
- Base: ea3ab19, local only. No reviewer or publication.
- Evidence: build/ug0-traffic-light-perf-r3 (runtime-relative owned tree).
- Hardware baseline: ug0-feedback-r2, completed two laps. Mean frame work
  21.382594ms, p95 32.75ms; pair_contacts stage includes traffic and props,
  mean 2.881547ms. Not stable60.
- Confirmed: impacted traffic used human physics with no hull-world collision.
  Original583E80 selects3 via426110. Stop branch583E0E/5833F5 targets speed0.
- Original57D000 weights at CarTypeInfo+C60 select column0 for track1003;
  six eligible models, not all15 class2 records. Emergency weights are zero.
- Implemented selector3/world contact and eligible roster. Bounded hull scratch
  storage removes repeated allocations without changing contact order.
- Headlight equations and ps1.1 compiler output preserved. Brightness cause
  not yet established; do not claim parity or blindly dim the original color.
- Targeted native checks and SDK/package completed below. Source remains local
  and uncommitted; this is not full acceptance. Headlight brightness remains open.
- Owner replied "too early" to the Netloader question. Upload is held; no new
  readiness or permission to send is inferred.


## Local verification and open brightness investigation

Evidence directory remains build/ug0-traffic-light-perf-r3. Results apply to
local changes on ea3ab19, not a published commit. No additional agents used.

- Native original57D000 chooser on the copied authenticated retail table and
  nine supplied seeds selected only weight1 models. Original583E80 requested
  selector3. Factory callback observed, not executed; live RNG not reproduced.
- cmake --build build --target nfsu_intro_grid_viewer nfsu_race_session_tests
  nfsu_ai_track_tests nfsu_race_effect_fast_tests nfsu_gpu_upload_tests
  nfsu_shader_manifest_tests: PASS (Release, existing configured retail root).
- build/nfsu_race_session_tests.exe: PASS (race-tests.log).
- build/nfsu_gpu_upload_tests.exe: PASS (gpu-tests-r3.log). Earlier old-stage
  validator failures retained in gpu-tests.log; validator now matches manifests.
- Targeted CTest shader_manifest_tests, shader_source_guards, ai_track_tests,
  race_effect_fast_tests: 4/4 PASS (targeted.log).
- Retail traffic-impact probe: all six eligible hulls retained world collision
  in the directed wall approach. Prior human/no-world baseline crossed it.
  Incorrect first fixture absolute-height bound retained as failed impact.log;
  corrected ground-relative fixture passes in impact-r2.log. PC timing is not
  console performance acceptance or complete traffic collision equivalence.
- Actual retail RaceRun 6000 ticks and render/package consistency: PASS in
  final-drive.log; generated final-chase/grid/traffic.tga captures preserved.
- Real SDK build nfsu_switch_race_nro: PASS (sdk-build-r2.log).
- ctest --test-dir build-switch -R
  "^nfsu_switch_race_(nro|embedded_shaders)$" --output-on-failure:
  2/2 PASS (sdk-package-r2.log). Initial missing RomFS vertex shader failure
  preserved in sdk-package.log; all four explicit shader package lists fixed.
- Per-vertex light projection replaces three per-fragment world dot products;
  light equation/color remain unchanged. Zero-alpha original lamp sprites skip
  emission only when depth/blend state guarantees no output. No 60fps claim.

Brightness evidence: authenticated original color binding4102A7, projection
40DD00 and mask40E560 agree with the current recovered equation. Compiling the
retail worldheadlight and worldheadlightreflect pixel shaders with legacy
D3DX9_24 produced ps1.1 mul/mad/mul_x2 without explicit saturation. Compiler
outputs are preserved in worldheadlight-ps11.txt and
worldheadlightreflect-ps11.txt. This does not prove whole-frame parity.
No arbitrary brightness multiplier or speculative saturation was introduced.
Original receiver selection/runtime technique and matched-scene final output
remain comparison gaps. The current shader optimization does not fix brightness.

Remaining: establish the brightness mismatch, actual portable Switch frame-time
and traffic-impact checks after owner permits upload. SAT pair contact and held
brake traffic control remain bounded port adapters, not complete original GJK
or traffic-controller recovery. No full Default/Release matrix repeated under
owner's minimal-test instruction. No push, merge, release or upload.


## Combined r4 candidate

Owner then authorized finishing lights and building the combined candidate for
later Switch testing. No immediate upload was requested; deployment stays held.

Confirmed missing source behavior: load_headlight_texture and
load_original_car_glow_textures decoded only level0. Original GLOBALB.BUN
HEADLIGHTS and the inner/outer flare chains have four levels; HEADLIGHTGLOW
has five. The original worldheadlight/worldheadlightreflect samplers explicitly
use linear mip filtering and clamp. The port's clamp sampler disabled mips.

The race loaders now preserve all authored levels, upload descriptors carry
those levels and full payload, and the projector clamp sampler uses linear
min/mag/mip. Existing world sprite filtering already uses linear mip filtering
in the actual race path. No generated mips, brightness multiplier, gamma change
or removal of source light was introduced. An actual retail regression verifies
all four lamp chains reach the GPU upload table on both supported scenes.
This fixes a proved filtering omission; it does not prove the owner's entire
brightness discrepancy has been eliminated. CPU captures still sample level0,
so they cannot establish the console's new mip-filtered appearance.

Additional commands/results for r4 (logs in the same evidence directory):
- cmake --build build --target nfsu_freeride_skyline_tests nfsu_intro_grid_viewer -j 4:
  PASS (light-mips-build.log).
- build/nfsu_freeride_skyline_tests.exe <authenticated retail root>:
  PASS, zero failures, both full-effects scenes (light-mips-retail.log).
- build/nfsu_intro_grid_viewer.exe <retail root> <new prefix> 120 profile:
  PASS actual UG0 metadata/start, RaceRun and four-car GPU package/captures
  (mips-r4-ug0.log). Earlier impact/6000-tick tests remain retained.
- Real SDK configure tag ug0-traffic-light-r4 and build target
  nfsu_switch_race_nro: PASS (sdk-configure-r4.log, sdk-build-r4.log).
- ctest --test-dir build-switch -R
  "^nfsu_switch_race_(nro|embedded_shaders)$" --output-on-failure:
  2/2 PASS (sdk-package-r4.log).
- git diff --check and modified-file/path byte scan: PASS. Canonical HEAD ASCII
  scans print nothing; no symlink/submodule entries. Local commit follows.

Exact NRO and source identity are sealed in package-r4.json beside
nfsu-ug0-traffic-light-r4.nro. The held r3 binary was preserved separately.
Remaining owner check: portable headlights/glows in motion, traffic impact and
frame-time delta on the same route. No stable60/full-original-contact claim.
