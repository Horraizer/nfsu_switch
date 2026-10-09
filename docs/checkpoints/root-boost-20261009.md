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
