# Idle launch / active-race damping correction, 2026-10-19

Owner reports drag-career-r13 still cannot win Easy and first gear at idle
barely moves (about2mph). Solo owner-authorized continuation, no delegation,
board publication or ref changes. Branch codex/draw-distance-original remains
at ea4d472c9c399265370fa10be94736b1121771ad with the existing dirty integration
and pending owner merge preserved. Root ownership at build/day20261008-root-combined.

Confirmed reproduction: ignored actual loaded career event4 driver supplies
no gas throughout countdown, full gas at GO, no nitrous. r13 states remain
around800rpm and1.26m/s through1200 frames, no heat, blow, cutoff or brake.
Source evidence and old output: build/drag-cold-r14/cold-r1.log. Live r13 capture
is snapshotted in r13-run1-snapshot-r1.log with receipt snapshot-r1.json; it
also shows a real full-gas idle trap, independent of unknown owner launch input.

Original477729..477771 checks WORLD STAGE4, not race type4. It overrides
rigid130 after the preceding collision-state policy: disable extra linear
(and its associated adaptive angular) damping when processed throttle>.3,
or published support==4 and abs(published forward vertical)>.15. It leaves
rigid134 unchanged. The published pose operand is biased state+58 /actor98,
first transform column z. Equality at.3/.15 does not pass the relevant gate.
Source585410 consumes this flag; the port restored the damping calculation
but omitted this outer stage override, so low-speed damping balanced drive
force and trapped the engine at idle. This is a confirmed integration omission,
not an engine-damage claim or a reason to add an arbitrary anti-stall boost.

Current change: VehicleInput carries explicit race_active context separately
from drag_mode; RaceSession supplies it to moving human and rival inputs.
VehicleModel and contact continuation apply player_race_body_policy after
collision flag selection, using published support/pose. Raw standalone model
callers retain their explicit inactive default. No engine torque, ratios,
shift windows, AI tuning, speed cap or target finish time is fitted.

Bounded native evidence: tools/race_damping_native.py executes actual
477729..477775 on supplied states without hooks; tests/race_damping_native.inc
pins its outputs and player_powertrain compares the translated policy. The
first harness attempt used a nonexistent observers API and failed before
emulation; damping-native-r1.py preserved. r2 succeeded. Initial translated
tilt comparison was reversed, detected by6 boundary failures; corrected to
GREATER before candidate acceptance. Final player_powertrain checks PASS.

Actual PC loaded-session result after correction: at step2401781rpm/4.08m/s,
step3605143rpm/11.51m/s; it continues shifting and accelerating. This history
is from current dirty source at build/drag-cold-r14/cold-r3.log. Intermediate
cold-r2.log was overwritten by an investigative rerun; cold-r1 andcold-r3
remain the before/after record. Do not treat overwritten intermediate output
as immutable evidence. The tracked career test now has an idle-launch option
which fails if full gas cannot leave idle and exceed10m/s without nitrous.

Checks so far:
- cmake --build build --target nfsu_player_powertrain_tests nfsu_career_races_tests -j6
  PASS; final build output build/drag-cold-r14/build-pc-r3.log.
- nfsu_player_powertrain_tests.exe PASS after source correction.
- NFSU_DRAG_IDLE_LAUNCH_PROBE=1 NFSU_CAREER_TEST_DIFFICULTY=0
  nfsu_career_races_tests.exe RETAIL4 1200 PASS actual idle launch.
- Original native regeneration PASS160 no-hook policy cases; output matches
  tracked include. NOT a complete original car/race equivalence assertion.
- Controlled warm launch/first-green driver remains41.54 both Easy/Hard.
  Easy2nd, KLUTCH41.49; Hard1st with rivals unfinished. This is NOT owner
  driving and NOT full difficulty acceptance. Same driver r13 was41.76.

Next: fresh SDK build drag-launch-r14, selected package checks, source seal;
then console verification of idle launch before accepting drag balance.
Difficulty Easy/Hard parity and owner gameplay acceptance remain OPEN.
Full Default/Release matrices are deferred per owner minimal-check request.
No Netloader readiness is inferred, and no running capture is stopped.
All previous message/voice, effects and collision debts remain OPEN.

Final focused checks:
- final-pc-r2.py PASS ordinary circuit event1/600frames, actual event4 idle
  launch/1200frames, player_powertrain and native include byte equality.
- final-pc-r1.py stopped before simulation on event0: that career fixture
  expects an owned stock human, but the intro is tuned. No event0 gameplay
  pass claimed. The unrelated fixture assumption was not changed; event1
  is the supported ordinary-race smoke. Failed ordinary-ug0-r1.log retained.
- Current warm Easy/Hard rerun r2 remains41.54 both, Easy2nd/Hard1st.
  These numbers are investigative driver history, not retail parity pins.
- Fresh Switch SDK build tag drag-launch-r14 and7 selected package/shader/GPU
  tests PASS, switch-build-r1.log. Hardware launch still unverified.
- Bash tracked HEAD/worktree ASCII/mode/whitespace scans PASS. Existing line
  ending conversion warnings retained; no non-ASCII source content reported.
- Source seal records the final exact dirty candidate and NRO identity.
