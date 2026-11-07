# Garage flow correction and menu audit

CHECKPOINT
role: worker
agent: GPT-6 root integration session
updated_utc: 2026-10-19
assignment: Direct owner continuation after career-tuning-r18 hardware test
phase: investigating
branch: codex/draw-distance-original (owner integration, existing dirty merge)
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local integration only; no commit, push, merge or publication authorized
verified: r18 owner run captured in build/garage-flow-r19/r18-owner-run1-snapshot.log; original nxlink capture remains untouched
ownership: root, active; sequential solo per owner

Owner rejected garage black transitions, missing category camera focus, placeholder
part icons, invisible rims and blank submenus. r18 build/tests do not establish
these acceptance criteria. Existing production integration changes are preserved.

Confirmed causes:
- switch/race_main.cpp prepare_garage closes renderer and recreates graphics
  output when a component bank signature changes, including menu-only changes.
- Regular-part thumbnails fall back to GENERICPLACEHOLDER. Original 0x507B3D
  passes the category icon from table0x706530+4 to 0x507280, not TEXTUREHASH.
- Leaf FNG heading names are not all rebound. Vinyl layers have no master_thumb;
  they use Vinyl_Thumb_1..4 and need their own selection binding.
- FrontendMusic mixes speech over full music gain; no speech duck implemented.

Additional owner scope in this pass:
- Verify native category camera behavior and implement smooth focus changes.
- Verify original speech/music duck and restoration on end/cancel.
- Audit initial/next music choice against original, including repeated starts.
- Recheck animated arrows/fountains and other connected track animations.
- Recheck finish sequence: Race Over, results/statistics, style and awards.

Research is readonly authenticated PC1.4 USA speed.exe disassembly under
build/garage-flow-r19. Native partial ranges are evidence, not full fidelity.
No production correction from this pass yet; no new NRO/upload.

Next: trace original music gain/playlist controls and garage camera/icon inputs,
then fix the confirmed lifecycle/UI problems and run focused runtime checks.
Hardware behavior, animations and result-screen bypass remain unverified.
Default/Release matrices and independent review have not been run for this pass.

## Implementation and runtime findings (local, unsealed)

Production corrections now present:
- Same deko3d device/swapchain/display/HUD retained during garage bank replacement;
  drained upload ledgers and scene allocations retire before new uploads. Old CPU
  scene ownership survives the replacement call. Non-preview navigation reuses
  the installed base bank. SDK build passed after the first lifecycle change;
  later edits still require final SDK validation.
- Body icon table was shifted: authenticated original706530 associates FRONT
  with739C4A1B, REAR with015C767C, SKIRT with4E6C2AF9, SPOILER withBB034EA6,
  HOOD with028F7092 and ROOF with79165861. Corrected category and part entries.
- Native11-field camera presets and eased focus are connected to garage slots.
  tools/frontend_garage_camera_native.py executes original487830/4870C0/487680.
  frontend tests compare every saved native pose, view basis and translation.
  Actual current frontend tests PASS (camera-port-root-r17/frontend-root-r21).
- Rims style selection now previews its actual wheel record, before size choice.
- Leaf headings are rebound; four authored vinyl slots now have selection content.
  Full leaf navigation passed in pages-root-r22.log. UI screenshots reveal
  additional original placeholder text in performance packages; still fixing it.
- Speech music envelope added, separate from voice/cue gain. Native original
  settings constructor5364E0 and preset setter536870 executed by new witness
  tools/frontend_speech_mix_native.py. PCM attack/hold/release/retrigger tests
  PASS; actual music/cue transition test PASS in music-root-r21.log.
- Race audio previously restarted original IG bank1 on every event. It now
  resumes after the track(s) actually consumed; deterministic original order
  retained. This latest integration still needs focused final checks.

New decisive r18 hardware finding:
The owner WON event1. nxlink records CAREER_FLOW race.terminal place=1, followed
by post.failed: HUD image GPU memory budget exceeded. The combined frontend
atlas could not fit while unnecessary race postprocess buffers were retained.
It then returned race.return failure=1 without post_shown. This explains the
results bypass; no speculation about owner launch provenance is needed.
The current fix retires finished NOS/light-history targets before the results
atlas upload, keeping the scene/display. No memory limit increase was made.
Needs final package checks and actual Switch finish acceptance.

Animation audit so far:
r18 hardware log reports WORLD ANIMATION texture_aliases=37. Current source
rotates authored aliases every simulation step and writes fenced GPU descriptor
slots; arrows and WATERA are present in prior authenticated native research.
This establishes connection, not visual acceptance of every animation. Recheck
current actual event scene and named arrow/water assets before closing it.

r18 original log now ends with exiting and nxlink poll closure. Preserve it,
its sealed NRO and source receipt. No new upload/commit/push/publication.
Remaining: inspect/fix performance/vinyl/decal leaf presentation, complete music
history checks, scene animation smoke, final PC/SDK/ASCII checks, fresh source
seal and owner hardware check. Full matrices/independent review not claimed.

## Common animation cause resolved; mandatory matrices started

The first named-arrow assertion used SGN_ARROW (a white construction sign),
which is NOT referenced by track1003 geometry. The failed scene-root-r34/r35
logs are preserved. Direct readonly texture extraction and prepared mesh
inspection identified the actual neon barrier families in INGAMEB:
TRACK_BARRIER_NEONB_ and TRACK_BARRIER_NEOND_. Their definitions were absent
from the prior track-only animation collection, while their static frame
textures were already drawn. This was the genuine missing connection.

Added load_texture_animations: bounded header scan of an uncompressed chunk
file, skipping texture/mesh payloads on disk. The common texture sources and
spillover metadata are merged before wanted texture preparation. Only used
animation families retain their real frames. No per-frame pixel upload,
geometry rebuilding or extra draw pass was added. This is shared across tracks.

Current focused evidence:
- build-root-r42.log: focused PC build PASS.
- texture-root-r43.log: metadata source/transaction tests PASS.
- scene-root-r43.log: actual event1 scene, named neon B/D and fountain source
  descriptors advance to the authenticated frame after32 steps; full command
  validation PASS. Unlike the earlier generic count, this checks actual assets.
- neon-animation-native-root-r45.log: original545EA0 executes39 supplied
  clock/phase/image-rotation cases for the actual neon/fountain parameters,
  no hooks. Separate from production arithmetic and asset-loader checks.
- garage-root-r43.log: actual car/garage loading and rendering, original slot
  focus, retained old/new scene ownership and shared owned race car PASS.
  Front and wheel CPU snapshots inspected; both show the intended component.
- music-root-r32.log: actual FrontendMusic select/back cue transitions PASS;
  two separate RaceAudioService instances render different original song PCM
  with decoder backpressure and no underrun. Repeated stop does not skip twice.
- playlist-native-root-r29.log: original FE and IG selector consumes played
  bits and chooses subsequent default-order songs, no random shuffle assumed.
- frontend-root-r31.log: all128 saved native garage camera states plus the
  earlier browser camera controls PASS.

Full validation required by the supplied current protocol is now running;
this supersedes the older checkpoint's deferral of Default/Release matrices.
The existing build cache was Release; the Default gate explicitly configures
an empty CMAKE_BUILD_TYPE. A fresh sibling build-release uses Release. Both
configure the readonly ASCII retail root and the real Python3.12 interpreter.
Runtime PATH includes the compiler libraries. Build-only processes:
- exec session34961: full Default configure/build, full-Default-root-r44.log.
- exec session24013: full Release configure/build, full-Release-root-r44.log.
No production source edits while these builds run. CTest matrices follow
serially after their respective all-target builds finish. Do not claim them
passed or substitute earlier focused tests. Final SDK build must include the
new common-animation metadata correction (r34 SDK predates it).

Completed r18 nxlink capture preserved as r18-owner-run1-final.log/.json;
original logger process no longer running. Its NRO/source seal is unchanged.
No new upload, commit, push, branch movement, merge or publication.

Remaining: required gates and any confirmed failures, final UI screen check,
latest SDK/NRO checks, source seal, and owner hardware acceptance of black-free
transitions, rims/camera, speech music duck, neon/fountain animation and finish
results/style/rewards. Full future performance brand/package detail screens,
Trade In and other documented later tuning scope remain outside this slice;
raw editor placeholders are no longer published as kit descriptions.


### Full gate build failure and observer correction

Both Default/Release all-target builds r44 failed to link the gearbox phase
observer: the production helper now receives optional offset bounds and a
world RNG, while two test-only forwarding wrappers retained its old signature.
Updated tests/vehicle_gear_phase_tests.cpp and
 tests/vehicle_runtime_oracles.cpp to accept and forward those inputs unchanged.
No production physics or expected phase assertions changed. Full builds r46
are rerunning; neither matrix has passed yet. Original failed logs preserved.


### Native sponsor icons and mandatory gate status

Final brand audit found another real placeholder: sponsor hash_0c is a name
alias, not a texture hash. Original68F1F0 initializes the68-row table at744360;
512294 matches three aliases and reads its icon. New
 tools/career_decal_icons_native.py executes both original slices without hooks,
checks first-match behavior in204 supplied cases and verifies generated
src/career_decal_icons.inc. CareerGarage uses this lookup and FrontendArt loads
the required original sprites. Original unknown behavior remains zero.
Current pages-root-r69 run PASS; inspected the genuine5ZIGEN logo in the UI.
The earlier r64 patch attempt stopped after the header on a namespace assertion;
r66 completed the implementation explicitly. Its failed attempt is preserved.

All-target Default and Release builds r66 PASS, current SDK build r66 PASS.
SDK checks-root-r67 was NOT RUN under the wrong native CTest environment
(MSYS Python path); rerun through Bash sdk-checks-root-r68 passed7/7.
Tracked ASCII/whitespace scan r67 PASS; final untracked/source scan pending.
No claim of independent review or completed console acceptance.

Full Default ctest-root-r53:192 passed,52 failed of244, no retail skip.
This is a FAILED mandatory gate, not a release-ready result. It predates only
the sponsor-icon binding; current manual garage-pages covers that binding.
Release full matrix on final source is running as exec session82493,
ctest-Release-root-r70.log. Do not mark either matrix green.

To isolate attribution, sealed r18 source was extracted unchanged into owned
ignored build/garage-flow-r19/baseline-r18 with separate baseline-build.
Selected baseline comparison r65 reproduced all five selected failures:
world-contact oracle, circuit selection pin, drift1301 loader ceiling,
sprint1107 loader ceiling and race sampler policy. Manifest before/after
both passed. This does NOT establish baseline attribution for the entire
52-failure list. Current common animations add genuine frame residency;
old circuit pin already failed in r18 before that addition.

Remaining: record Release matrix result and full source ASCII scan, preserve
an explicitly unaccepted candidate/source seal if possible. Mandatory suite
failures prevent a passed gate or release claim. Owner hardware still needs
garage transition/camera/rim checks, speech duck, animated neon/water and the
post-finish results/style/reward flow. Later performance detail/Trade In scope
is unchanged. No new upload or repository publication.


### Final source candidate; gates remain blocked

Full Release ctest --test-dir build-release --output-on-failure -j2 completed:
192 passed,56 failed of248 (includes retail-full), no skipped retail tests.
Full Default result remains192 passed,52 failed of244. Final sponsor binding
was rendered by pages-root-r69 and is included in the Release full run.
No failed gate was weakened or replaced by a green focused result.

PC all-target Default/Release builds r66 PASS. SDK r66 PASS. Selected SDK
contracts r68 PASS7/7. Native sponsor table/lookup r62 PASS204 cases.
Final tracked/untracked source, scratch path and automation ASCII checks PASS
inside seal-root-r74.py, executed from the actual non-ASCII workspace path.
Main checkout status --porcelain -uno remains empty.

Unaccepted source candidate saved by seal-root-r74.py:
- build/garage-flow-r19/artifacts/nfsu-garage-flow-r19.nro
- same directory garage-flow-r19-source.zip, source-manifest.json and receipt.json
The receipt is the record of exact binary/source/evidence SHA-256 values.
Status UNACCEPTED CANDIDATE; NOT SENT. Do not claim a green mandatory gate,
full original fidelity, independent approval or hardware success from this seal.
Source snapshot predates this final checkpoint-only paragraph; production code
matches the snapshot. Prior r18 sealed artifacts remain unchanged.

Final UI inspection confirms native sponsor logos and regular body/rim icons.
The isolated UI pages intentionally have no3D scene in their black center;
combined front/wheel camera render evidence is garage-root-r43. Some authored
header/common cash labels still overlap on the Visual page. This needs native
runtime visibility/layout verification; do not describe every garage screen
as a completed pixel match. Full original category UI animation is not newly
claimed implemented by the GPU transition and camera corrections.

Smallest next owner decision: continue repairing the mandatory integration
suite before a hardware run, or explicitly authorize a bounded hardware-only
check of this unaccepted candidate with the failed gates disclosed. Until
then no console upload or release/publication. This is a precise checkpoint,
not completion of owner acceptance. Primary requested behavior still needs
Switch evidence, and full-suite failure attribution is only checked for the
five selected baseline categories, not the whole list.
