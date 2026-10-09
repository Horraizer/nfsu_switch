# Skyline turbo and nitrous integration

Owner request: install turbo and nitrous on the tuned free-driving Skyline.
Work is sequential in the existing root-owned tree. No reviewer, worker,
source publication or merge is dispatched. Source remains local only.
Parent: 995e597a2ab16a625997c3a57be6344bf05ac17f, including minimap-r2.

## Implementation

The free-drive setup selects induction3 and nitrous3, leaving all other
upgrade levels unchanged. This is the port's selected upgrade configuration,
not a claim that the FE cosmetic preset authenticates these performance levels.
apply_vehicle_tuning uses Skyline's own original tables. Installed induction
is already added into crank torque there; no second turbo multiplier is applied.

RaceSession uses the existing NitrousControls L reservation, NitrousResource
and NitrousEngineState, with the race world's 4000Hz clock. Original additive
46D057 nitrous torque enters after throttle-scaled drive torque and before
engine braking, in both prepared force and engine derivative calls. Ordinary
port mode explicitly disables drag/override predicates; full original event0
global selection is not claimed. Grid hold spends no charge. Pause does not
advance the clock. Release/disconnect stops boost. Pose resets preserve the
bottle; a new session initializes it. No invented refill is implemented.

The original46CCC0 turbo display producer is separate from engine torque.
It uses the recovered fixed-angle polynomial, not libm sine, including the
fourth-power blow-off argument and required update interval. Pose reset clears
the display state. It runs once on the completed world update; original full
component scheduling parity is not claimed. The earlier27-case report contains
the correction's failed-before witness; final27 supplied contexts match the
original executable at binary32 stores under x87 007F. Initial fixture cutoff
and subsequent producer corrections are not counted as passes.

HUD uses original INGAMEB Turbo fill/lines/red/needle and SR_NOS_BAR6 artwork
and FEng centers, under the existing gauge anchor. NOS alpha reads remaining
charge, behind scale/needle. The Turbo label uses the existing retail font.
The atlas dimensions are unchanged. Full original live theme/alpha/timing
parity and NOS audio/camera/exhaust effects are not implemented or claimed.
NOS1 extends enabled session diagnostics only; disabled recordings keep their
previous byte layout. Generic input/spec ABI dependencies were rebuilt.

## Evidence and validation

Artifact directory: build-artifacts/draw-distance-20261008-root at repository
root. New artifact tag: boost-r1. Preserve previous minimap-r1/r2 packages.

- Native affected-target Release build PASS, boost-r1-native-final-build.log.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_race_boost_tests|nfsu_nitrous_tests|nfsu_race_session_tests)$':
  3/3PASS, boost-r1-native-final-tests.log.
- nfsu_race_boost_tests with the configured retail root PASS. Genuine Skyline
  records resolve both upgrades; mass and gearbox remain unchanged. Integrated
  paired inputs demonstrate additional actual crank torque, L without shift,
  consumption/release/depletion and reset/new-session lifecycle.
- root-boost-turbo-check.py:27/27 binary32 source producer cases PASS in
  boost-r1-turbo-original-final.json, authenticated speed.exe input and no hooks.
- Full native PC scene capture PASS on event1 and event8, with current ABI,
  Skyline and active nitrous. boost-r1-pc-event1/8.png and corresponding logs.
- Real Switch SDK Release build PASS; NRO, embedded shaders and frontend
  profile contract CTests3/3PASS, boost-r1-sdk.log. FXAA on, MSAA off.
- ASCII source/helper byte scans and whole committed tree scan required before
  sealing. Runtime helpers are smoke-tested from the actual Unicode workspace.

Owner requested only mandatory targeted checks. No complete default/Release
matrix is claimed. Earlier unrelated manual gear HUD fixture failures remain.
Retail inputs were read-only throughout; no retail output path is used.

## Remaining

Actual Switch acceptance of turbo/nitrous and corrected map is pending.
Seal nfsu-boost-r1.nro with source SHA and content SHA in boost-r1-manifest.json.
Ask for current Netloader readiness before upload. Owner should compare launch,
L hold/release, charge depletion and both tracks. Existing handling and original
selection limits remain; passing scoped checks does not close whole gameplay.


## Owner level-one successor: boost-r2

Owner explicitly selected level1 induction and level1 nitrous for the combined
map/turbo/nitrous hardware check. This supersedes boost-r1's level3 free-drive
selection. The original two-bar NOS_2 artwork and FEng centers now follow the
installed count2; the six-bar artwork remains for the earlier level3 setup.
The generic turbo and nitrous producers are unchanged. Corrected minimap-r2,
accepted rendering settings and previous integrated changes remain included.

Validation from the actual workspace path:
- cmake --build build -j4 --target nfsu_race_boost_tests
  nfsu_freeride_skyline_tests nfsu_track_render: PASS.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_race_boost_tests|nfsu_freeride_skyline_tests)$': 2/2 PASS.
- nfsu_race_boost_tests with retail root: PASS for genuine level1 Skyline
  setup, integrated charge lifecycle and original two-bar HUD artwork.
- Fresh PC full-scene/HUD event1 and event8 captures: PASS. The helper also
  asserts the actual resolved free-drive preset selects both level1 upgrades.
- Real SDK Release NRO build and the three existing NRO/shader/frontend
  contract CTests: 3/3 PASS. FXAA on, MSAA off, FPS overlay on.
- Changed source/helper byte and path scans: PASS before local commit;
  whole-HEAD Bash ASCII/mode scan follows sealing.

Evidence: boost-r2-native-build.log, boost-r2-native-tests.log,
boost-r2-retail-boost.log, boost-r2-pc-event1/8.log and .png,
boost-r2-pc-hud.png, boost-r2-sdk.log and boost-r2-manifest.json under the
existing artifact directory. Previous seals and failed history are preserved.
Local commit only, no source push/merge. No full matrix or independent review
is claimed, per owner direction. Actual Switch acceptance remains pending.
The sealed NRO has tag boost-r2; confirm current Netloader readiness before
upload and check corrected map, automatic turbo and held L nitrous together.


## Nitrous sound and exhaust successor: boost-r4

Owner requested nitrous sound and exhaust after reporting little perceived
acceleration on boost-r3. Scope here is audio and visible exhaust, not a claim
that the hardware acceleration report is resolved. Prior paired PC telemetry
at source784cb79 showed additional actual torque and speed under identical
inputs; boost-r3-pc-ab-finding.json preserves that bounded result.

Sound uses original Sound/NOS/Nitrous_00_MB.abk sample1 (whole-clip loop),
voice11, active RaceSession nitrous state and a five-millisecond output ramp.
Release, depletion, finish and service stop mute the loop. Pause uses the
existing global output fade. Gain0.55 is port mix policy; no full native audio
scheduler/spatial/controller parity is claimed. No guessed release sample.

Exhaust uses original SFX_FIREB hash B3095EB7 from Global/INGAMEB.BUN and
selected-car authored exhaust markers, transformed with their body instance.
Selected Skyline has one LEFT_EXHAUST marker. The texture's four cells use
immutable quad/index uploads, with one selected cell per active pipe. No draws
when inactive, additive blending, depth test and no depth write. Length/width,
axial billboard and animation cadence are explicit port adapters; native
particle producer, color modulation and lifetime parity are not established.
Existing headlights, glows, car shading, world shading and physics are retained.

Validation from the real workspace:
- cmake --build build -j4 --target nfsu_race_audio_tests
  nfsu_audio_playback_tests nfsu_freeride_skyline_tests nfsu_race_boost_tests
  nfsu_track_render: PASS. Fresh affected ABI/dependency build.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_race_audio_tests|nfsu_audio_playback_tests|nfsu_race_boost_tests)$':
  3/3 PASS. New activation/release/restart/finish PCM checks included.
- root-boost-r4-scene-run.py from repository root: fresh native event1 and
  event8 actual selected Skyline scene renders PASS. Original flame texture
  and marker loaded; visible composed-frame pixels asserted. Prepared and
  reference GPU commands validate with one active flame; release removes it.
  Original PCM is nonzero while resource active and silent after release ramp.
  Earlier full-atlas capture retained; r2 uses individual animation cells.
- root-boost-r4-build.sh from repository root: actual Switch SDK Release NRO
  build PASS; NRO, embedded shaders and frontend profile CTests 3/3 PASS.
- Changed source/helper bytes and paths ASCII PASS; git diff --check PASS.
  Whole committed tree ASCII/mode scan follows local commit.

Runtime evidence is under build-artifacts/draw-distance-20261008-root:
boost-r4-r2-pc-event1/8.log, boost-r4-r2-event1/8.png,
boost-r4-nos-event1.wav, boost-r4-sdk-build.log, boost-r4-native-tests.log,
boost-r4-manifest.json and nfsu-boost-r4.nro. Levels1/1, AF4, FXAA on,
MSAA off and FPS overlay off. Seal identities are in the manifest.

Owner requested minimal tests and no reviewers. No complete native matrix,
independent review, source push, merge or deployment is claimed. Actual Switch
sound/exhaust appearance and acceleration acceptance remain pending. Preserve
boost-r1/r2/r3 and prior hardware logs. Confirm fresh Netloader readiness before
sending boost-r4; no nxlink launched by this task.
