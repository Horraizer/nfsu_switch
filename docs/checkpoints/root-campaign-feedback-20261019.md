# Owner campaign feedback audit, 2026-10-19

CHECKPOINT
role: worker
agent: GPT-6 root solo
assignment: Audit missing world animations, NOS blur in free drive, event
briefing speech, event scroller neighbors and latest Easy drag result.
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: runtime root/build/day20261008-root-combined
saved: Existing owner integration and frozen r10 preserved. Local audit only.

Evidence: build/campaign-audit-r11/r10-run1-snapshot-r1.log and snapshot-r1.json
preserve an immutable copy while original nxlink capture continues. Do not
stop or call that run terminal: event5 was still running at this snapshot.
Three drag attempts: first two have no finish and positive launch quality;
last completed43.03 position2, NEON43.00. Trace38 events,0 drops. Player GO
physical7215.946 versus6160 target, quality-1; four requests .85,.5,.85,.85.
Easy difficulty0 confirmed by each actor setup. All rivals also had bad GO
quality; NEON later got1.0 for all three requests. Current original-backed
context offsets are0..0.01 for NEON context1, other actors context0. No new
parameter mismatch proven by this log or by the earlier prescribed-wheel
bench. The30ms loss is real; it is not evidence that Easy must guarantee win.
Original full-body acceleration and complete Easy parity remain unverified.

1. Track animations: world geometry/materials are loaded and recorded as
static track command lists. No general track animation time/UV/frame update
is wired through track_scene/track_render/race_gpu_scene. Vehicle, frontend
and drift-crowd animations are separate implemented paths. Do not claim the
world animation chunks are completely decoded or handled. Need inventory of
authored arrows, fountain/texture sequences and their original consumers.

2. NOS blur: freeride log has active NOS121..201km/h and changing charge;
boost is reaching gameplay. Deko allocates four-pass blur and prints its
setup. speed_blur.nitrous_frame is wired before present. These facts prove
neither visible blur nor original matching pixels. Owner reports no effect;
keep defect OPEN. Need establish active pass input/output, not infer a GPU
pass happened from the allocation banner. Original sparse probe exists but
is OFF in frozen r10; no hardware pixel evidence in this drive.

3. Event speech: CareerEventMedia currently stores before/after movie and
rewards only. Briefing transition invokes menu selection SFX; FrontendMusic
mixes music plus FrontendSfx, with no per-event voice request. Missing wiring
confirmed. Which original events have voice and exact stream/bank mapping
remain unverified; do not invent speech for all events.

4. Event scroller: inspected original video_2026-10-13_08-22-28.mp4 near54s;
selected Drift icon plus adjacent Circuit/Drag icons are visible. Current
career_ui.inc explicitly draws one selected MODE_ICON at y-191 and skips
runtime thumb/master_thumb templates. Catalog.available returns a list;
neighbors are missing from presentation, not proof events are unavailable.
Reference samples/contact sheet: build/campaign-audit-r11/reference-map.
Restore native scroller layout/selection and locked/available behavior from
original templates and controller; no arbitrary spacing or fake unlocks.

No game source edits, new build/upload, agents/reviewers, branch/ref move,
commit/push/merge/publication in this audit. Existing drag balance checkpoint
and before/after fixture outcomes remain. These five scopes are not closed.
next: Prioritize actual drag parameter/shift-path comparison and NOS pass
visibility; restore authored event scroller/voice and world animation paths
with bounded original evidence. Keep owner hardware iterations consolidated.
blockers: No local tooling blocker; missing original voice/event mapping and
world animation formats are research gaps, not an owner permission issue.
ownership: root retains follow-through, solo.

Owner items6/7 follow-up:
6. Effects update checks snapshot.finished before duplicate-tick suppression
and stops NOS/shift/skid mixer voices; engine update also stops on finished.
Existing race_audio_tests exercises active NOS -> finish silence. Runtime
adapter sends snapshot after simulate, including finish tick. Thus the
reported sound cannot yet be called a proven NOS-voice leak.
Found independent provisional finish cue: race_audio.cpp loads FE_MB.abk
sample30 with the explicit comment that retail mapping is unconfirmed; the
finish scheduler plays it once. This is a concrete alternative source of a
NOS-like finish sound. Extracted actual candidate sample into runtime temp
outside Git (finish-audio-output.json), using audio_dump. First request was
correctly refused inside Git; failed log retained. Second dump exit0. Do not
claim the sample is NOS or original-correct without further evidence. No
silent replacement with an arbitrary sound. Current behavior still rejected.

7. Primary PE finish controller4350D0..435369 retained earlier; completed
its tail under build/campaign-audit-r11/finish-controller-tail.asm, plus
post-input-state/post-actor-state disassembly. Authenticated executable SHA.
4352F3..43531A changes a human controller to kind3 via425FE0;435323..43532C
selects action17 on controller kind3. Original4537D0 publishes an enabled
path with explicit speed0. Existing rival finish adapter implements this;
live main human loop instead stops RaceRun.simulate once outcome is finished,
holds a fixed3-second presentation dwell, then enters generic post-race menu.
This is not a complete original player stop/result transition implementation.
Do not describe a no-op world freeze as recovered braking/stop behavior.
425FE0 constructs a new controller; original entire controller activation,
post-finish tick progression and game-mode branches remain to be recovered.

Current career result grants prize/statusWon only for first place; others
return map without win reward. Intro0 first place skips generic results and
advances through scripted movie/starter-car flow. Events1..5 otherwise use
the same Continue/Statistics/Restart menu and map return, not a fully recovered
mode/place-specific sequence. Need original victory/defeat/retry and event
movie/reward timing, using owner reference. Current drag results also reuse
circuit stats UI. Do not mark complete from the presence of a result screen.

No behavior patch/new NRO/upload in this audit. Owner left item8 blank; no
extra scope inferred. Preserve all seven confirmed gaps and bounded limits.

Owner additional items: repeat rival impacts, HIT TRAFFIC penalty, NEAR MISS,
DRAFTING/Tailgating points and speed influence. The prior blank item is now
superseded by these actual requests.

Original scoring rules loaded from GLOBALB34A07:
-15 HIT TRAFFIC: negative rule, rate-50, minimum-15, no positive multiplier.
-22 NEAR MISS: rate100, multiplier limit5/step1. This is not an arbitrary
flat award to every proximity sample; original43F630 maintains a pass state
using actual collision/proximity records, speed/lifetime and other gates.
-26 DRAFTING: continuous rate25 and minimum25 with timing gates. Original
43FB0A..43FB44 reads actor47C, speed>20 and drafting>.25 before trigger.
Current session leaves drafting0, no trigger15 or22 exists. Rule loading
and presentation alone are not implemented traffic/drafting producers.

Fresh original43FBB0 boundary witness records nine supplied impulse/age/role
cases in traffic-style-native-r2.json. Real car predicates execute; only
style-trigger/reset consumer calls are observed at the consumer boundary.
Role3 qualifies, role2 does not; factor=min(record60/40,1). Ages400/401 ticks
are refused. Native5 equality reaches the trigger, correcting any shorthand
saying strictly greater5. Point minimum/publication lives in the separate
style kernel, so a trigger factor is not proof that every touch costs50.
Attempt r1 ended outside declared range at ret43FD11; r2 extends the allowed
instruction end through43FD14 and passes. Preserve both, no false pass.

Drafting speed effect: native479160 writes aero_cache+C=1-actor47C. Four
supplied drafting values0,.25,.5,.75 give drag scales1,.75,.5,.25. No hooks;
engine/body scales stay1 in this supplied closed race-scale context.
Evidence: drafting-aero-native-r1.py/json/log; executable hash authenticated.
This is a drag-resistance reduction, not an invented additive speed boost.
Current VehicleModel keeps aero drag_scale1 and RaceStyle sample drafting0;
there is no live physical/scoring draft effect. Full426300 pair geometry,
426450 world accumulation and live producer timing remain to be restored.

Disassembly evidence: drafting-pair426300..42644A, aero-scaling479160..EA,
traffic-style-admission43FBB0..43FD11 and drafting-style43FB10..49. The
broad style-producers.asm decode stopped early after107 instructions and
must NOT be presented as full43F130..FD30 coverage. Older skipdata traces
are navigation only; exact entry43F630 and individual decoded instructions
establish the cited near-pass path, not a native whole-pass verdict.

Repeat rival audio: reproduced an actual current scheduler omission with
repeat-impact-probe.cpp/exe, linked current production libraries. Same car
pair stays touching with zero impulses between two20-unit impacts59 frames
apart. Both produce nonzero-gain impact events, but only the first has
start=true. Service only starts its one-shot on that flag, so a finished
voice is not restarted by the second event. Logs repeat-impact-r1.log and
repeat-impact-build-r1.log. Pair.impacting never clears while pair touches;
per-actor constant pair identity and the36/52-step episode approximation are
port policies, not fully recovered original pair/voice admission. This is a
concrete reproduction of missing replay, not proof every solver impulse must
play a sample or that every owner-reported missed hit has this cause.
Need original re-admission/lifetime gates before a supported implementation;
retain native subtype and bank graph work. No automatic per-tick sound spam.

No gameplay behavior edits or new NRO/upload in this audit. Native examples
and one production scheduler reproduction only; no all-gates/review claim.
All original parity/owner acceptance criteria remain open as listed above.


2026-10-19 correction: the opening r10 audit paragraph's claim that selected
UP0..0.01 for NEON was original-backed was wrong. Actual476E0D maps +2C/+30
to UP C8/CC, not +24/+28. Also context0 goes through476EC0 to row0 rather
than retaining constructor defaults. Corrected production decoder and native
constructor regression are recorded in root-drag-career-20261019.md. Earlier
RNG arithmetic tests did not verify that field mapping. No claim of complete
drag balance parity; original Deck benchmark and remaining RNG limits there.
