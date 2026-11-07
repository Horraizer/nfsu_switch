CHECKPOINT
role: worker
agent: Codex root, solo owner-directed implementation
assignment: owner requests original NOS acceleration-screen/blur effect
phase: awaiting-evidence
branch: codex/draw-distance-original
base: 4c3a5aa6925e327c7f57f65bfb0db6915367c676
worktree: runtime-relative build/day20261008-root-combined
saved: local source commit containing this checkpoint; no source publication
verified:
- cmake --build build --target nfsu_speed_blur_tests nfsu_intro_grid_viewer -j4: exit 0
- ctest --test-dir build -R '^nfsu_speed_blur_tests$' --output-on-failure: 1/1 PASS
- python tools/speed_blur_original.py with authenticated retail root and current
  probe: 220 cases PASS, exact envelope/composite byte; bounded float offset
  differences, full output build/nos-visual-r1/original-compare.json
- build/nfsu_intro_grid_viewer <PC-root> build/nos-visual-r1/world 720 speed-blur:
  exit 0, live NOS through normal L input. Two same-pose CPU scene previews.
- cmake --build build-switch --target nfsu_switch_race_nro -j4: exit 0
- ctest --test-dir build-switch -R '^nfsu_switch_race_(nro|embedded_shaders)$'
  --output-on-failure: 2/2 PASS, current embedded speed_blur.frag verified.
- Minimal targeted gates per owner request, no redundant full matrix/reviewers.
decisions:
- Original 4099BA/40CD40 blur precedes 4099C5 actors and 4099D3 car nodes.
  Initial preview placed blur after cars, was rejected and is retained as
  build/nos-visual-r1/preview-*. Corrected preview is world-*. Depth preserved.
- Half-resolution world blur, cars/shadows/glows/HUD later. Original envelope,
  speed ramp, four-tap filter, second-pass offsets and blend factor.
- Actual NOS resource drives the presentation effect in Free Drive and UG.
  Pause freezes it; reset/countdown/finish clear it. No powertrain/audio edits.
- Current prepared single-sample/FXAA race path only. Alternative MSAA path
  unchanged and does not yet support this stage. No invented FOV pulse.
- Adapter uses car velocity in current view. Exact original view velocity and
  separate camera pullback producer remain unverified; not full camera parity.
- Prior mode-parity-r7 fixes are included; no additional physics/audio change.
remaining: actual Switch appearance/onset/release and FPS, owner acceptance.
next: send exact NRO on owner request with fresh Netloader readiness; record
  its hardware identity and log. Do not assume old readiness or touch foreign nxlink.
blockers: hardware validation pending; publication rejection still prohibits
  board writes/source push/merge without resolved authority.
ownership: root; no delegated worker/reviewer (owner instruction)
artifact: build/nos-visual-r1/nfsu-nos-visual-r1.nro
sha256: a5e82090d5910ad19f7b698cf792c40c4be488c43e399e8920699c71d4e5520a
