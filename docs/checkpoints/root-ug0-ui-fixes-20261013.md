# UG0 presentation correction, 2026-10-13

Owner reports on deployed career-r1-wip: Go Underground welcome absent,
Race Over absent, rival minimap color unchanged. Solo correction; no agents,
reviewer, board publication, push or merge. Owned tree:
build/day20261008-root-combined, codex/draw-distance-original.
Base:4fc70a2e01f91cd459d78b1dba763c98d805642d.

## Implemented

- Rival marker color from original5912B0 palette7313DD and58EA88 consumer:
  RGB0/66/255, kind5. Player color unchanged. Existing modulation retained.
- MU_UG_Explained.fng from authenticated decoded FrontB offset0xBC528C,
  English64711625, authored text/line breaks, font21, original logo/backing.
  Accept starts the prologue; Exit/B returns without saving or starting UG0.
  Shown while profile remains at intro; a resumed post-intro career bypasses it.
  Selection uses original steady colors/groups. The dialog fits a centered
  4:3 canvas; the main menu renderer is unchanged. Entry/pulse animation and
  full original career-profile predicate are not certified.
- HUD_SingleRace RaceOverMessage and RaceOverBacking animate keys at1200Hz,
  native4A7E1F/4A7F24, font38 and gold text. Race loop now presents the banner
  after a real finish, without advancing terminal physics. It then continues
  to the existing loss results or victory movie. Three-second dwell is an
  explicit port adapter, NOT recovered original finish camera/timing parity.
  Replay/profiling/AB diagnostic terminal behavior remains immediate.

## Evidence and checks

Local evidence: build/ug0-ui-fixes-r1.
finish-state.txt records raw original Animate tracks and original state name.
reference-start.jpg freshly samples the owner20261012 clip from0seconds.
Older reference-welcome.png actually showed the main menu; it was not used
as proof of welcome layout. Original welcome was visible in the fresh clip.
frontend-r2/welcome-0.png is the CPU render of the current shared UI geometry;
welcome-1.ppm covers Exit selection. All prior career pages still render.
hud-r1 has three-rival minimap and Race Over animation diagnostic frames;
these use a gray background, not a complete finished race or hardware run.

Commands from owned runtime path (toolchain on PATH):
cmake --build build --target nfsu_career_tests nfsu_race_minimap_hud_tests nfsu_race_hud_tests -j 4
ctest --test-dir build -R '^nfsu_(race_hud_tests|race_minimap_hud_tests|career_retail)$' --output-on-failure
Native final summary:5/5 including retail read-only before/after fixtures.
nfsu_career_tests RETAIL build/ug0-ui-fixes-r1/frontend-r2:PASS; five stock
loads, main Skyline reload, welcome both selections and existing career pages.
nfsu_race_minimap_hud_tests --capture RETAIL build/ug0-ui-fixes-r1/hud-r1:PASS;
three rival colors, immutable presentation, capacity, finish animation frames.
Actual SDK:cmake --build build-switch --target nfsu_switch_race_nro -j 4.
Package checks:ctest --test-dir build-switch -R '^nfsu_switch_race_(nro|embedded_shaders)$' --output-on-failure.
Exact final results and NRO identity are in manifest.json and final logs.

## Remaining

Hardware welcome Accept/Exit, actual finish transition, rival marker appearance
need owner verification of ug0-ui-r2. No full Default/Release matrix run under
owner's minimal-test instruction. This is a local WIP, not release acceptance.
Prior career-r1 live log has exited to menu and closed the socket. Do not infer
Netloader readiness from its old report or restart that completed session.
No hardware send in this checkpoint. Await fresh readiness after packaging.
Other traffic/world/UG event progression backlog remains unchanged.
