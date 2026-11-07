# Sound-bank RNG continuation

CHECKPOINT
role: worker
agent: Codex root
assignment: Owner four-domain fidelity audit and correction, continued solo
phase: implementing
branch: codex/draw-distance-original; publication/index HOLD continues
head: ea4d472c9c399265370fa10be94736b1121771ad
base: local r66 corrected-source boundary; no new commit or merge
worktree: build/day20261008-root-combined
saved: local-only; build/fidelity-r67/root-entry.json
verified: r66 Default284/284, Release289/289, Switch14/14 and final ASCII/index/source checks passed
ownership: Codex root; active solo

The owner scope is still open. FIDELITY_STATUS.md lists current source gaps
and runtime acceptance separately. r66 is unsent and independently unreviewed.
No Git/index/board mutation, new original game/capture, computer use, new chat,
worker/reviewer dispatch or console transfer is authorized by this continuation.

Confirmed source mismatch: EngineAudioGraph callbacks7/8 use a private
xorshift seed per graph. Actual6511B0 uses six shared carry words.651150
initializes all six from PE6E9BCC..6E9BE0 plus a supplied seed.651180 is a
fallthrough portion, not the full initializer.64F580 reads config713D94;
64E800 copies/defaults that configuration. Native original consecutive
boundary/carry output is saved under build/fidelity-r64/root-bank-rng-original.json.
Ghidra source/xrefs are in the same directory. No production RNG change yet.

Next: finish the game/init seed caller admission, then add a typed original
bank RNG with shared process lifetime, actual-code fixtures, and a connected
multi-bank differential. Replace the synthetic RNG hook in the effect-bank
oracle with actual651150/6511B0 execution. Keep explicit replay/test context
separate from the production default. Do not claim the entire original sound
sequence from generator equality: missing actor/caller order and backend
pan remain distinct tasks. Validate targeted controls/PCM before freezing and
running the full mandatory matrices plus a new NRO.

## RNG implementation and critical retirement witness correction

The typed BankAudioRandom constructor/next-state is connected to bank
callbacks7/8 with a shared default SND lifetime. bank_audio_random_native.py
executes651150/6511B0 directly and the compiled audio test matches consecutive
seed/carry fixtures. The effect-bank oracle now executes original RNG rather
than substituting xorshift. Its normal selected-bank cases and a two-module
interleaving/reload case pass, including a private-state negative control.
Target logs: build/fidelity-r67/root-target-r2-*.log and root-effects-original-r2.
The former identical-NOS test required two matched independent SND contexts;
two simultaneous events in one native context do not consume identical RNG.
Those explicit contexts retain exact PCM comparison without changing the
shared production default. Full mandatory gates have NOT run on r67.

CRITICAL: deeper64D860 inspection invalidates the r66 retirement explanation.
Player+10 is mutable backend type, initiallyFF, not authored fixed ownership.
Actual64D860 changes it to the selected sample type before backend start.
The earlier retirement witness replaced the entire Player and did not model
that write, so it incorrectly exercised an unstarted FF Player. Its asserted
no-stop result does NOT prove a started one-shot survives graph destruction.
Do not accept tools/effect_retirement_native.py's old fixture or the r66
player_stops_on_retire constant as original fidelity. r66 remains unsent.

Next immediate correction: execute actual64D860 and64C690 while supplying
only type-specific PCM backend start/status and stop IO. Provide a valid
652720 voice pool and observe actual stop dispatch. Rewrite the witness to
assert dynamic backend type0; use FF only as an explicit negative control.
651F30 invokes a backend codec-stop callback. The exact tail semantics still
need that backend producer; do not force audible clutch PCM without proof.
The existing Effect::retire static-byte policy and its PCM assertion must be
corrected/retracted before further acceptance. Keep all failed/old evidence.

Useful original files in build/fidelity-r67: root-native-0064d860.txt,
0064d5b0,0064d630,0064d640,0064d660,0064d730,00651f30,00652720 and006584e0.
Backend tables:6E9A98 start(type0=64D5B0),6E9AA8 destroy(64D630),6E9AB8
release(64D640),6E9AC8 restart(64D660),6E9AD8 status(64D730). The simple old
retirement witness under build/fidelity-r64 is preserved as rejected evidence.

Separate pan research:652FD0 builds256 per-speaker angular tables from the
configured speaker angles using659130/6590B0 and quantized sine/cosine.
Stereo initializes90/270 degrees. EngineAudioVoice.pan currently does not
reach Mixer. No pan production change has been made; recover its consumer
and volume scaling before adding a guessed stereo law.

The corrected retirement witness now executes actual64D860. Started PCM
changes Player+10 to0, then64C690 dispatches64D630/651F30 stop. ForcedFF is
only the negative control. All selected upgrade cases pass. Removed the r66
static-byte ownership field and detached-tail policy. High-RPM checks now
validate controls/category and actual SML/MED/LRG sample eligibility, without
requiring invented clutch PCM for an immediately retired module. Backend
codec-stop/tail timing remains unproven; source propagation again uses the
existing portable stop policy. The former r66 retirement fidelity claim is
retracted even though its build/test receipts remain historical true results.

## Stereo producer and connected bank pan correction

The corrected started-Player witness now reaches real6571E0 and DirectSound
Stop/Release. Receipts: root-started-retirement-device-r1.json; no complete
restart/status/timer backend equivalence is asserted. Historical r66 gates
remain true, but its retired-voice explanation/policy is superseded.

Recovered actual652FD0/6533C0/64D280/64D1E0/6513B0/6512F0/656870 for explicit
mono software stereo context. stereo_pan_native.py regenerates the original
coefficient/gain fixture with only locks and allocation IO supplied. The C++
producer matches every checked float bit. Mixer now consumes explicit bank
pan, preserving generic UI/music and stereo clips. Pan propagates through
human and remote engines/exhaust, selected effects, smokeables and traffic;
delayed529100 gets the human angle, immediate clutch events keep fixed0.

root-target-pan-r1 and r2 receipts pass compiled audio checks, original
pan/RNG effect-bank wiring and supplied left/right original engine controls.
The effect oracle now includes the formerly discarded pan field. The whole
Player callback is still supplied there: use the separate corrected lifecycle
witness for the stated stop boundary. Full mandatory gates have NOT yet run
on current r67. New integrated real-bank side selection test is in progress.
AUDIO_BANK_RUNTIME.md and FIDELITY_STATUS.md retain exact scope/limitations.

Next: finish integrated PCM verification, freeze code, run required Default
and Release retail matrices and build/test a new unsent Switch NRO. Do not
mutate production code while the frozen matrices run. Publication/index/
board, original runtime/recording, console and independent-review HOLD remains.

Frozen source 2026-10-29T04:58:31.880850+00:00:
build/fidelity-r67/root-code-freeze-r1.json. Integrated actual human/remote
bank PCM side-selection test passed; no production mutation until validation
completes. Required matrix/Switch drivers are root-final-gates-r67.py and
root-switch-build-r67.py, each smoked from this actual non-ASCII workspace.

Native final fixtures 2026-10-29T05:14:43.846605+00:00:
root-native-validation-r67-r2.json passes original shared RNG, software pan
and started-Player stop witnesses. The r1 launcher used an unsupported --root
flag for retirement and failed before that witness; the failed receipt is
retained, and r2 uses the documented positional retail argument. No source
changed. Switch r67 configure/build and all package/shader tests passed.
Host Default/Release required matrices are still running, not yet accepted.

Traffic follow-through remains source research, not implementation. Existing
r27 cache/neighbor evidence was reread; new Ghidra copies under fidelity-r67
confirm these same boundaries and add explicit default-lane details:
- strategy4 =44A200/6C97D4: query4543D0 -> action18 priority600, then453920
  -> action3 priority300, else default8 through44E260. Selected priority and
  current-kind gates are in44E020. No racer strategy1 substitution is valid.
- default8 uses4528B0: its three-second retained epoch and side/empty
  descriptor branch call45CFF0; ordinary retained traffic lanes use45AE10.
 45CFF0 resolves456E90 lane admission before45AFD0 path publication.
-583350 expires458CC0 decisions BEFORE44CF30 road/neighborhood refresh,
  then583C80 road traffic/intersection producer, end/admission checks,
  action predicates/selection,583850 publication and44C940 scratch clearing.
  Its odd world-frame modulo gate applies to setup role3 (actor+14 ->+4);
  other setup roles use the retained floating timer. Earlier research wording
  incorrectly called this a physics selector; the typed constructor audit
  corrects that distinction. Impacted traffic remains setup role3. Current port tick phase and
  impacted scheduling require a coherent caller correction.
-583910 consumes strategy cap582F70,5A6BC0's road-speed drop and454F30
  interpolation, and5A6B00 at current/ahead path distances. It then limits
  the retained action speed. Its source is not the current min-of-four-point
  adapter.582F70 also uses retained controller4DC, not a constant alone.

Next traffic change must connect initialized/projected road data, explicit
583BB0/549130 policy and eight-slot expiry, direction/category11..16 lane
context, default8/avoid3/obstacle18 retained actions and actual583350 caller
phase together. The physical44F470 producer combines road and physical
forecasts; the old r27 aggregation supplied original physical returns and
must not be relabelled as a complete independently restored query. Current
corridor/spawn and natural collision criteria stay open. No production
traffic rewrite was made while audio source is frozen for required gates.

## Validated local r67 boundary, full owner scope still open

Updated UTC: 2026-10-29T05:38:10.936046+00:00. Source identity is root-code-freeze-r1.json,
not HEAD alone. Default and Release passed every registered check without
retail skips; retail-full and before/after file manifests passed. Switch
package/shader checks passed. Independent review and hardware acceptance
remain unperformed, and the NRO is unsent.

Command: `cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"`

Result: exit 0.

Command: `cmake --build build -j 5`

Result: exit 0.

Command: `ctest --test-dir build --output-on-failure -j 5`

Result: exit 0. 100% tests passed, 0 tests failed out of 284

Command: `cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release`

Result: exit 0.

Command: `cmake --build build-release -j 5`

Result: exit 0.

Command: `ctest --test-dir build-release --output-on-failure -j 5`

Result: exit 0. 100% tests passed, 0 tests failed out of 289

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r67 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r67 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0.

Command: `/usr/bin/cmake --build build-switch-fidelity-r67 --target nfsu_switch_race_nro -j 2`

Result: exit 0.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r67 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0.

Command: `C:\Users\horra\AppData\Local\Programs\Python\Python312\python.exe -B tools/bank_audio_random_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `C:\Users\horra\AppData\Local\Programs\Python\Python312\python.exe -B tools/stereo_pan_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `C:\Users\horra\AppData\Local\Programs\Python\Python312\python.exe -B tools/effect_retirement_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground --out build/fidelity-r67/root-started-retirement-final-r67.json`

Result: exit 0.

The first Release archive build failed with ranlib permission denied,
not a C++ diagnostic. The preserved failed receipt is
root-default-release-validation-r67.json. A fresh configure/build/test of
the identical frozen source passed in root-release-validation-r67-r2.json.
The native fixture launcher typo/failure is likewise retained separately;
the corrected native commands above actually ran. No failed attempt is
relabelled as a pass.

The software stereo coefficient producer, engine/camera pan propagation,
selected-bank pan observations and shared SND carry generator are connected.
The r66 retired-Player static-byte theory is retracted; corrected original
start/destruction reaches DirectSound Stop/Release. All broader audio and
source limits remain in AUDIO_BANK_RUNTIME.md and FIDELITY_STATUS.md.

Code freeze is now released for the next coherent local traffic change.
Do not reuse r67 gates for later source edits. Preserve root-source-r67.zip
and the exact unsent NRO receipt. No Git/index/ref or board writes, new chat,
independent review, original runtime/recording or console transfer occurred.
