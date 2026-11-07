# Combined race sound candidate, 2026-10-12

Owner request: fix the listed race sound categories in one candidate and use
one hardware listening session. Solo work and targeted validation only;
no delegated reviewer, broad unrelated matrix, or new remote branch.
Powertrain fidelity remains in the separate owner-requested backlog.

Source: 14794fb6c442c2b74dae6800f53854ba22a3291b.
Parent engine/exhaust source: 22239a0236695629f1f7154639406b0e2a06d51f.
Owned tree: build/day20261008-root-combined, codex/draw-distance-original.

## Changes

- Keep selected car/upgrade engine and exhaust from the parent implementation.
- Execute the authored NOS00/01, skid PAV/PAV2 and SML/MED/LRG shift graphs.
  Handle sample selection, pitch/gain curves, ramps, random bags, integer
  delay, player completion, release and graph retirement. Original bank code
  is compiled into bounded operations at load; no x86 runtime is shipped.
- Decode all selected shift samples, including the LRG third sample. Its
  additional module code precedes the first template; stop at the actual
  graph RET instead of treating the next module as padding.
- Feed separate front/rear skid controls and actual gear changes. Use the
  current NOS upgrade to select its sound bank and play the release tail.
- Retain world contact audio through the original 300 ms touch lifetime,
  then release scraping over the original 133 ms fade. Short solver contact
  gaps no longer restart an impact or chop a scrape.
- Copy existing player/rival pair contact data into sound snapshots. Original
  531AC0 subtype and 531D10 variant families use SND_WORLDImpact, as original
  531350 does for both class0 and class1. No collision physics is changed.
  The port SAT contact point and two-impact output voice limit remain.
- Reserve three extra snapshot entries for player/car contacts without
  dropping the last world solver contacts. Sound context is output-only.

## Evidence and commands

Primary-workspace archive:
build-artifacts/draw-distance-20261008-root/audio-all-r1/receipt.json.
It seals the exact NRO, source SHA, and all evidence files and hashes.

Commands ran in the owned tree with C:/Strawberry/c/bin on PATH:

- cmake --build build -j4 --target nfsu_race_audio_tests
  nfsu_race_sound_preview nfsu_race_session_tests nfsu_race
  Result: exit0. Fresh affected dependencies, including contact layout.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_audio_playback_tests|nfsu_race_audio_tests|nfsu_race_audio_retail|nfsu_race_session_tests)$'
  Result: 6/6 PASS, including retail before/after fixture, no skips.
- tools/race_effect_audio_native_check.py --root RETAIL --probe
  build/nfsu_effect_audio_probe.exe --out build/audio-batch-r1/native-r2
  Result: all16 supplied graph contexts match original callback controls.
  Backend completion and RNG are supplied witness inputs, not live parity.
- tools/car_collision_audio_native_check.py --root RETAIL --probe
  build/nfsu_effect_audio_probe.exe --out build/audio-batch-r1/car-native-r1
  Result: all6 supplied geometry/velocity contexts match original531AC0.
- tools/engine_audio_native_check.py --root RETAIL --probe
  build/audio-batch-r1/engine-probe.exe --out
  build/audio-batch-r1/engine-regression --tuned
  Result: selected Integra engine/exhaust, zero callback-control differences.
- build/nfsu_race_sound_preview.exe RETAIL
  build/audio-batch-r1/preview-device --device
  Result: PASS20s production device/service, all requested effect categories,
  no runtime graph error or output PCM clipping. Includes music in actual
  service output; the saved combined.wav excludes music for diagnosis.
- root-sdk.sh in the primary archive configures audio-all-r1, builds the real
  Switch race NRO, then runs fma_census, race_nro and frontend_profile_contract.
  Result: exit0, 3/3 PASS. Final run follows the snapshot-capacity correction.
- Byte-level changed/new source and automation/name checks: PASS.
  git diff --check: PASS. Committed-tree Bash ASCII gate follows this record.

Earlier local failures remain archived: old contact-expiry expectations,
standalone raw mix without the production limiter, and the LRG module/sample
binding. Their corrections are separate from the final passing results.

## Explicit fidelity and acceptance limits

This is one listening candidate for the requested sound categories, not a
claim that the entire original sound engine has been ported. Upstream original
RPM/load smoothing, exact physical-to-skid parameter producers, camera/spatial
and category gain ownership, remote rival engines, turbo/backfire coverage,
and exact backend resampling remain incomplete. NOS/shift event gain22000,
six dB effect headroom and the existing stereo limiter are port mix policy;
these are not claimed as recovered original mastering settings. Gear-change
trigger timing uses copied actual gear, not the whole original sound-owner
state machine. Interface cue candidates remain as in the parent.

No change to acceleration physics, tyre forces, rendering or controller input.
PC device execution is not an owner listening verdict or Switch performance
measurement. Hardware acceptance remains pending for this exact NRO.

## Git and next step

The source and this checkpoint are local commits only. Fresh remote query:
origin/main2183f0e, github/main98888e7; neither contains the new sound source.
There is no remote codex/draw-distance-original branch. Do not silently create
extra GitHub branches or replace main with an unaccepted audio candidate.
No source push, merge or release upload occurred in this pass.

Owner has been asked for current Netloader readiness. Next: send the sealed
nfsu-audio-all-r1.nro on confirmation, capture one UG0/free-drive listening
run and its output audio underrun/clipping/thread metrics, then resolve any
reproduced sound issue in this same candidate. Keep the original source files
read-only and the rejected/failed evidence intact.
