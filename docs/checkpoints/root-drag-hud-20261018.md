# Drag HUD owner correction - neutral/first, messages, blue lamp

CHECKPOINT
role: worker
agent: GPT-6 root, solo
assignment: Owner rejected r8 gear/neutral presentation, absent early/late
messages and blue lamp never lighting. Correct all three in one candidate.
phase: candidate ready; hardware visual acceptance open
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: runtime root/build/day20261008-root-combined
saved: Local dirty owner integration; r8 frozen NRO/source kept intact.

Owner test log r8 captured and ended with exiting. No crash during the
recorded drag drive; owner HUD rejection is authoritative acceptance feedback.
Metadata updated at build/drag-rpm-audit-r8/upload-drag-rpm-r8-run1.json.
No agent/reviewer/branch/ref/merge/push/publication action. Retail read only.

Confirmed faults:
1. Original constructor4A3FCD stores 3rdPersonNeedle at HUD+20; its dynamic
   tint was incorrectly applied to 3rdPersonGear in the port. Original gear
   label at+6C keeps authored color when clutch mode3 and orangeFFFF8000
   otherwise (actor3E8 producer478F92). Original gear table6FB7B0 maps
   neutral1 toN, first2 to1; numeric mapping was already correct. Reproduced
   current N/gear frames in before-neutral/before-first before changing code.
2. Blue/red authored scripts last600 ticks without looping. Port selected
   blue at idle, let it finish while invisible, and never restarted it.
   Actual original4F6CE0 returns false when elapsed>duration;4F6B70 resets
   current time when the HUD requests that state again. Restart is now tied
   to state selection/expiration, including the hidden RPM approach gate.
3. Only positive quality events and two labels were connected. Original
   4A5059 binds hash2303D76C Short Shift! for zero quality;4A50A1 binds
   27D75CD8 Over Rev! for negative quality. Both restored with Good/Perfect.
   Conduit38 glyph subset also lacked G,o,f,!,S,i: drag-only loading now
   adds those glyphs before texture placement. Messages would otherwise
   contain holes even after their event was restored. Centered text now
   honors authored vertical justification, as the existing ordinary helper.

Native source witness under build/drag-hud-r9/native-hud.py authenticates
speed.exe and GLOBALB. It observes five actual label selections at the UI
binding boundary and executes15 original state-active cases, including
600 equality versus601 expiration. No semantic hooks. Native-hud-r4 passes;
prior r1-r3 fixtures omitted current-gear publication/EBX actor binding and
are preserved as failed harness attempts, not passes. No original GPU
render or live FEng tick-producer equivalence claim.

Changes in race_drag_hud.inc, race_hud.cpp and race_hud.h. Current tests
extend the actual retail career renderer: stale hidden pulse restart,
neutral/first glyph/tint under all3 hint states, all four fully rendered
messages, and physical upshift event delivery regardless of quality.
PC verification complete:
- cmake --build build --target nfsu_career_races_tests nfsu_race_hud_tests
  -j6: exit0 (pc-build-r2.log). Initial fixture typo used x/y rather than
  TrackVertex.px/py; failed build retained as pc-build-r1.log.
- nfsu_race_hud_tests.exe: race HUD PASS (hud-r1.log).
- NFSU_DRAG_PRESENTATION_PROBE=build/drag-hud-r9/frames,
  NFSU_CAREER_SCENE_CHECK=1, NFSU_CAREER_TEST_DIFFICULTY=0,
  nfsu_career_races_tests.exe <retail> 4 600: exit0 (presentation-r1.log).
  Actual drive emitted negative and perfect upshift events; all four
  complete label glyph sequences and six neutral/first lamp states passed.
- Inspected PC raster neutral-blue, first-red, message-2 and message-3.
  N/1 authored tint, blue/red needle and lamp, full Short Shift!/Over Rev!
  visible. These are controlled PC HUD fixtures, not live Switch frames.

Fresh SDK build/package exit0 with tag drag-hud-r9; focused gates7/7.
Evidence: build/drag-hud-r9/sdk-build-r1.log. Frozen NRO/source and receipt
are in build/drag-hud-r9. Bash ASCII/whitespace scan passed (ascii-r1.log).
No full Default/Release matrix rerun: owner requested focused iteration.
Earlier engine/gearbox evidence remains sealed; no new physics changes.
Switch visual acceptance remains open; r8 hardware rejection is not closed
by PC fixtures or package checks.
blockers: none locally; hardware readiness needed after candidate is ready.
ownership: root active solo.
