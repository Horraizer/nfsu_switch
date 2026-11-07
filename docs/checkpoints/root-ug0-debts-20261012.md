# UG0 fidelity debt audit, 2026-10-12

Owner assignment: inspect tachometer/acceleration, sound, traffic and opponent
behavior against the new original-game recording. Solo, sequential work;
no delegated review or broad matrices. Audited source: 34ce497b21d677f3766ba1a0061869375621191d.
Owner accepted the standings-gaps-r1 table on hardware. Its nxlink log records
an actual two-lap finish. Preserve that accepted candidate while fixing debts.

## Evidence boundary

Reference: video_2026-10-12_16-46-38.mp4; provenance and SHA256 in the local
ug0-debt-audit-r1/reference-receipt.json under the primary workspace's
build-artifacts/draw-distance-20261008-root. Viewed a three-second overview
and full-resolution gameplay frames. The clip contains a career introduction,
movie/loading transition, starting grid and opening racing segment, not a
complete race. Civilian vehicles are visible. The player's control trace,
selected difficulty and exact original runtime configuration are not recorded.
No listening verdict: audio findings below come from current source and earlier
authenticated original-code research, not a newly matched acoustic comparison.

## Confirmed current implementation gaps

1. Tachometer and acceleration: read RaceSession's countdown/running input,
   VehicleTrackSession grid engine/release, VehicleModel engine/gearbox/body
   update and RPM publication, and RaceHud readout. Neutral throttle shaping,
   inertia-dependent RPM fall publication and original angle bands exist.
   This does not close full trajectory fidelity. HUD speed still computes XY
   magnitude and fixed MPH instead of the original actor publication and units
   preference. Owner speed magnitude/forward fields already exist separately;
   exact HUD producer must be identified before substituting one. First-person
   dial variants are still absent. Earlier bounded native witnesses remain in
   root-tachometer-audit-20261012.md and root-grid-throttle-20261012.md.
   Next comparison: identical selected/tuned UG0 car, neutral rise/release,
   GO/clutch, fixed-gear throttle/coast, automatic shifts and NOS on/off; log
   raw/applied throttle, physical/published RPM, gear/clutch, wheel spin,
   speed, engine torque, boost and time. Attribute first divergence before
   adjusting any physics or presentation constant. The new video alone cannot
   establish a timed acceleration defect because inputs are unknown.

2. Sound: src/race_audio.cpp still selects CAR_0/ENG_UG_0 for every car and
   mixes samples1..4 at fixed RPM centers with approximate pitch/gain. There
   is no separate engine-bank selection for the actual car/upgrades, exhaust
   graph, shift-event audio or rival engine voices in this service. Tyre audio
   is one pavement sample driven by a clamped maximum-slip rule. Nitrous uses
   an original clip but a port gain and binary start/mute policy. World impact
   and scrape have recovered graph controls, while car-contact audio and full
   lifecycle/spatial admission remain incomplete. Source snapshot collects
   track contact observations, not pair_frame car contacts. Reuse existing
   AUDIO_FIDELITY_312 research; do not repeat bank discovery or substitute
   guessed equalizer settings. Resolve actual UG0 car/upgrades, then integrate
   original engine/load/exhaust/shift controls and compare isolated PC audio.
   Last hardware log has no output underruns/late cycles/final PCM clips, but
   limiter activity exists; these counters do not establish sound fidelity.

3. Traffic: event0_setup reads traffic policy and requests six role3 setup
   entries. Runtime has only player plus three rivals. Six setup entries are
   not evidence for six simultaneously visible cars. Implement original
   spawn/retention, route/speed and retirement policies and interactions after
   vehicle/audio work; do not place decorative static vehicles as a substitute.

4. AI: current IntroAiPlanner uses original route/context speed machinery,
   but AiSpeedPolicyInput.catchup_scale remains default1. ai_catchup_update
   has no live production caller. selected_ai_scalar is parsed from the event
   but has no production consumer. Difficulty context selection does exist;
   a missing catchup call does not prove the game is using hardest difficulty.
   Adaptive rival upgrades, action ownership and human-intent prediction are
   still incomplete. Establish original live configuration before changing
   speed or rubber-banding. Do not arbitrarily slow all rivals.

Additional opening-sequence debt shown by the reference: UG entry flow,
original cinematic/grid camera/starter sequence and transition timing are not
closed by the currently implemented crowd and countdown. Track as the same
first-race fidelity work, after the four priorities above.

## Focused baseline

Fresh build passed for nfsu_player_powertrain_tests, nfsu_race_hud_tests,
nfsu_race_session_tests and nfsu_race_boost_tests. One targeted CTest run:
ctest --test-dir build --output-on-failure -R
'^nfsu_(player_powertrain|race_hud|race_session|race_boost)_tests$'
passed4/4. Logs: ug0-debt-audit-r1/baseline-build.log and baseline-tests.log.
This checks implemented expectations, not full original-game equivalence.
No production change, new NRO, deployment or merge in this audit. Retail files
read-only. Next active priority is controlled tachometer/acceleration comparison;
then original audio, traffic and live AI balancing. Do not reopen the accepted
standings implementation or start parallel worker/reviewer loops.

## Owner priority update, 2026-10-12

Owner explicitly deferred the remaining tachometer/acceleration comparison
and selected audio as the active task. Do not continue powertrain research
in parallel. At75d35c1, limiter equality and HUD published-speed corrections
are implemented; the old XY-speed finding above is historical and superseded.
Hardware powertrain-r1 launched and drove, then returned to the menu. Evidence:
powertrain-audit-r1/hardware-run1-summary.json beside the archived nxlink log.

Backlog: match physical and displayed RPM during throttle release and shifts;
match selected/tuned launch and coast against original live clock/input/setup;
verify units preference and first-person tachometer variants. Sparse hardware
RPM logging does not close needle fall, and bounded engine comparisons do not
close whole acceleration fidelity. No arbitrary needle smoothing is authorized.

Active audio step: actual car/upgrade bank selection and original engine/load
layer controls, followed by exhaust/shift, tyres, impacts and NOS as supported.
Reuse authenticated audio312 research, work solo, and run necessary targeted
checks. Hardware sound quality still requires the owner's listening check.


## Owner deferred impact audio, 2026-10-16

After the collision-native-r8 console run, the owner reported that impact
sounds disappear. Owner explicitly deferred this defect for later and selected
the failed first drift launch as the current task. Preserve traffic/rival
collision fidelity as unaccepted; do not retune impact strength from this audio
report. Evidence: contact-complete-r5/nxlink-collision-native-r8-run1.log.
Next audio step, when resumed: trace contact-to-impact event admission and
voice lifetime through repeated traffic/rival hits; compare original behavior.


## Deferred drift HUD appearance, 2026-10-16

Owner completed the drift-civic-r9 console test and reported that the drift UI
is incorrect specifically in how messages appear. Owner explicitly requested
recording only: do not fix this now. Later compare message trigger conditions,
timing, transitions and animation against the supplied original drift reference;
exact discrepancies have not yet been isolated. This is an owner observation,
not a diagnosed animation-code cause. Existing impact-audio debt stays deferred.
Evidence: contact-complete-r5/hardware-drift-r9-summary.json and its source log.
