# Drag mechanics correction, 2026-10-18

Owner accepted r5 startup but rejected RPM/speed/lane mechanics and Easy
balance. Scope: compare original code and correct these mechanisms together.
User keeps solo workflow and minimal meaningful tests; no new reviewer/chat.

r5 Switch log reached play/finish on event4. First abandoned after931 steps;
second finished43.88, third42.45, player4th; rivals41.60/41.88/41.90. Profile
0 and selected contexts0/1/0 are logged. No load failure recurred. This closes
only the reproduced GPU startup binding defect, not mechanics acceptance.
Logs/receipts preserved under build/drag-gpu-r5. Runtime GPU max12.63ms and
no driving-work frame over25ms in the final run; not original parity evidence.

Original comparison in progress, build/drag-mechanics-r6:
-46DFA0 multiplies clutch derivative by gearboxDC and positive shift quality;
 negative quality with requested gear>=3 uses0.35. Port always used base rate.
-476DCC initializes AI DC from6B70EC by PROFILE difficulty:0.4/0.8/1.0.
 Port rival selector1 currently uses1 for every profile. Human D8 quality
 window3/1.5/1 is already selected; do not replace it with AI DC.
-476DFE only overrides AI random shift offsets when selected context>0,
  <=9; reads original GLOBALB34B00 row+24/+28/+2C/+30. Port uses default
  [-100,0] for every actor. Need authenticate initialized rows and consumers.
-Do not force gearbox control60=3 merely because drag is active: original
 4615E0 distinguishes manual clutch scheme3 from ordinary auto clutch. Earlier
 chat shorthand describing a wholesale special drag gearbox is incomplete;
 correct only original-backed paths after following actual dispatch.
-Existing held-lane helper covers45D800/45D970/4605E0 assuming7003D8=1.
 Need verify actual lane basis/availability and path publication against
 original, plus HUD shift cues and physical RPM. No arbitrary slowdown added.

Main checkout and staged GLM integration remain untouched. No source push,
merge or new upload. Next: implement confirmed clutch/difficulty omissions,
compare bounded native original cases and diagnose lane/HUD integration.


Implemented original46DFA0 quality multiplier and AI profile request scale,
plus46D760 offsets selected by476DFE's context>0 policy. No manual-clutch
control60=3 forced: dispatch distinguishes that optional control scheme from
normal drag shifting. Existing human quality D8 window remains3/1.5/1.
VehicleSpec retains immutable per-actor original tuning; ordinary default
quality0/request scale1 and ordinary random bounds remain the same.

Additional original lane defect confirmed:45C59F reads actor24 controller kind
2 for drag, not actor28 physics selector1. Port incorrectly passed1, invoking
ordinary zero lateral step below60mph or5s. Corrected to2: original7061DC
curve gives5..1 per path sample. Existing nearest lane/exclusion helper matches
28 original456E90 cases; no arbitrary new lane sensitivity added.
Human path now retains the previous published geometry via45AE10's retained
branch until press/release/category change or road reselection dirties it.
45CFF0 also takes45AE10 directly for an existing lane descriptor. Prior code
rebuilt the human path every frame and restarted its lateral transition.
Ordinary and rival path strategy not expanded in this change.

Evidence: tools/drag_mechanics_native.py authenticates executable+GLOBALB;
no semantic hooks. native-r2.json contains120 original clutch derivatives,
12 original shift RNG cases on actual context bounds,28 nearest-lane cases,
12 controller2/3 offset curve cases. Synthetic state/straight-road boundaries
are explicit; not an original whole-race trajectory or live shared-RNG proof.
Original disassembly under build/drag-mechanics-r6 includes controller,
constructor/dispatch, gearbox, lane and path consumers.

Intermediate player and road tests PASS. After clutch/tuning changes, before
final lane correction, CIVIC full fixtures on all3 difficulties PASS; Easy
fixture now wins. That is a driver fixture, not hardware balance acceptance.
Final current-source PC checks and fresh SDK build are running. Do not deliver
an intermediate artifact or call the whole drag mode original-equivalent.


Final current-source checks:
- cmake --build build --target nfsu_player_powertrain_tests nfsu_ai_road_tests
  nfsu_ai_action_tests nfsu_career_races_tests -j6: PASS, build-final-r1.log.
- Direct player_powertrain,ai_road,ai_action targets: PASS, *-final-r1.log;
  new native clutch/RNG/nearest/offset cases are included, zero failures.
- NFSU_DRAG_LANE_PROBE=1 NFSU_CAREER_TEST_DIFFICULTY=0
  nfsu_career_races_tests RETAIL4 1800: PASS. Same actual CIVIC/three-rival
  session handles left press/release and right press/release on the authored
  straight; lane-drive-final-r1.log. This validates requested side movement,
  not original whole-controller transient equivalence.
- NFSU_CAREER_SCENE_CHECK=1 nfsu_career_races_tests RETAIL4 6000 with profile
  difficulties0/1/2: PASS. Full command validation includes all GPU resources,
  reflected geometry, particles/glows and HUD. Bounded test driver completes
  all3; Easy wins, Medium/Hard lose. No arbitrary opponent slowdown was added.
- build/drag-mechanics-r6/build-switch-drag-mechanics-r6.sh: fresh SDK build
  exit0, selected package/shader/GPU-contract checks7/7 PASS, switch-build-r1.
- Bash ASCII/entry-mode/whitespace scans PASS, ascii-r1.log. Source snapshot
  scan/readback verifies all current source bytes and new path names.

Exact NRO/source identity is in build/drag-mechanics-r6/mechanics-r6-receipt.json.
The candidate includes earlier difficulty/dirt/shadow and r5 startup binding
correction. New changes are original-backed clutch quality/profile/context
parameters, controller-kind lateral step and retained human drag path.

Remaining acceptance: owner Switch RPM/speed/left-right behavior and Easy
balance. No original complete race trajectory equivalence, original global
RNG-stream identity, manual-clutch input scheme, entire drag HUD parity,
Default/Release full-matrix, review/merge/publication claim. In particular the
existing missing authored shift-light/progress presentation is still a known
HUD debt, not fixed by physical clutch/steering corrections. No claim that
full drag mode is finished. Preserve deferred other scopes and all failures.

Owner explicitly deferred Netloader upload of mechanics-r6: do not send until
fresh owner authorization/readiness. Sealed NRO and source receipt are ready
under build/drag-mechanics-r6; no r6 upload process was started. Next external
step is the deferred hardware mechanics/balance test. Source ZIP predates
this upload-pause-only checkpoint. Preserve r5 runtime logs and all failures.