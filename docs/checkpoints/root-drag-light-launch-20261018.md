# Drag shift light and launch - owner continuation

CHECKPOINT
role: worker
agent: GPT-6 root, solo owner implementation
assignment: Owner requests original blue/green/red drag shift light and launch boost audit.
phase: implementing
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: runtime root/build/day20261008-root-combined
saved: Local uncommitted integration; GLM merge preserved. Prior r6 sealed separately.

Owner deferred r6 Netloader upload. This task does not revoke that pause.
Do not upload r7 until fresh owner send authorization. No reviewers/agents,
new branches, pushes, merge completion or publication in this iteration.

Confirmed: load_race_drag_hud omitted Shift_light and Shift_light_back.
Original4A5200..4A5796 reads gearbox upshift table, category6FFF60 and
human difficulty scaleD8. Green threshold is up-.95*.5*window; late is
up+(1-.95)*window; light visibility requires publishedRPM>up-.95*window.
Strict original comparisons retained. Original authored PulseBlue/Green/Red
scripts and image resources restored. Gear text also follows original tint
and1500RPM pre-green interpolation. HUD argument+23 is view mode, not
lap-record status. Adapter supports the existing authored chase HUD views.

Confirmed: original46D1C0 sets neutral target from idle/redline/shift fraction;
constructor46D447 multiplies by drive-class factory+5C (front1,all1.2,rear1.1).
The port request table omitted this neutral slot. Existing grid adapter also
assigned first directly, bypassing46E8FB..46E925 quality evaluation. Added
neutral target and evaluate physical engine RPM at held-grid release once.
Existing production clutch timestamp and46DEC3 torque pulse consume quality;
no invented torque or pulse duration. Ordinary forward request helper remains
unchanged. Native source cases authenticate speed.exe and GLOBALB; no hooks.

Native-r1.json/r2.json used an incomplete supplied pulse prefix; comparison
failed25 cases, preserved in powertrain-r1.log. This is a fixture error: the
authentic46DC98 prefix initializes the timestamp before46DEC3. Native-r3.json
runs that prefix and preserves6 target/48 neutral-request+torque observations.
No claim of original whole-controller, whole-race or live difficulty parity.

Initial PC source compile PASS; ordinary HUD unit PASS. Corrected native
fixture/current-source PC build ongoing, then actual scene/HUD/launch checks
and fresh SDK packaging. Hardware remains unverified. Evidence and logs:
build/drag-light-launch-r7. Retail files read only.
next: Finish focused launch and authored-light render checks, freeze combined NRO.
blockers: none for local implementation; owner deferred hardware upload.
ownership: root active, solo.


Final local verification (same production source as SDK artifact):
- Native-r3:6 original neutral targets and48 original neutral request/torque
  cases, executable+GLOBALB SHA authenticated, x87=007F, no semantic hooks.
- cmake --build build --target nfsu_player_powertrain_tests nfsu_race_hud_tests
  nfsu_career_races_tests -j6: PASS, pc-build-r2.log. Follow-up test capture
  fixture build PASS, pc-build-r4.log; no production change after r2.
- nfsu_player_powertrain_tests: PASS, powertrain-r2.log. Includes new native
  launch cases and existing original drag quality/heat/boost/clutch cases.
- nfsu_race_hud_tests: PASS, hud-r1.log.
- NFSU_DRAG_LAUNCH_PROBE=1 NFSU_DRAG_LAMP_PROBE=build/drag-light-launch-r7
  NFSU_CAREER_SCENE_CHECK=1 NFSU_CAREER_TEST_DIFFICULTY=0
  nfsu_career_races_tests RETAIL4 600: PASS, launch-scene-final.log.
  Actual CIVIC and3 rivals, full renderer command validation and HUD loading.
  Test-only button driver holds RPM, actual GO release selects positive
  original quality; first gear consumes positive torque pulse. Authored
  blue/green/red image/script resources render all3 colors. Controlled HUD
  readout captures are not original-game screenshots or hardware captures.
- Earlier launch-scene-r1 failed because the test button driver overshot the
  valid launch window under original neutral throttle slew. It is preserved;
  the anticipatory release changed only the test driver, no game constants.
- nfsu_career_races_tests RETAIL4 6000 with Easy: PASS to actual fixture
  finish, drag-finish-final.log. A bounded fixture driver, not owner balance
  acceptance. Do not infer all difficulties or whole-trajectory equivalence.
- build/drag-light-launch-r7/build-switch-drag-light-launch-r7.sh: fresh SDK
  build exit0 and7/7 package/shader/GPU-contract gates PASS, switch-build-r1.
- Bash ASCII/entry mode/diff whitespace scan PASS, ascii-r1.log. Snapshot
  seal additionally scans every source byte and every new path name.
- Inspected actual PC composed lamp-blue/green/red.png. Capture helper first
  omitted atlas_height (lamp resources still correct, fonts sampled wrong);
  final helper uses the real atlas height, all3 captures inspected again.
  shift-light-pc.png contains crops of these controlled PC HUD frames.

All r6 mechanics/difficulty/dirt/shadow/startup fixes retained in one NRO.
Source comparison against r6 is saved in r6-to-r7.diff. Main checkout clean;
no agent/reviewer/branch/ref/upload action. Frozen identities live in
build/drag-light-launch-r7/light-launch-r7-receipt.json; source snapshot is
local only with this owner integration. Prior frozen r6 preserved.

phase: awaiting-evidence
remaining: Owner Switch verification of lamp timing, launch feel, mechanics,
and Easy balance. No full Default/Release matrices, independent review,
merged/publication or original full drag parity claim. Other deferred scopes
remain deferred. Source and artifact are ready for the authorized hardware
step when the owner resumes upload.
next: On fresh send authorization and Netloader readiness, send exactly the
sealed light-launch-r7 NRO with nxlink log and record runtime receipt.
blockers: Owner explicitly deferred console upload; do not send automatically.
ownership: root owns prepared artifact and follow-through, solo.
