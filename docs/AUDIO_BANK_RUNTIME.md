# SND bank state and portable stereo output

This local restoration is part of the owner's interface/car/object/sound
fidelity audit. It does not certify a whole original race or hardware mix.
Retail speed.exe PC1.4 USA stays read-only; native tools check its identity.

## Shared bank random state

651150 initializes six words713FCC..713FE0 from6E9BCC..6E9BE0 plus seed.
651180 is only its fallthrough tail.6511B0 advances the shared carry state;
it is separate from high-level sound6F227C and simulation/world RNG.
64F580 initializes this SND library using config713D94. The game device
wrappers532A20/532BB0 do not replace that seed field; the fresh PE default
is zero. Device reinitialization is distinct from loading an ABK or a race.

BankAudioRandom replaces per-graph xorshift with that generator. All default
bank callbacks share process state. A replay can bind an explicitly owned
initialized context; load/reset does not invent a new seed. Control callers
serialize state access, and PCM rendering does not consume random values.

bank_audio_random_native.py executes real651150/6511B0 for the checked seed,
carry and consecutive-state fixture. race_effect_audio_native_check.py also
runs actual RNG inside selected original ABK wiring. Its two-module schedule
replaces one module midway and discriminates the rejected private-state
policy. Backend Player feedback remains supplied in that wiring comparison;
it is not proof of complete Player timing or natural caller order.

## Angular stereo publication

652FD0 constructs the original quantized256-bin speaker table. Stereo uses
speaker angles90/270 degrees;6533C0 reads the high eight azimuth bits.
64D280 only applies that angle to mono voices.64D1E0 normalizes the clamped
volume with1/32767, and6513B0/6512F0 publish per-channel software gains.
The fresh normal gain comes from64FBD0.656870 stores each channel result.

stereo_pan_native.py executes those original producers/consumers with only
lock and table-allocation IO supplied. Its fixed context is one mono
software voice, two speakers, master1 and the original fresh normal gain.
The C++ playback test compares coefficient float bits with that fixture,
including cardinal, wrap, quantization and volume-clamp boundaries.

The portable mixer builds the small table once before callbacks, caches
unchanged gains and applies them only after an explicit bank pan control.
Generic UI/music and stereo clips retain their own output. Engine/exhaust,
effect, smokeable and traffic Player bindings now pass their bank azimuth.
The human camera angle and remote spatial producer reach engine-bank input;
delayed529100 shift uses the human owner angle. The separate immediate
528D70/528F80 events keep their original fixed pan parameter.

engine_audio_native_check.py accepts a supplied pan and compares the actual
5236D0 owner writer plus original bank controls. The selected-effect oracle
now compares pan too, rather than discarding it from both observed sides.
Compiled real-bank tests exercise human/remote PCM side selection and finish
silence. Backend PCM interpolation, gain smoothing, limiter and integer
accumulation remain port policies. This software stereo recovery does not
prove the separate DirectSound3D/HRTF branch, all speaker layouts, original
source admission or final listening balance. Remaining NOS/skid half-gain
adapters are not silently removed.

## Started Player retirement

The former r66 witness replaced64D860 and left its mutable backend typeFF.
That invalidated its detached-tail conclusion. The static-byte ownership
policy and its forced audibility assertion have been retracted.

The corrected effect_retirement_native.py executes actual64D860 and64C690.
A successful type0 PCM start changes Player backend type before destruction;
64D630/652720/6584E0/651F30 then reach codec stop6571E0 and DirectSound
Stop/Release. Only actual PCM allocation/status and COM/device IO are supplied.
A forced post-startFF byte is an explicit negative control. This proves that
started-voice stop path, not the complete backend restart/status/timer model.

See FIDELITY_STATUS.md for open source and runtime acceptance. Current
publication, original-session and console HOLD remains in force.
