# Shared drag RNG continuation, 2026-10-19

Owner clarified: an original player-view video cannot show rival shift inputs.
No rival-shift evidence is requested from that video. Continue from original
code. Earlier video request was for player RPM/launch only and is withdrawn
as a requirement for AI analysis.

Root solo continuation of root-drag-career-20261019.md; same owner worktree,
branch/head, no commit/push/merge, no reviewer or chat delegation. Earlier
r12 frozen source/NRO remains immutable. r13 is the combined current candidate.

Original46D760 reads RNG at WorldSimulation+2D0 through7361F8 and writes it
back after each draw. Planning and every gearbox share that state. Current
VehicleModel::reset seeds every gearbox12345; subsequent gear commits used
separate identical per-car streams, disconnected from the planner world RNG.

Live drag gear commits now consume the planner's race-owned stable RNG state.
RaceRivalPlans/VehicleInput carry a non-owning pointer into that stable heap
state, never into a transient plan. It is used only at player_gear_update's
actual phase1-to2 randomization and publishes immediately after both draws.
Human, three rivals and AI planning therefore share the same stream; retained
contact input points at the same live state. Calls with no shared state retain
the existing standalone/ordinary behavior. There is no per-frame RNG draw,
allocation, file I/O, timing-fit coefficient or speed cap in this change.

Lifetime: RaceRun owns both session and planner. Planner State is created
before the first plan and remains stable throughout the live race; a new
load replaces it only as the race is rebuilt. Destructing a session does not
advance physics or dereference its retained input RNG pointer. No pointer is
serialized, hashed as an address, or copied into the save profile.

Native regression: tools/drag_world_rng_native.py executes authentic46D760
for two boxes in A/B/A order with one supplied world seed12345 and actual
context0/context3 bounds. Six draws, no hooks; native-world-r1.json/log.
Compiled player_powertrain regression exercises the real phase transition
with that same shared state, compares all offsets/state words to the native
outputs and verifies a completed clutch consumes no random value. PASS.
This detects the prior separate streams; not a cloned implementation oracle.

Source changes retained from r12: original UP/DOWN constructor field order,
context0 row handling, Underground D8/DC defaults1 and drag catchup bypass.
The bypass corrected redundant state updates; it was not a demonstrated
selector1 engine boost. Do not repeat the earlier stronger hypothesis.

Checks/evidence under build/drag-world-r13:
- build-pc-r2.log: current career/player-powertrain targets compiled PASS.
- powertrain-r1.log: existing checks plus shared native A/B/A PASS.
- green-difficulty0/2-r1.log: same stock CIVIC controlled-launch/first-green
  driver41.76 both. Easy position2, KLUTCH41.73; Hard position1, rivals not
  yet finished. These fixed-input fixture results are NOT the original
  player's two-Perfect-Shift trace or a guarantee of winning Easy.
- career-scene-hud-r1.log: actual event4 draw/resource validation, original
  neutral launch/boost, neutral/first/light/tint/all4 labels and5 actual
  shift events PASS. Existing default test driver42.19,position2; not a
  timing parity assertion. No old output files reused; fresh temp folder.
- Fresh SDK tag drag-career-r13 and selected package/shader/GPU checks are
  recorded in switch-build-r1.log. Final artifact receipt records completion.
- First apply script refused an ambiguous header match before any source edit;
  apply-r2.py uses full unique signature and completed. No partial write.

Remaining limits:
- Original full scene startup/RNG draw order and initial world seed are still
  not recovered. Planner retains its existing explicit deterministic port
  seed. Shared-stream consumption restores a known original dependency;
  it does not establish the exact original game's random sequence.
- Rival times cannot be compared as fixed constants independent of player
  shifting: the original shared stream is consumed by both. Earlier r12
  delayed-player timing observations describe r12 only, not r13 predictions.
- Full original body/tyre/track acceleration and Easy/Hard gameplay acceptance
  remain unverified. Owner's original41.18..41.42 and MIATA about43.2 remain
  external reference observations; no source constant was fitted to them.
- No console upload this turn. Need owner Switch comparison of the combined
  candidate before calling drag difficulty accepted. No fresh readiness
  assumed, no old nxlink process polled or stopped.
- Klutch briefing identity/voice, other campaign feedback debts remain open;
  their inputs were located, not implemented by these drag corrections.
- Full Default/Release matrices not rerun under owner's focused-check request.
