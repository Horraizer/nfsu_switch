# Event8 visibility and physical parity audit

CHECKPOINT
role: worker
agent: Codex root solo
assignment: Owner event8 bridge, rival tuning/acceleration and crest behavior
phase: awaiting-evidence
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
saved: Local only; publication/index hold remains active
ownership: root active; no delegation or independent review

## Retail provenance

PC1.4 USA speed.exe SHA-256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7
GLOBALB.BUN SHA-256:
39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9
Retail inputs remain read-only. Ghidra full analysis completed, then the
functions and callers below were decompiled from that executable.

## Wrong span, lap clock0:19.29

The owner's correction supersedes the earlier pier hypothesis: the piers
are correct and must remain. The low span at the canal exit is the defect.

Original5450F0 derives view indices from the view table. Original53E290
adds exclusion bit2 for player views1/2. Original445EC0 rejects an instance
when the context mask intersects (instance.flags XOR0x60). The low bridge
variants in section302, instances29..31 have flags0x62. The normal high
bridge instance28 has flags0. The prepared RaceGpuScene path supplied no
mask, so both variants could be submitted to the player view.

The fix passes the recovered player-view bit to prepared culling and to
the owning reference frame. Cube capture retains its separate selection.
Mesh templates and retail transforms are preserved. This is a shared flag
rule, with no track number, mesh name, coordinate or instance exception.
The rest of the native context mask is outside this targeted fix.

Evidence under build/event8-parity-r52:

- root-original-r4.json:53E290 context mask producer.
- root-original-r5.json:408F00 caller and view scheduling.
- root-original-r8.json:5450F0 view index constructor.
- root-bridge-before.png and root-bridge-mask2.png: same camera, before and
  native main-view exclusion. The low span disappears; the high bridge stays.
- root-frame-probe-r2.log: real RaceGpuScene frame at the canal exit submits
  the valid bridge and no low alternate. First probe used another direction
  and saw no bridge; it is recorded as a failed proving frame, not a pass.
- tests/track_visibility_tests.cpp:test_prepared_culler checks main/capture
  alternation, correct bridge retention and prepared/reference agreement.

## Rival assignment

Original4B4040 selects authored presets with4B26F0, difficulty contexts from
opponent+10/+12/+14 and fixed tuning selector from opponent+16. Adaptive
57F700 receives the player's ride, nitrous level and performance category.
The current parser and caller use these same inputs.

root-mixed-native.json executes original57F2B0/57F700 with supplied CIVIC
mixed upgrade levels and the event8 FOCUS/GOLF records, without semantic
call hooks. root-mixed-port.log compares the port results. This includes
the owner's actual installed levels from root-r51-drive.log. Levels,
reference/rival ratings and aero outputs match in the supplied cases.
This verifies the adaptive setup seam; it does not prove full race balance,
the entire AI scheduler or a live original trajectory. No artificial rival
speed reduction or forced finish time has been added.

## Crest behavior: source boundary corrected

Original587070/588580 (human) and475BF0/475A00 (rivals) obtain440A40 road
height at each transformed strut attachment, then project along body-up.
The previous production adapter began with a finite segment and could miss
an already penetrated road or pick another surface at a seam. No jump-force
coefficient has been changed.

TrackVehicleGround now provides an explicit cached-height mode with one
ordered, allocation-free cache per car. The human model and AI world consume
it, retaining their distinct original compression formulas. Native player
controls and headless rivals select it. Baseline input-recording diagnostics
explicitly retain their prior finite-ray adapter: those recordings pin a
legacy approximation, not the new native player query. Direct vehicle/AI
query diagnostics also retain their explicit segment default.

The original dynamic-object577060 surface override is NOT connected by this
change. Static-road agreement is not full world/trajectory agreement.

The new player_strut_ground_native tool executes original587070 with440A40,
original567370 plane normalization and an empty dynamic-object list. It
has no semantic call hooks. tests/player_strut_ground_native.inc and
vehicle_track_tests pin height/slope projections. Integrated component
checks cover under-road compression, airborne droop and outside-region
misses. Initial synthetic normals differed by one ULP and the first proving
fixture failed; the corrected witness executes original normalization.

## Rival control-domain audit

Original44E2B0 initializes action4's retained speed to zero.450480 runs
45AE10 before4504EF..450654 following speed logic. That stage exists in the
port but is NOT the ordinary racing action. Full Ghidra450210/44DCE0 confirms
ordinary action7 publishes route-speed/enabled/catchup with no action4 cap.
An attempted action4 speed binding was rejected and removed after this
caller-domain check. The intermediate local source, bounded run and aborted
host matrix are preserved as rejected evidence, not release validation.

The current general45B150 path planner remains an adapter for ordinary
strategy/actions, including action7 retention versus general path search.
Full blocking/contact/passing action scheduling is not claimed equivalent.
No artificial speed reduction or forced finish time has been added.

The authored opponent byte+18 reaches original ride+736. Consumers in
44D8B0/450EC0 control blocking strategy; it is not an engine multiplier.
The event scalar+13C feeds style scoring, not acceleration. Both hypotheses
were rejected rather than wired into the drivetrain. Native full action7
source explicitly confirms ordinary route-speed publication.

## Validation and remaining work

Evidence is local under build/event8-parity-r52. Source freeze before final
validation: root-source-manifest-r52-r4.json and root-source-r52-r4.zip.
Publication/index hold remains active; no commit, push or independent review.

Initial full Default validation (before height changes) failed two general
60-second timeouts under concurrent host matrices and a real selector mismatch
in texture_filter_compare_main. The comparison now supplies the same native
main-view mask as RaceGpuScene. No image or pose pin was changed. Its targeted
retail gate subsequently passed, together with race_run and ai_action tests.
The first all-native height rollout also broke the old baseline recording;
legacy baseline replay selection is now explicit as described above.

The r2 matrix was cancelled on the action-domain correction above.
The r3 matrices are superseded by the completed r4 record below.
SDK passed. An event8 complete-race observation and hardware acceptance
remain pending.
The original default AI action scheduler and dynamic ground override remain
known parity gaps; neither a successful build nor a green unit test closes them.

## Final fixture correction

The full r3 Default matrix completed with only stability_tests and
stability_retail failures. Its one-wheel edge fixture placed an unmarked
lower quad beneath the complete upper quad. Original440A40 can reuse a
strict-interior cached polygon, unlike the legacy ray, so that fixture no
longer exercised its claimed edge domain. The lower road is now tiled
around the upper quadrant, with no overlapping deck or invented flag.
All previous stability assertions remain. The focused Default rerun passed
all four registered checks, including the retail read-only fixture.

Release r3 was cancelled before acceptance because its stability object
predated this correction. R4 repeats both mandatory matrices with two jobs
each and staggers their general environment checks. Only this test geometry
and checkpoint changed after the runtime freeze. The Switch NRO and all
production source bytes remain those already built and checked by SDK r52.

## Completed validation record

Runtime source remains the SDK r52 freeze. Only the stability test fixture
and this checkpoint changed for r4. This is local verification, not an
independent review or a full original-trajectory acceptance.

Command: `cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"`

Result: exit0; root-final-r52-r4-build-configure.log

Command: `cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release`

Result: exit0; root-final-r52-r4-build-release-configure.log

Command: `cmake --build build`

Result: exit0; root-final-r52-r4-build-build.log

Command: `cmake --build build-release`

Result: exit0; root-final-r52-r4-build-release-build.log

Command: `ctest --test-dir build-release --output-on-failure`

Result: exit0; root-final-r52-r4-build-release-ctest.log

```text
100% tests passed, 0 tests failed out of 287
Total Test time (real) = 1152.88 sec
```

Command: `ctest --test-dir build --output-on-failure`

Result: exit0; root-final-r52-r4-build-ctest.log

```text
100% tests passed, 0 tests failed out of 282
Total Test time (real) = 1557.45 sec
```

Switch SDK: root-sdk-r52.sh, exit0; root-sdk-r52.log records all11
registered checks passed. The NRO was not uploaded and hardware acceptance
remains open. A bounded production event8 run completed without query refusal
or reset; its conservative automated driver did not finish the race and
does not establish race balance. See root-event8-drive-r52-r3.log.

ASCII: root-ascii-r52.sh, exit0. HEAD path/content/forbidden-mode probes
each returned1 (no match). Current source bytes and paths are also scanned.
Index SHA-256 remains 2cd72e7c3be6f7645d5221b9a6f740b464a20bbffcec645336fd471f80730853.

Next: owner control lap of event8 on Switch with event8-parity-r52, checking
the canal exit, crests/landings and rival pace at the same installed tuning.
Full native strategy/action scheduling and dynamic577060 road override
remain outside the verified seams. Publication/index hold remains active.
