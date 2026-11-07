# Engine and traffic sound restoration

The original PC1.4 USA speed.exe is identified by SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Retail inputs stay read-only. Production executes portable C++, not x86.

## Actor identity is part of the proof

425350 selects distinct sound actors. The human allocation525C10 uses
vtable6BEEB0 and update52C620. Remote racers use subclass523420, vtable6BF2C0
and update524620. Traffic uses524970, vtable6BF150 and update5251E0.

The earlier engine_envelope_native.py witness executes remote-style controls
with the remote fixed load divisor. Its bounded arithmetic comparison is valid,
but its former description as a complete human engine witness was wrong.
That completion claim is withdrawn. EngineEnvelope retains that separate
remote kernel; HumanEngineEnvelope now drives the actual human runtime.

## Actual human control chain

52C3F0 publishes current physical RPM, throttle and gearbox load.529500/529530
advances human shift states BEFORE527080 and527180 publish audible load/RPM.
Ordinary load is the actual throttle mean, not the remote on/off average.
5292B0/5293E0 use the selected sound record's initial RPM drops and guards.
The field1D18 is current gear, as526AF0 demonstrates, not a gearbox mode.
HumanEngineEnvelope retains these states, curve order and low-gear timing.

The authored engine/exhaust balance is view dependent. The common positional
interface52EA00 publishes its angle;52C3A0 selects the active camera owner and
52C140 folds the angle and applies the appropriate sound-record offset or
interior value. human_engine_camera_balance implements that path.

529CD0 applies the integer crossfade before its upshift accent, unlike the
remote5235A0 order. The human position gain and category1 are then applied
BEFORE passing those gains into the retail bank graph. Applying a global
half gain after the bank is not equivalent. RaceEngineSound now follows the
human order; category10 remains a different owner used by remote controls.

The normalized RPM changes audible bank controls only. Physical torque/RPM,
acceleration, gears and tachometer are not retuned by this sound correction.
The gearbox torque publication retains its original units. Human setup526420
calls4647C0 on the selected RideInfo torque samples and stores their maximum
as the load divisor. The remote5234C0 override replaces it with160. These
must not be interchanged. The first actual-human witness manually supplied
160 and therefore did not establish human initialization. That claim is
superseded: human_engine_native.py now executes verified526624..526638 and
authenticates both setup vtable slots before running human controls.
Production takes the maximum from the actual selected/tuned handling curve.

## Original mix and final output

RaceAudioMix restores the traced normal/NOS/crash profile admission and
release curves for human category1, environment9, remote category10 and
music. Original zero-duration requests use the native minimum curve interval.
Fresh master/bus preferences and intrinsic human/music output gains are
witnessed separately by audio_position_native.py. FE SpeechMusicDuck applies
the absolute original music descriptor gain, including speech transitions.

Original536800 publishes gains before actor requests. RaceAudioMix retains
that publication phase and original52C1D0 high-speed, idle and crash producers.
HumanEngineEnvelope supplies actual local time, throttle edges and accepted
shift timestamps. human_profiles_native.py checks consecutive requests and
restoration without immediately republishing gains. Native fixtures own the
state pins; neither those scalar inputs nor the position routine prove every
remaining effect producer.
Custom user volume settings and per-song gain selection outside the supplied
unity-song witness remain outside this checkpoint's acceptance.

## Traffic sound

Traffic has its own ENV_STATIC horn/pass-by actor. It is not a player engine
on each traffic vehicle. RaceTrafficSound preloads the authenticated
Sound/IG_GLOBAL/ENV_COMMON_MB.abk and shares decoded samples across the pool.
TrafficAudioController retains the original geometric/speed admission,
car-type horn families, sound RNG draws, cooldowns, pass-by completion and
horn fade lifecycle. TrafficAudioPosition follows the original single-listener
distance gain, camera angle and Doppler publication.

The PCM backend owns fixed voices, with no bank reads or allocations during
update. Snapshot inputs contain copied physical positions/velocities,
CarTypeTable indices and activation epochs. Replacement, despawn and race
finish retire their voices. The mixer skips unused slots within PCM blocks.

## Reproducible evidence

The following tools execute authenticated original instruction slices:

- human_engine_native.py: actual human input/shift/load/RPM/gain order.
- remote_script_native.py: consecutive authored remote rev states and both RNGs.
- remote_position_native.py: actual remote positional context.
- human_profiles_native.py: profile admission and publication phase.
- human_camera_balance_native.py: actual positional angle and camera routing.
- race_audio_mix_native.py: consecutive profiles, interrupted curves and buses.
- audio_position_native.py: fresh preferences and final gain consumers.
- traffic_audio_native.py: real traffic controller with declared spatial and
  allocator/device boundaries.
- traffic_position_native.py: hook-free single-listener position publication.
- race_effect_audio_native_check.py: original bank wiring, including the
  selected ENV_STATIC variants, against portable graph controls.

Native fixtures own the state pins; nfsu_race_audio_tests compares them.
nfsu_race_audio_retail additionally renders the actual engine and traffic PCM
and checks lifecycle/retained storage. These are local tests, not a console
listening verdict. The complete required Default/Release/SDK matrix remains
necessary after the final integrated source boundary.

## Remaining boundaries

Remote racer playback now uses the actual grid car and resolved engine
upgrade. EngineEnvelope retains524620 scheduling, the remote160 divisor,
523560 authored balance and native signed32 accent/crossfade products.
The actual signed positional/category result reaches the bank without a
premature clamp. The final portable PCM consumer clips its device gain.
remote_position_native.py checks single-listener distance/pan/Doppler;
engine_audio_native_check.py also checks every bank with negative/high gains.

RemoteRevScript preserves the original two authored tables and524240 state.
Its fixture executes435F80,4216F0 and564A50, with declared race/physical
inputs. The runtime shares sound RNG with traffic and borrows the actual
planner/drag world RNG only during the main-thread update. No pointer is
retained by audio output. Independent diagnostics can supply their own seed.
Other missing original RNG consumers and the full live scheduler remain
separate boundaries; this does not establish complete original random order.

The former player prerace-script omission was a wrong actor classification;
524240 belongs to the larger remote subclass.
Additional backfire actors and the optional RPM-subtraction curve
producer are not established by these bounded human control slices. It was
previously called a limiter without proof of its trigger;52BF10 updates a
curve but does not identify its producer. Do not invent an RPM cutoff from
that field. The separate physical engine cutoff already has its own tests.

The stereo PCM mixer, resampling, output ramps and limiter remain the portable
backend, not proof of original device equivalence. Console NOS appearance,
listening, natural collision/recovery behavior and independent review remain
unaccepted. Never call the entire audio or physics system identical to retail
from kernel comparisons alone.

## Human shift event timing and gain correction

The live effects owner previously started a shift bank immediately on every
positive gear edge, at a fixed gain. Actual5292B0/5293E0 instead schedules a
pending cue using selected sound-tuning delays;529530 dispatches it only after
the deadline. Low-RPM upshifts and a down notification to native gear1
(Neutral; First is2) do not replace an earlier pending cue. HumanEngineShiftCue now retains that state inside the
human envelope. Physical transmission and RPM production are unchanged.

Actual529100 also truncates audible RPM, maps its1500..8000 interval through
the original crossfade lookup and scales selected sound-tuning short8. The
result passes the existing human position/category consumer before bank input.
The shift output no longer receives the port's extra half-gain factor. NOS and
skid output adapters are not silently reclassified as original device parity.

`human_shift_feedback_native.py` checks the real cue dispatch stage and gain
lookup. Its output actor is explicitly inactive; the fixture does not execute
device IO or certify the entire world/audio notification order. Tests own the
exact states and counts. `race_audio_tests.cpp` also drives the actual human
engine cue into the selected effect bank and PCM, with no immediate gear-edge
sample and finish silence. Independent listening remains unaccepted.

Immediate high-RPM disengage feedback528D70/528F80 is a separate actor from
the delayed shift bank and remains open. The human Turbo actor is connected
locally as described below; console listening remains unaccepted.

## Human Turbo spool and pressure release

The live human snapshot supplies46CCC0 engine+A4 divided by four times the
selected physical induction maximum, matching52C3F0. This is the existing
recovered physical turbo/display state, not a new RPM-based sound estimate.
The audible load/RPM and camera balance come from the current human envelope.
Original525C10 initializes the pressure-difference ring with three entries.
An earlier scratch interpretation called it five; constructor instructions
correct that interpretation. The separate human load average has its own size.

TurboSoundController follows528B60/528830/528470: the accumulated spool charge,
RPM-dependent ceiling, authored gain and fade, pressure-drop gate, release mode
and shared sound-RNG consumption. The primary event's final RPM parameter is
retained from creation. Release completion comes from the portable bank/PCM
backend. The selected induction level chooses its own sound-tuning row, not
the engine upgrade row.51DEC0 and521C80 select the Turbo bank using the induction
level and signed RideInfo sound variant; the current live binding uses the
original default variant. No extra customization field is invented.

The bank translator now supports original64CBD0 timer expiry at equality and
the authored wrapping inline IMUL. race_effect_audio_native_check.py exercises
every Turbo bank in spool and release modes. turbo_audio_native.py regenerates
the selected resource and consecutive controller fixture from speed.exe. Its
primary allocation/state and output completion are supplied boundaries; output
allocation/submission is explicitly hooked. This is not a running original
race or device-lifetime equivalence result. The native fixture owns exact pins.

RaceTurboSound connects those controls into two fixed voices with shared PCM.
Banks and samples load during race preparation; update uses existing storage.
The live category3 curve is checked beside the other original mix categories.
The compiled audio tests check spool, release, missing induction and finish
silence through the selected human engine feedback and actual retail samples.
The portable stereo mixer remains a backend boundary. Full original audio,
console listening and natural collision behavior remain unaccepted.

## Later shift/backfire restoration

The r64 source correction supersedes the immediate-actor/backfire omissions
above.528D70 and528F80 are distinct human output actors. The first observes
preceding audible RPM on5292B0; the second observes latched shift RPM when
529530 completes its first curve. Both use the initialized7058D4 curve and
6F4EF8 product, selected FX_SHIFTING_01 bank and category5. Separate graph
instances retain their original parameters and share decoded shift samples.
The human delayed529100 sound retains category1. Only the two immediate
clutch actors use category5; shift_category_native.py executes the real
526AA0/536C20 consumer and distinguishes their category gains before device IO.

527480's coast backfire uses526A40's upgrade gate, original sound RNG,
timer/state and gain/variant production.521C80 row1 chooses CAR_1 independently
of the selected engine-bank group. The current human owner publishes throttle
state and audible RPM after its envelope; backfire consumes shared sound RNG
before528B60 turbo. Only its sound decision/output is restored here, not an
unproved use of the separate1154 curve by other effects. Remote actors remain
a separate omission. The native witnesses and compiled PCM checks are named
in FIDELITY_STATUS.md; no whole-device or natural race parity is declared.

## Started Player retirement: corrected evidence boundary

The initial r66 witness did not execute64D860. Its supplied Player callback
left the mutable backend-type byte atFF and therefore skipped destruction.
That was not an authored ownership flag and did not prove an audible tail.
The static-byte policy and its detached-voice assertion have been removed.

The corrected effect_retirement_native.py executes actual64D860 and64C690,
with only type-specific PCM backend and allocator IO supplied. A started PCM
Player changes its backend type to0 and destruction dispatches a stop through
64D630/651F30. Deliberately changing it back toFF is a negative control only.
The port again propagates module retirement to its backend stop policy.

The selected LRG bank has a nonzero kind0 end sample; the SML/MED banks do
not. High-RPM event notification/category checks retain that distinction.
The corrected native codec-stop witness now reaches6571E0 and DirectSound
Stop/Release, with only PCM allocation/status and device IO supplied. Full
Player restart/status/timer behavior remains a separate boundary. See
AUDIO_BANK_RUNTIME.md for shared bank RNG and angular stereo restoration.
No complete audible or device parity is claimed by these component witnesses.

## Remote shift output owner

The remote controller is not the human delayed notification controller.
523990 sends529100 kind1 immediately using the preceding audible RPM and
parent6C pan.523A70 does not send that main sound on a downchange.
523E30 sends528F80 after the first upshift curve completes when the latched
RPM is at least7000. That separate end actor uses category5 and fixed pan0;
the main sound uses category1. Both use526AA0/536C20 spatial/category gain.

EngineEnvelope now publishes these notifications. RaceEngineSound retains
its preceding positional publication for the immediate notification and uses
the refreshed position for the later end. RaceRemoteShiftSound owns two
fixed voices per admitted rival, shares its selected decoded PCM between
main and end graphs, and retires them on finish, inactive actor and rewind.
RaceAudioService loads and drives those buses for its three remote engines.
It does not instantiate the human skid/NOS/turbo bus for every rival.

The differential source of record is tools/remote_shift_native.py and the
fixture it checks. It executes the actual notifications, state transitions,
shift constructors and gain/category arithmetic. Allocation/lock/free and
final bank submission are declared external IO. Position/category values,
non-interior output mode and gain profile state2 are supplied inputs.
The native fixture and real selected PCM/voice-isolation checks run in
nfsu_race_audio_tests and nfsu_race_audio_retail; the native verifier also
runs in nfsu_remote_shift_native_retail with the retail read-only fixture.

This restores the bounded ordinary remote shift path. It does not certify
natural module construction/update/device order, the full adaptive RPM
limiter, other output modes, remaining remote NOS/tyres or portable listening.
Those boundaries remain open in FIDELITY_STATUS.md.

## Remote adaptive gain profile

The separate529EA0 state machine is now connected after the second remote
input sample and before the raw-gain publication. Its selected sound fields
come from CarTypeInfo+9C0 plus the installed engine row:40/44/48/4C and the
signed short50. Rise duration depends on the actual selected current/first
RideInfo gear ratios. Down/up shift completion and real on/off timestamps
retain their own source order; a false input is not a new off transition.

Mode18E8 and blend18DC publish integer offset18E4.5235A0 adds that offset
after the shift accent, applies the authored balance, then conditionally
boosts the first channel from the channel difference. Spatial/category output
follows this calculation. The live rival snapshot now copies its selected
ratios, including the neutral/first special case; identity defaults belong
only to explicitly synthetic snapshots.

remote_gain_native.py executes the continuous original inputs, shift hooks,
profile and raw publication. The compact fixture hashes each complete case
stream, including RPM/load, mode/blend/offset, both raw gains, binary32 time,
last-off timestamp and throttle flag. --out writes fresh complete native rows
for diagnosis. nfsu_race_audio_tests compares the production envelope and gain
helper against those streams; nfsu_race_audio_retail additionally verifies
selected tuning fields and existing selected PCM playback. The native
regeneration is registered as nfsu_remote_gain_native_retail.

Inputs are declared stock selected ratios, non-interior mode, ordinary game4,
explicit position/category and authored balance256. Natural construction and
module/device order, the distinct196C RPM subtraction and transient5298E0 sound controls, other output modes,
remaining remote NOS/tyre actors and portable listening remain open. This
profile is not a change to physical acceleration, gear ratios or suspension.

## Human adaptive gain and bumper shift mode

The human owner now uses the same529EA0 profile with its own update order:
529530 completion hooks see the preceding audible RPM; profile update sees
the current human input/RPM.529CD0 balances first, adds the integer offset,
applies the mode-dependent channel boost, then applies the shift accent.
This differs from the remote5235A0 order. The bumper mode2 branch also scales
the second channel by1+extraA/newA; it is not an inverse ducking operation.

The human snapshot copies the selected current/first ratios. Actual52C3A0
copies the player POV byte to sound2B0; native POV2 is the bumper camera.
529100 uses that mode as the final shift-bank variant. The live cue now carries
it to the selected graph without changing the separate clutch/end actors.
The selected-bank native check includes that previously untested branch.

human_gain_native.py executes actual human input, shift wrapper, load/RPM,
profile, camera-mode producer and raw publication. It supplies explicit
conditional ratios, torque divisor, RPM maximum, fade and camera/balance
inputs; it does not claim a whole natural car factory or device replay. The
compact fixture hashes complete continuous streams. Full native rows are
available via a fresh --out directory. nfsu_race_audio_tests compares the
production human controller/profile/raw mixer; the registered retail checks
also compare selected bank controls, actual PCM presence, cue mode and live
ratio publication. Normalized RPM and physical acceleration are unchanged.

Optional196C RPM subtraction has no established active trigger here.1E48 is
a distinct transient sound-command producer through5298E0, called by the
human529500 wrapper; it is not that RPM field. Those transient commands,
nondefault11C8 fade, natural module/device order and full portable listening
remain open. Source tests do not certify DirectSound3D/HRTF or mastering.

## Ordinary remote Turbo output

Remote524130 now has its own controller and selected induction bank.523C30
publishes pressure1.38 or0 from the retained throttle latch. Unlike human
528B60, remote release uses audible-load crossing20; equality does not
release.528470 selects its mode/gain/RPM from the current sound owner and
the shared6F227C sound random context. The spool and release share decoded clips.

The Turbo child has its own6F42D0 spatial profile. Fresh preference base
attenuation, camera distance, near1 and quadrant ranges25/15/25/15 precede
category3; it does not reuse the remote engine's different profile. Port
output reserves two disjoint voices per rival outside all existing ranges.
The real grid supplies each selected induction upgrade and physical feature
admission at load time.46C6D0 admits any positive selected induction-curve
entry, including the factory Eclipse/RX7/Skyline curve at upgrade0. No
asset IO or clip decoding is added to the per-frame update.

remote_turbo_native.py and nfsu_race_audio_tests compare consecutive original
524130 output controls and the actual52EA00 spatial output. The controller
witness supplies an existing primary event, enabled feature and secondary
completion; even its level0 tuning case explicitly supplies enabled1. It is
not a claim that a stock car has a Turbo actor. Compiled retail playback
checks the three actors, the factory level0 Turbo bank and selected-curve
snapshot publication, and separately refuses output without induction.
Native regeneration is nfsu_remote_turbo_native_retail. Natural construction,
module/device callback order and portable listening remain unverified.

The earlier generic remote-backfire omission needs actor-specific ownership:
ordinary524620 does not call human527480 or the human52BF10 curve update.
No human coast effect has been added to rivals based only on a missing-sound
hypothesis. Other sound-owner triggers and196C's possible generic writers
remain separate research; isolated gain witnesses did not establish them.
