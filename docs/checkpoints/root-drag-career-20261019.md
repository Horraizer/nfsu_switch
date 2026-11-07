# Drag career policy and shift-table correction, 2026-10-19

CHECKPOINT
role: worker
agent: GPT-6 root solo
assignment: Owner original Deck stock CIVIC/Easy/no NOS41.35 (two Perfect
Shifts), CARL/MIATA43.27. Repeated Easy41.18, MIATA about+2s; Hard41.42,
MIATA+1.76s. Owner clarifies the player time difference is imperfect shifting,
not evidence of a physical Easy/Hard acceleration difference. Urgent original
code comparison; drag takes priority over event-message audio.
phase: implementing
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: runtime root/build/day20261008-root-combined
saved: Local owner integration; no commit/push/merge. All older candidates kept.
ownership: root solo. No workers/reviewers/new chats dispatched.

Confirmed original differences and corrections:

1. 476D8F/476DC4 compare777B4C to1 and bypass the quick-race difficulty
   overrides for Underground.46D390 initialized gearboxD8/DC to1. Our human
   D8 had3/1.5/1 and rivalDC had.4/.8/1 for all modes. Career now retains1.
   The main-menu Underground handler sets EBX1 at4B324B and777B4C=EBX at
   4B3513, selecting MU_UG_UndergroundMenu.fng. Existing raw numeric policy
   tables are authentic; applying them unconditionally to career was wrong.
2. 476E0D..476E2E copies row+2C/+30 into gearboxC8/CC (UP random bounds),
   and row+24/+28 intoD0/D4 (DOWN bounds). Decoder used serialized order as
   consumer order. This made context1 UP0..0.01 instead of-800..260RPM,
   yielding near-perfect automatic shifts. Corrected ai_speed_configuration.
3. 476DEA branches for context<=0 to476EC0, setsECX0, then compares EAX
   againstECX at476DFE. Context0 therefore DOES apply row0; negative contexts
   do not. Prior port comment/condition>0 was wrong. Context0 now receives
   UP-900..300 and DOWN0..0.01 from the real table, not default-100..0.
4. 44BD42..44BD56 returns before ordinary catchup updates in drag. Live
   planner omitted this gate; now passes drag special context. Existing
   drag rivals use selector1 and the physical scale setter is gated on
   ai_physics_, so prior logged catchup values alone do NOT prove a live
   drag engine boost. This correction is not claimed as the timing cause.

Native evidence, build/drag-message-r12:
- career-native-r2.py/json:12 original bounded constructor policy cases,
  modes1/2, roles1/2, difficulties0/1/2; actual44BD10 drag early-return case.
  Constructor context getter0 is explicitly supplied at interface; all
  actual policy instructions execute. Seeded D8/DC defaults are separately
  visible in authentic46D390. Attempt r1 omitted allowed476EC0 branch and
  failed; retained. r2 is the passing run, not r1.
- tools/drag_mechanics_native.py now executes original476D6C..476E34 with
  initialized real GLOBALB rows and supplied context getter0..3, then captures
  the actual C8/CC/D0/D4 fields. No constructor arithmetic is replaced.
  mechanics-native-r3.json/log:4 constructor rows,120 clutch cases,12 RNG,
  28 nearest-lane,12 offset cases PASS. Constructor getter calls disclosed.
- tests/drag_mechanics_native.inc updated only from these real consumers;
  tests/ai_action_tests.cpp --retail-drag RETAIL compares compiled decoder
  with those original fields on the actual retail GLOBALB chunk.
- Earlier r6 RNG witness seeded C8/D0 from an assumed field order and treated
  context0 as default. It checked RNG arithmetic, not the constructor mapping,
  and did not support the earlier claim that live selected offsets matched.
  Preserved earlier evidence, explicitly superseded that mapping claim.

PC checks:
- cmake --build build --target nfsu_career_races_tests nfsu_ai_action_tests
  nfsu_player_powertrain_tests -j6:PASS, build-pc-r2.log.
- nfsu_ai_action_tests --retail-drag RETAIL:4 constructor rows,failures0.
- nfsu_ai_action_tests: existing offsets/publications/owner/fallback/contact
  cases failures0, action-pc-r1.log.
- nfsu_player_powertrain_tests:PASS, powertrain-pc-r1.log.
- nfsu_ai_catchup_tests RETAIL:13 cases and32 history steps,failures0.
- Actual PC event4/three-rival runs on Easy/Hard, same stock CIVIC, controlled
  launch and first-green manual pulses:41.76 on both, position1 Easy and3
  Hard. Green-first is a bounded driver, not an owner reaction-time model.
  Logs green-difficulty0/2-r2. The intermediate policy-only runs-r1 left
  the offset mapping wrong and are NOT the final result.
- Diagnostic delayed-player runs reveal AI finish times without stopping at
  the player's earlier finish: Easy CARL42.54/KLUTCH42.04/DARYL42.93;
  Hard CARL41.62/KLUTCH41.72/DARYL42.25. Player deliberately lifted after
  GO and did not finish within fixture budget; these are AI observations,
  NOT successful player finish tests. Initial delayed-player-r1 suppressed
  launch entirely and failed its boost assertion; r2 restored initial gas.
- Existing career scene/HUD probe: neutral/first tint, restart of all three
  lights, all four shift labels and five actual upshift events PASS, plus
  real GPU draw command validation. career-scene-hud-r3.log. r1 failed narrow
  environment path conversion for an output under Unicode workspace; r2
  omitted creation of output directory. r3 creates ASCII runtime-temp output
  and runs from the real workspace. Both failed attempts retained.
- Fresh Switch SDK build tag drag-career-r12 and7/7 selected NRO/shader/GPU
  contract checks PASS, switch-build-r1.log. Full Default/Release matrices
  deliberately not rerun under owner's focused-check workflow.

Limits and next:
- This fixes original-backed constructor/consumer mistakes; it is NOT proof
  of a complete original body/tyre/track acceleration trajectory.
- AI times are still not the owner's original43.2 benchmark, especially Hard.
  No arbitrary AI slowdown or time-fitting coefficient was introduced.
- Runtime gearbox RNG remains per-car seed12345, whereas original46D760
  consumes the shared world RNG. Whole constructor/driver RNG call order and
  actual original GO state are not reproduced by the isolated witnesses.
  Do not claim exact original difficulty or racing-time parity yet.
- Frozen candidate needs an owner Switch comparison before acceptance; no
  fresh Netloader readiness assumed and no upload performed in this turn.
- Owner message clarification retained: drag briefing is Klutch, and event
  messages must play their existing voice clips. Found HEAD_KLUTCH plus
  SpeechStreams/STRM_English_Speech_MB.ast and its STRMBANK ABK. Current
  CharacterName/portrait still hardcoded Dirt; voice wiring not implemented.
  This remains open after the priority drag correction.
- Original initial briefing disassembly4C5AC3 reads event+130 character ID;
  mapping helper4C55F0 and4AD7B0 remain to decode. Do not hardcode one sender
  for every event or invent voice for an event without an authored entry.
