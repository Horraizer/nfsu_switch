# Campaign results follow-up checkpoint - 2026-10-19

role: worker
agent: GPT-6 root solo
assignment: owner remaining campaign feedback; latest owner asks what remains open
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: local-only; accepted r14 and sealed unsent r15 preserved

No production changes, build, upload, commit, publication or ref movement in
this follow-up. Netloader answer was a scope question, not readiness consent.
Read-only native/FNG research is in build/campaign-results-r16. This research
is static evidence, not native runtime execution or restored production UI.

Confirmed remaining implementation gaps:
- Switch post-race flow goes straight to PostRaceMenu, except UG0 victory.
- Original49E100 builds an ordered page list: PostRaceResults (6FB98C),
  conditional StyleAnimator (6FB994), conditional RewardPopup (6FB990),
  then conditional PostRaceMenu (6FB998). UG0 and tournament conditions apply.
- Original49E0B0 skips StyleAnimator for UG0 and mode4; otherwise it checks
  integer-converted player result field+BC. Need establish that field fully
  before binding the condition. Do not invent a per-mode score predicate.
- Existing PostRaceStats circuit binding is reused for drag. Original49C820
  dispatches49CC70 for drag,49CD90 for drift,49CEE0 for circuit. Drag branch
  binds finish time, time behind, top speed and two additional fields via
  49DE00/49DE80; their identity/telemetry must be recovered before editing.
- Authentic INGAMEB FNG offsets: results161E0C, stats16368C, menu164F0C,
  style16708C. Direct bounded object iteration finds84/87/55/46 objects.
  Older generic feng_objects research helper only reports subsets here;
  do not treat its8 result objects as a complete package.
- Native page strings and static producer instructions recorded in
  entry-49e0b0-r2.asm, entry-49ed00-r2.asm and entry-49e0c9-r1.asm.

Other r15 limits remain as recorded in its immutable receipt: console NOS
appearance/audio/new-texture/performance acceptance; speech random variants;
map pulse/interpolation; finish cue identity; non-texture world animations.
Complete traffic/rival collision fidelity and earlier deferred tachometer,
acceleration, drift HUD and residual frame spike are not claimed complete.

Next: use original result-page bindings and owner references to implement
mode/place-specific results and drag statistics, preserving r15 seal. Any
new candidate needs focused UI/runtime/SDK validation and its own new seal.

## 2026-10-19 owner continuation: results, drag statistics, rival shadows

Owner authorized a single consolidated candidate after restoring the result
sequence and drag statistics, and comparing rival shadows with the original.
No reviewer/worker dispatch or Netloader sending. Sealed r15 is immutable.

Local edits now load the original results, style and cash-reward FNGs alongside
existing menu/statistics pages. Original49E100 page ordering and UG0 victory
bypass are preserved. Drag uses49CC70's five fields: finish time, time behind,
top speed, perfect shifts and percentage. Completed player gearbox changes
are counted in the physics/session path, not sampled by the HUD. Stock career
style reputation clamps to1 in4ACE64; tuned career reputation is not invented.
Original style-category0.5/1.75/2.55-second clocks, multiplier and total stages
are being wired with skip-to-total then Next. GLOBALB34A08 supplies the gauge
thresholds. English reward language477A6F0E/2CCB70AA is used, with authored
457-unit text wrapping. Persistent style bank adds save schema2, while schema1
remains readable. Rewards/progression are consumed after Continue; Restart
creates the existing fresh whole race and does not duplicate cash.

Original owner reference video2026-10-13_08-28-47 confirms the results panel,
style page/gauge and return to Underground Menu. Changed completed event1..5
continuation accordingly. This is not a claim of restoring the result camera
or every tournament/unlock presentation; current campaign scope ends at opening
its first tournament, and stock owned cars remain the supported career setup.

Rival-shadow finding: original56ED31 overwrites each transformed grid point Z
with the common render-origin Z before road queries. Port used transformed Z,
so pitch/roll could select another stacked road through440A40's z+1 bound.
Corrected this render-only query domain, with a tilted-car regression. Native
56EB33..56ED80 supplied bounds/matrix witness records all16 XY/UV points in
shadow-grid-native-r12.json. Earlier stock bounds/texture/mip/alpha witnesses
remain useful but are not console appearance acceptance. No opacity/size tuning.

Initial PC build and seven fresh UI fixture images passed before the latest
style/save/shadow changes. Screenshot contact sheet inspected. Latest dependent
PC rebuild is in progress, build-pc-r2.log. Pending: finish targeted checks,
actual drag result binding, original numeric style/stock-shadow comparisons,
new SDK build/package gates, ASCII, immutable combined source/NRO seal. No
new source has been committed, published or sent to hardware.

## Final software checkpoint: campaign-results-r16

Implementation now covers the first-five normal result flow: original results,
conditional style counting/gauge, cash and authored car/track reward pages,
post menu/statistics/restart, then Underground Menu. UG0 victory keeps its
separate movie path. Knockout/blown-engine results cannot become career wins
because of their last live progress rank. Restart retains the existing fresh
session path and does not award cash/style to the saved profile.

Additional original data check: event1's kind4 reward emits no popup in49E676;
event2 has none; event3 unlocks Drift Track1; event4 unlocks2003 Mitsubishi
Lancer ES and reverse14th & Vine; event5 unlocks reverseOlympic Square.
The earlier verbal Focus identification was wrong and corrected. Production
resolves the car hash against the original table; the failed Focus expectation
was in a test, not the production lookup. Original49E420 language/templates
and reward-record order are retained; caret line breaks use the authored457
unit wrap width. Authentic unlock notice frames were inspected.

Drag statistics now use the original five fields and native result/gap time
formats. Leader results use elapsed time, following rows use signed gaps.
Stock career style multiplier is1 as4ACE64 clamps the stock zero visual score.
V1 saves read with zero prior style bank; new writes use schema2 and preserve
style progress with the existing alternating/checksummed save mechanism.
Older binaries do not understand schema2; no SD saves have been modified here.

Verification evidence (local uncommitted tree, not independent review):
- PC build-pc-r9.log and build-career-r10.log: PASS, all required current targets
  compiled. Earlier compile failure and fixture failures retained.
- pages-final-r3.log: PASS, real original FNG CPU frames/GPU quads, result-style-
  reward-menu order, cash/car/track queue, skip-to-total and27 original numeric
  style cases. Native49F1B0 comparison declares only an external string-format
  sink stub, not whole FEng execution. The7000-tick negative-count boundary
  exposed a real clock-domain difference; production now uses binary32 tick
  conversion and passes. Failed earlier evidence remains in final-r1 logs.
- career-final-r4.log: PASS, original reward messages, loss/win continuation,
  style-bank persistence, v1 migration and interrupted-write recovery. The
  initial v1 fixture used the wrong hash-letter case and was corrected.
- shadow-final-r3.log: PASS, original cached-ground cases, five original grid/
  alpha supplied poses and tilted-car query-height regression. Native witness
  source: shadow-native-r21.py/json, authentic56EB33..56ED80/56EFD9 slices.
- stock-shadow-native-r22.log/json: original selected CIVIC/MIATA/NEON/GOLF
  bounds agree bitwise; authentic solid headers/reducer, no call hooks.
- drag-result-final-r3.log: PASS, actual loaded drag through finish with real
  shift counts/ratio and fields, plus physical finish braking. No acceleration
  or difficulty tuning changed. Circuit scene-final-r3.log PASS includes live
  animated texture aliases and all complete GPU command bindings.
- career-ui-final-r4.log: PASS, six real frontend submissions. Earlier absolute
  Unicode output argument failed in the native test utility; successful smoke
  uses an ASCII relative output path from this actual workspace.
- switch-build-r3.log: final production SDK build and selected7/7 package,
  embedded shader, speed-blur and GPU-contract checks PASS.
- Full Default/Release matrices remain deferred under owner's minimal-test
  instruction. No reviewer dispatch, commit, push, merge, publication or upload.

Acceptance still requires Switch: finish flow on win/loss, drag statistics,
style save/reload, rival shadows on slopes/stacked roads, and r15 NOS/audio/
animated-texture appearance/performance. This restores first-five stock-car
page/data flow; it does not claim original result-camera choreography,
tournament reward screens, tuned career reputation, rival-stat tab controls,
or complete collision fidelity. Existing deferred backlog is unchanged.

Next: seal this source plus the final tagged NRO, confirm current Netloader
readiness, send that exact artifact and capture the hardware session log.
