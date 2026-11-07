# Drift presentation checkpoint

- Owner scope: finish event3 start crowd/camera and HUD animation, then NRO.
- Owned tree: build/day20261008-root-combined; branch codex/draw-distance-original.
- Base: 926cc867884c1232dd2b78fb689a8faee4cfb727. Changes local and uncommitted.
- Corrected selection evidence: 7361C4 is local player count, not race mode.
  47BC63..47BC82 selects playlist9 for one local player and one race vehicle.
  INGAMEB chunk34036 lists scenes2331..2334. The port selects2333 deterministically;
  retail selection uses race RNG (47BD07..47BD17). No exact RNG replay claim.
  Earlier2337 and2305/2307 investigations were wrong branches of the selector,
  not accepted single-player implementations. Their logs and captures remain.
- Scene2333 reads camera cuts, car motion and crowd/starter from STREAML1RB.
  NISFlagGirl is an actor class, not necessarily a female model: model hash at
  actor+50 names the authored mesh. Compound starter playback is halved by5799AE.
- FEng Init/Highlight/SlowMessage and wrong-way animations are loaded from
  HUD_Drift.fng. Original Highlight chains to Init. Simulation clock drives UI.
- Reference video: video_2026-10-13_08-22-59.mp4, SHA256
  d4b8f1cf1b5e0c2c0ca0550b1e0822595e339c846785715117dadbbbf6f612b7.
  Countdown shows the gauge only; full drift HUD appears after GO.
- Evidence scratch: build/drift-presentation-r1. Failed compile/runtime attempts
  are retained (including scalar decoder rejection of the starter's140 lanes).
- Pending: final PC visual/lifecycle checks, native scalar decoder comparison,
  target checks, ASCII scan, NRO, console owner acceptance.
- Netloader readiness is not assumed; no upload performed.

## Local validation and delivery checkpoint

- PC build: cmake --build build --target nfsu_race nfsu_drift_track_check
  nfsu_start_crowd_check nfsu_race_hud_tests -j6 -> exit0 (build-pc-r12.log).
- nfsu_drift_track_check <retail-root> -> PASS. Authored HUD keyframes,
  loop/return to Init, staged HUD visibility, crowd animation/stable upload/
  retirement, and live input-only drift banking passed (check-r1.log).
- nfsu_start_crowd_check <retail-root> -> PASS unchanged UG0 lifecycle/restart.
- nfsu_race_hud_tests -> PASS.
- nfsu_race --root <retail-root> --event 3 --car CIVIC --generate
  build/drift-presentation-r1/camera-input-r8.txt --out
  build/drift-presentation-r1/camera-r8 --frames 100,300,383,385,420,650
  --car-shading on --texture-filter linear -> exit0, finished four laps,
  zero resets. Conservative route driver is not a scoring/handling oracle.
- Inspected PC captures: starter/front cut, cinematic release to chase,
  tach-only countdown and post-GO drift HUD. Fixed SlowMessage font binding
  and multiplier Sj vertical centering from original resource metadata.
- scalar-native.py -> native634180 returned on every bounded call; 590
  scalar outputs matched the float-store algorithm bitwise across starter,
  camera and car codecs (8/16-bit, type10/11). This checks the decoder
  algorithm against native execution, not full original scene rendering.
- Failed initial Switch command used unavailable WSL; retained log-r1.
  Actual SDK build uses C:/msys64/usr/bin/bash.exe, log-r2.
- No upload yet; console loading, visuals and performance remain unverified.

- SDK build log-r2 completed exit0. Switch NRO, embedded shader and GPU
  contract checks: 3/3 passed (switch-check-r1.log).
- NRO: build/drift-presentation-r1/nfsu-drift-start-r1.nro, 3968485 bytes,
  SHA256 be1be39886c462332d833348472af8991aa7cd434aa3906dc70f89ec564e7fbe.
- Tag: drift-start-r1. Local candidate only; no upload or hardware acceptance.
- ASCII scan of modified files/new paths/scripts passed. Full matrices were
  not repeated, per owner instruction to limit checks to affected behavior.
