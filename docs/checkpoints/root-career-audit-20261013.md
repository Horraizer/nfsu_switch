# Career continuation audit, 2026-10-13

Owner assignment: inspect three new original recordings, career garage/map,
GLM drift restoration, drift handling, difficulty and complete movie triggers.
Solo inspection; no delegated workers/reviewers, source publication or upload.
Current candidate: f0d720b517d039614a2d19d829b5fcfc42c4562f.
Owned tree: build/day20261008-root-combined, codex/draw-distance-original.
Evidence: build/career-audit-20261013 in that tree. Local-only checkpoint.

## Viewed references

Contact sheets sampled each owner clip at five-second intervals. These show:
- 08-22-28: starter car selection/purchase, movie, then career race map.
- 08-22-59: map/event selection and Easy/Medium/Hard rewards, drift start/play.
- 08-28-47: drift results/reward, map, Underground menu, Customize Ride movie,
  then customization garage. Video order differs from attachment order.
No frame-by-frame input reconstruction or isolated sound comparison performed.
Input identities are in owner-media.json. Screenshots are ignored local media.

## Current implementation boundaries

FrontendAction has underground/free_drive/exit only. Garage geometry and
frontend rendering are reusable, but starter purchase, career map menu,
customization menu and persistent career profile are not connected.
Switch UG0 wins play the fixed 02_FAN_s movie and return to the main menu.
RaceEventTable parses prerequisites and availability data; this is not a live
career screen or progression/save implementation.

Difficulty is not exposed as an owner-selectable Easy/Medium/Hard game option.
Event0Descriptor defaults to index1, selecting original rival contexts from
three rows. Those contexts reach IntroAiDriver and its retail speed settings.
The separate ai_catchup_update implementation has no production caller in this
candidate. Do not claim complete original difficulty behavior from parsed rows.

GLM drift commit 6c987ce6c2f4acce7c1a38eee0681c32ffea864e is on
origin/codex/day20261012-glm-drift1. git merge-base --is-ancestor against the
candidate returns1: it is NOT in the last owner NRO. Read its source diff;
restores event3 / track1301 / L1RB assets, selection, track loading and derived
route. No drift-specific vehicle-force or score implementation is added.
GLM checkpoint reports a completed four-lap desktop route and targeted gates;
those are author-reported results, not a new independent runtime verification.
Its full suites retain documented baseline failures. Do not call them all green.
No fresh drift build, original handling parity check or Switch test performed.

## Original movie system

Authenticated speed.exe SHA256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Read-only chunk walk of FrontB 34A02,332-byte event records, finds movie names
at+AC (before) and+C4 (after). event-movies.json preserves all populated rows:
0:01_DAY/02_FAN;7:05_ENG;15:06_SAM;16:after07_GOD;
78:11_PNK/13_KIT;92:09_GBE;111:14_MUT/16_RES;
112:08_EDD/17_MET;113:after19_FIN. Other movie assets exist outside these rows;
this table alone is not the complete trigger policy.

4E82A2 reads735DC4: special menu-movie flag selects04_GUM_s at4E82AB.
4EB05E tests76027F. Its zero path sets that flag and735DC4, opens
MU_UG_NewCareerFMV; subsequent path enters MU_Garage directly.
4E8580 handles two completion/input events, enters MU_Garage from
MU_UG_UndergroundMenu and clears735DC4. Video supports first-customization
behavior. Persisted serialization/reset domain of76027F still requires tracing.
For an event context735DC8,4E82B2 reads event+AC; finish handler returns to
MU_UG_MapScreen and clears that context. Ordinary context starts career setup.
Post-movie constructor4E86D0 selects success/failure movie fields and special
_S/_E suffix policy using owner state. Current hardcoded UG0 wrapper covers
only the current fresh-career slice, not these general branches.

Implement one transition system using retail event movie fields, menu triggers,
profile flags, return destinations and skip/completion paths. Do not add a
separate hardcoded chain for every event, replay tutorials on every visit, or
pretend a player API alone completes this without career/save/menu consumers.
Full movie lifecycle port is NOT implemented by this audit.

## Original drift boundary

Existing native lifecycle tooling identifies78A346/7361A0 as drift mode/override.
Fresh source inspection at47854F..478561 dispatches479510 for that mode.
479510 computes heading versus velocity angle, samples779AA8, and accumulates
an additional body contribution at+120; further mode-specific arithmetic
continues. This is affirmative evidence of a mode-specific dynamics path,
not support for implementing drift by simply disabling stability. Full routine,
its inputs/tuning and scoring remain unported/unverified. No physics changed.
Exploratory context files beginning mid-instruction are not function evidence;
use the corrected drift-mode-dispatch, drift-mode-force and movie-end-handler.

## Next implementation order and limits

Preserve prior UG0 fidelity backlog (welcome, Race Over, traffic contacts/style,
lights/audio/routes, minimap color, movable props and world animation).
Career continuation requires a real profile/save and retail-driven transition
model, starter purchase, map/difficulty and garage entry; integrate movie
triggers into that flow, then integrate GLM drift assets and original drift
physics/scoring. Do not treat track availability as full race-mode support.
No NRO was rebuilt, no branch integrated, no hardware acceptance asserted.
