# Interface audit of r98

Audit date:2026-11-03. Worker: GPT-6 root solo. Owner assignment: audit the
interface before/during loading, after races, menus and loading hints.
This is a source/data audit with bounded desktop observations, not an
independent PR review or a new console acceptance. No game implementation,
save, original executable or NRO was changed in this audit.

## Source and method

The audited implementation is the delivered r98 source snapshot identified by
build/fidelity-r98/root-source-freeze-r98-r3.json. Existing source files were
checked against that manifest before the audit. Git/index/refs/publication
HOLD remains in force. The committed HEAD alone does not identify this dirty,
combined implementation; use the snapshot manifest.

Original input: PC1.4 USA speed.exe, SHA256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Original executable, GLOBALB, INGAMEB, FrontB and English language resources
were read only. Fresh Ghidra decompilations and a direct FNG/tip-table inventory
were used. The desktop probe links the existing r98 Release libraries and
calls the actual FrontendArt, CareerCatalog and RaceResultStatistics APIs.
It does not open the original game or operate owner profiles.

Ignored evidence and reproducible scratch programs are under
build/ui-audit-r99, prefixed root-. The audit intentionally does not repeat
whole-project builds or the previous full CTest runs: implementation code
has not changed. Those runs cannot establish complete UI fidelity anyway.

Status terms:

- CONFIRMED: a current source path or desktop observation demonstrates the gap.
- PARTIAL FEATURE: implemented presentation is a subset of the original.
- UNVERIFIED: evidence is insufficient to call the current behavior wrong.
- INTENTIONAL: an owner-approved adaptation or parked feature.

## Finding index

| ID | Priority | Area | Finding |
| --- | --- | --- | --- |
| UI-01 | P1 | In-race/exit | Pause/quit confirmation is absent; technical result can escape into normal flow |
| UI-02 | P1 | Underground | Ranking and career Statistics rows have no screen |
| UI-03 | P2 | Loading | Original conditional tip/controller presentation is absent |
| UI-04 | P2 | Career transitions | Several 2D panel timelines are evaluated as settled snapshots |
| UI-05 | P2 | Post-race statistics | Sprint receives circuit/lap fields |
| UI-06 | P2 | Post-race statistics | No selected-racer browsing or rival detail values |
| UI-07 | P2 | Post-race audio | Navigation/page actions have no frontend sound route |
| UI-08 | P2 | Shop/save | Ordinary confirmations and save presentation remain simplified |
| UI-09 | P2 | Magazines | Cover album exists; complete original browser/award UI does not |
| UI-10 | P3 | Pad help | Screen/action-specific Switch hint coverage is incomplete |
| UI-11 | P2 | Main frontend | Options/statistics/profile frontend remains restricted |

## Prioritized findings

### UI-01 / P1 / CONFIRMED: no normal pause and quit-confirmation flow

Scenario: during an ordinary race, press Y or Plus. Y toggles the simulation
pause flag. Plus breaks the race loop directly. There is no normal pause
screen with Resume/Restart/Options/Quit and no intervening quit confirmation.
An unfinished normal race can then reach the technical result fallback,
including the build, step, performance and outcome rows.

Source: switch/race_main.cpp:852,854,872,1582; show_result at394..470.
The original has MU_Pause_PC.fng, MU_PauseOptions_PC.fng and
MU_PauseConfirm.fng. These are not wired into the current race loop.

Correction scope: restore the original pause/confirmation state owner, adapt
its controls to Switch, and keep ordinary cancellation/error presentation
out of the diagnostic result screen. Preserve a separate explicit diagnostic
entry for profiling.

### UI-02 / P1 / CONFIRMED: Underground ranking and career statistics are inert

Scenario: in the Underground menu select UGRanking or STATISTICS and press A.
The menu renders and highlights those entries, but the dispatcher falls back
to "Not available yet". They have no destination screen.

Source: src/career_ui.inc:218 and switch/race_main.cpp:2247..2253.
Original: MU_UG_RankScreen.fng and MU_UG_StatsScreen.fng;4EAFF0 routes their
screen selection. This is separate from the functioning post-race statistics
panel and from the points used to open campaign events.

Correction scope: restore the two actual screen/controller paths and their
ranking/statistic data. A selectable row with a toast is not the screen.

### UI-03 / P2 / CONFIRMED: loading tips and controller-card branch are absent

Scenario: a load for which the original has an enabled gameplay tip. The port
always renders the generic PC_LOAD/loading-bar branch, without the selected
message, its picture or the controller-card fallback.

Source: src/frontend.cpp:293..369; RaceLoadingArt has no event/profile/tip
context and retains only the generic artwork. The original PC_Loading.fng
also contains Game_Tips and the associated message objects.

Original source:4DD290 selects the presentation branch;4DF710 chooses an
eligible tip by enabled state, display limit/count and priority;4DDFA0 binds
its image/text or routes4DE0E0 to the controller card. The6FA290 catalogue
includes nitrous, drift, drag, tournaments, trade-in, reputation and other
career topics. This is conditional selection, not a random tip on every load.
The current implementation has no matching tip-selection/show-count owner.

Correction scope: restore tip eligibility, counters, presentation and any
continue gate; adapt controller images/bindings to the actual Switch controls.
Adding arbitrary text over the generic bar would not restore this mechanism.

### UI-04 / P2 / CONFIRMED: several career panel transitions are settled snapshots

Scenario: enter the event map, Underground menu, difficulty selector or garage
and let its 2D panel animate. career_objects applies Init/intro/FadeIn and
highlight states at load; draw_career later selects the resulting objects
without evaluating the career state timeline from the current UI clock.

Source: src/career_ui.inc:25..59,204..225,570..571. Contrast the explicit timed
sampling in draw_welcome and draw_post_race.

Evidence: the current desktop probe records identical 2D vertex submissions
before/after advance(.25F) for these pages. The original FNG inventory contains
timed Init/intro/fade-out/SlideIn/highlight states, including map LAPSNUMBER
and difficulty Heads. This is a concrete transition gap. It does not mean
the main-menu animation, car motion, garage camera, welcome or style counter
are all static; those have separate time owners.

Correction scope: retain and run the appropriate original FEng states for
entry, selection and exit, with events and per-screen lifetime ownership.
Do not apply a generic fade to every object.

### UI-05 / P2 / CONFIRMED: sprint statistics use the circuit field set

Scenario: open detailed statistics for a sprint. The actual current API on
event2 reports RaceMode=circuit, DataName_3=BEST LAP TIME: and
DataName_7=LAPS LED:, although its route is point-to-point.

Source: include/nfsu/race_result.h:81..119,148. The only mode alternatives
are drag and drift; the remaining report is treated as circuit.

Original49CEE0 checks the track's mode-mask4 branch. That branch omits the
best-lap/lap-list path and binds finish/gap/top-speed/average/trip through
49D4C0,49D5B0,49DB30,49DC20,49DD10.49DA50 is the separate BEST LAP TIME
binding. The authenticated English keys confirm these labels.

Correction scope: add the actual sprint/point-to-point statistics binding and
visibility rules. Time-trial field selection needs its own original check;
this finding does not assert that every time-trial field is already wrong.

### UI-06 / P2 / CONFIRMED: detailed statistics cannot browse other racers

Scenario: after a multi-car race, open Statistics and request another racer.
The port has only player values. The statistics page accepts A/B to return;
it has no previous/next racer input or selected-racer state.

Source: include/nfsu/frontend.h:135..137, include/nfsu/race_result.h:31..40,59,
and switch/race_main.cpp:1445..1460. Results-table rows for opponents do not
supply their full statistics to this screen.

Original49CBD0 changes a clamped racer index;49C820 binds the indexed race
record and enables/disables the previous/next controls.49BEE0 initializes the
selected index. The current renderer also filters mouse_over_L/R rather than
replacing that browsing function with Switch controls.

Correction scope: retain the required per-racer statistics, restore index
selection and correct boundary visibility, and expose an appropriate pad hint.

### UI-07 / P2 / CONFIRMED WIRING GAP: post-race navigation has no FE sound route

Scenario: move between Continue/Statistics/Restart or change post-race pages.
The loop changes the UI state without dispatching a frontend up/down/select/
back cue. The main and garage menu loops do dispatch those sounds.

Source: switch/race_main.cpp:1445..1475 versus1863..1866 and2155..2179.
The race-audio owner provides no equivalent post-race menu action interface.
Original49CBD0 sends a selection notification through4F8340/4CD180 when the
selected racer changes. Existing frontend sound recovery is documented in
FRONTEND_66.md and src/frontend_sfx.cpp.

Correction scope: recover/map the real post-race button/page notifications
and their audio while preserving song continuity. This source finding does
not claim a newly measured device mix or exact cue timing.

### UI-08 / P2 / PARTIAL FEATURE: shop confirmations and save presentation

Scenario: confirm an ordinary part or Trade In, or choose Save in Underground.
Ordinary shop confirmation is a hand-placed text label such as
"Trade this car? A: OK B: Cancel"; Save persists state and shows
"Career saved". The full original dialog/save frontend is not used here.
The Unique award path already uses its dedicated authored dialog.

Source: src/career_ui.inc:536..540; switch/race_main.cpp:2252.
Original resources include PC_LoadSave.fng and the shared dialog system.
Buying, cancellation and saving can function while their presentation remains
incomplete. No price, ownership or save-loss defect is inferred from this.

Correction scope: restore the appropriate original dialog/page states and
messages, with the actual Switch actions and error/overwrite policy.

### UI-09 / P2 / PARTIAL FEATURE: magazine frontend is an album adapter

Scenario: receive or browse a magazine. The current path displays the earned
cover with its saved car and allows cycling/dismissal. It is not the complete
MU_UG_MagazineScreen/MU_UG_MagazineGiveth screen/controller presentation.

Source: src/career_ui.inc:727..765; switch/race_main.cpp:2026,2168,2268;
CAREER_MEDIA_MAGAZINES.md. Covers, saved tuning, caption and recipe selection
are already implemented; full browser/award layout, state animation, song
override and some special-case presentation producers remain open.

Correction scope: connect the original browser and award-page owners without
replacing the saved cover/car with a promotional stock image.

### UI-10 / P3 / PARTIAL FEATURE: pad hints are an incomplete adaptation

Loading has no controller card (UI-03). Career panels use a small manually
selected footer set (src/career_ui.inc:562). Post-race rendering removes
several PC help/button objects without a complete replacement Switch help
layer (src/frontend.cpp:911..914). Magazine presentation has no equivalent
browser help layer in record_magazine.

Correction scope: a screen/action-based Switch hint owner. Retain the existing
correct palette and vinyl action hints; audit press/hold, previous/next and
confirmation/back states rather than copying PC keyboard labels literally.
This is a coverage/presentation gap, not a claim that every current hint
contains a wrong binding.

### UI-11 / P2 / PARTIAL FEATURE: main settings/profile frontend is restricted

FrontendMenu exposes Underground, owner-requested Free Driving and Exit.
Main Statistics, Options and Customize are visible but unavailable; garage
customization remains available inside Underground. The current career entry
uses one port save base instead of a complete original profile/load/save UI.

Source: include/nfsu/frontend.h:13..34; src/frontend.cpp:18..21,1073;
switch/race_main.cpp:1884. The original has PC_LoadSave, options selectors,
HUD options and pause options. Settings such as units/HUD/audio/control
configuration do not have the corresponding complete frontend here.

This is existing milestone scope, not a new r98 regression. It must be counted
for a full-interface release, rather than describing the main menu as complete.
MULTIPLAYER / COMING SOON is an intentional owner-approved parked feature.
Free Driving with arbitrary cars/tracks/tuning is also an intentional adaptation.

## Present boundaries: do not list them again as missing

- Original main-menu art, moving garage/menu car, music and the timed main
  animation have implementation/native checks. UI-04 concerns career panels.
- The first Underground welcome is an overlay on the resident menu; its
  scripts are sampled with time. It is not the old unconditional black screen.
- Event-map selection, briefing, difficulty, authored speech/media and their
  progression are connected. Their complete visual/audio acceptance is separate.
- Normal frontend/race/restart/tournament loading uses the original generic
  art and moving loading bar. The old ordinary heap/percentage loading page
  is not the current primary loading UI. UI-03 concerns the missing branches.
- Race HUD includes lap/time/position, minimap, turbo/NOS and drag shift cues;
  countdown and Race Over have live paths. No new reproduction of their old
  missing-display reports was established in this audit.
- Ordinary post-race results, style counting, rewards, menu and Statistics
  are shared implementations, not drag-only. Original49E100's ordered flow
  and the separate UG0 victory bypass are present.
- Full-race Restart, tournament round continuation, reward ownership, before/
  after movies and magazine acknowledgement have progression checks. That does
  not certify every natural Switch transition or its exact visual timing.
- r98 Trade In has actual resident car switching, selected logos/thumbnails,
  data and purchase checks. No new reproduction of the old static-car issue
  was established in this audit.

## Checks and rejected suspicions

The actual linked UI probe and original resource inventory were run from the
real workspace path. Logs and raw software layers are in build/ui-audit-r99.
The current source remained unchanged throughout these observations.

A suspected reversed post-race Up/Down order was rejected. The actual rendered
menu places Continue at the bottom, Statistics in the middle and Restart at
the top. Repeated Up selects that correct visual sequence. The generated PNGs
are isolated UI layers over a diagnostic black background; they do not prove
a black-screen defect in the live result composition.

The original style-page condition was checked through49E0B0/49E100; a new
zero-net-style bug was not established. Do not turn an unconfirmed suspicion
into a defect merely because the implementation looks abbreviated.

## Unverified presentation/acceptance work

- Complete on-device finish -> results -> rewards -> unique/movie/magazine ->
  return sequences for win/loss, drag terminal failures, knockout and every
  tournament round. The source path is present; old reports are not proof of
  a still-current skip bug.
- Natural post-finish camera/world composition, transitions and music/page
  timing against the original. No new original launch or console run occurred.
- Continuous loading-bar cadence during long synchronous asset work: the
  current screen only advances when progress/render polling presents it.
  A visible stall duration was not measured, so this is a limitation to test,
  not a newly confirmed console hitch or a crash claim.
- Remaining drift notice retirement/leaderboard states and full minimap option
  styles, beyond the restored selected-mode/branch tests.
- Every authored row color, achievement popup, help glyph, safe-area/scaling
  and docked/portable view. Passing resource and vertex checks cannot accept
  every final screen.

## Recommended implementation order

1. Pause/confirmation and ordinary cancel/error presentation (UI-01).
2. Correct mode-specific/per-racer statistics, then post-race audio/help
   (UI-05,UI-06,UI-07,UI-10), alongside terminal-flow acceptance.
3. Loading tip/controller/continue state owner (UI-03).
4. Common career FEng transition/lifetime owner and original shop dialogs
   (UI-04,UI-08). This should serve map/difficulty/garage, not separate fades.
5. Underground ranking/statistics and the remaining options/profile frontend
   (UI-02,UI-11); complete magazine browser/award UI (UI-09).

These are UI tasks, not a recommendation to retune car physics or lower
rendering quality. No implementation fix or new NRO is part of this audit.
