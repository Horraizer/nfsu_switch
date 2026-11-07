# Selected engine and exhaust audio, 2026-10-12

Owner requested moving the remaining powertrain comparison to backlog and
working on sound. Solo implementation from75d35c1. No delegated reviews,
source publication, merge or hardware acceptance. The existing accepted NROs
are preserved. This batch addresses engine/exhaust, not all audio debts.

## Implemented

- Original520320 first remaps CarTypeInfo index; table6F8100 maps the result
  to a sound group. Applying6F8100 directly to the car index would be wrong.
  The playable-car mapping is recovered, including INTEGRA -> group2.
- Actual selected engine upgrade reaches both engine and exhaust bank paths
  in desktop and Switch live adapters. Event0 supplies the existing decoded
  level1 human performance setup. The sound group is not the visual kit.
- Original52649D..5264BE selects CarTypeInfo+9C0+upgrade*64;
  529CD0..529D36 produces the gain pair from signed+A/+C and normal crossfade
  endpoint multipliers. Runtime reads the selected record from GLOBALB.
- engine_audio.cpp translates the straight-line bank initializer into checked
  data operations at load, retaining the retail lookup tables and state. No
  retail x86 code executes on the console. Unsupported wiring is refused.
  Original lookup, product, selector, edge, oscillator and ramp callbacks
  produce four Player controls per bank. Matching group/upgrade are required;
  an original bank retirement request is an error, not ignored.
- Engine and exhaust receive separate voices. The PCM backend applies graph
  sample/state/pitch/gain, keeps loops across control changes and plays an
  authored tail on release. No file access or graph allocation per tick.
- The existing limiter remains. Six-dB combined engine/exhaust bus headroom
  is an explicit port mastering choice; it is not claimed as original volume.

## Evidence

Primary-workspace relative archive:
build-artifacts/draw-distance-20261008-root/audio-engine-r1.

engine_audio_native_check.py authenticates USA1.4 speed.exe, then executes
original bank code and callbacks through the earlier audio312 harness.
The compiled C++ probe uses exactly the same supplied RPM/load transitions.
native-r2 contains engine/exhaust controls for every group and upgrade.
native-tuned additionally executes the original selected-gain producer using
the authenticated Integra record and compares the resulting bank controls.
This proves bounded graph controls, not the original resampler or live input.

race_engine_audio_check.cpp replays the saved actual port grid/gas/coast/shift
trace through the production RaceEngineSound and mixer and through the old
CAR0 four-band mix. preview-r2 has before.wav and after.wav, no music/effects.
It also verifies malformed wiring, wrong-bank retirement, audible nonzero PCM,
changed output and finished-state teardown. Output is local, not distributed
retail assets. Playback tests cover release phase and sample-tail completion.

The first local run refused a mismatched event upgrade; the first callback
comparison exposed an incorrect ramp scale in the implementation. Both were
corrected before integration. native-r1 retains those ramp differences.
The initial full-gain preview overloaded the mix; preview-r2 uses the original
selected gains and explicit bus headroom. Keep failed evidence, not stale PCM.

## Local validation checkpoint

Native affected targets built, including the actual desktop race adapter.
Targeted CTest audio_playback_tests, race_audio_tests, race_audio_cue_tests and
race_audio_retail passed with the retail read-only fixture. Commands and results
are preserved in build-final.log and tests-final.log. Preview-r2 records actual
production control/mix execution without device output; it is not an original
PCM or listening comparison. Native mappings and bank names are separately
checked in native-selection.json. All new/modified files passed the byte-level
ASCII scan; the committed tree scan follows. Full known-unrelated matrices were
not repeated, following the owner's development instruction. Switch SDK and
package checks are the next step; no hardware audio result yet.

## Remaining

Original upstream audio RPM/load smoothing, gear-change sound events, full
camera/category/spatial gain ownership, rival engines, tyre material/control,
car/world impact episodes and nitrous controls still require separate work.
Physics RPM has not been changed to hide an audio discrepancy. Powertrain
fidelity stays in the owner's backlog. Current audio is not asserted to be
fully identical to the reference. Hardware listening remains required.
