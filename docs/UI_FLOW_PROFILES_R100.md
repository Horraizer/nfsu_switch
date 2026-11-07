# Pause, result statistics and named profiles

Owner scope: restore race pause/exit, then result/statistics behavior from the
PC1.4 USA original, add named independent careers and a Switch software keyboard,
and combine the changes into one NRO. This pass works on the held combined
worker tree. It does not publish, merge or replace the independent review gate.
The exact source and validation receipts live in build/ui-flow-r100; the packaged
receipt accompanies out/nfsu-r100. The committed HEAD alone is not this snapshot.

## Original evidence

The read-only input is speed.exe, SHA256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Ghidra evidence is saved as root-native-*.txt in the pass directory.

- 4CD660/4CDEA0/4CE370 configure the race pause controller. 4CD7B0 dispatches
  resume, restart, options and leave; closing waits for the authored FadeOut.
- 4CDCF0 constructs the restart warning about losing this race's style points.
  4CDDB0 selects the leave warning by game context. 4DC7C0 copies the dialog
  operands; 4DC840 handles accept/cancel. The focus object is button2 (Cancel).
- MU_Pause_PC.fng at INGAMEB offset0x175e0c and MU_PauseConfirm.fng at0x173b0c
  supply actual object placement, colors, resources and scripts. Screen Init
  and FadeOut use the authored200-tick timeline, with1200 ticks per second.
- 49E100 owns the existing results -> style -> rewards -> menu sequence.
  UG0 victory retains its original special continuation rather than inventing
  the ordinary event reward sequence.
- 49CBD0 clamps selected statistics racers. The bounded native oracle executes
  its actual bytes, stubbing only rendering and sound callees. Its fixture is
  tests/post_race_selector_native.inc; tests/frontend_tests.cpp compares the
  production selector and sound-admission decision with that fixture.
- 49C820 selects a physical race record. 49CEE0 branches on point-to-point
  track-mask4 and omits lap statistics; 49CC70 has drag-specific fields.
  49CD90/49D940/49DF30 are the drift binding. Drift's simulated score entries
  have separate IDs beginning at100 in43EB00; they are not extra physical
  cars to index through RaceSession::rival.
- 417700 calls41F160 with the original12-character name-entry limit.
  The profile list reuses PC_LoadSave.fng at FrontB offset0xbdf40c. Keyboard,
  storage paths and controller hints are explicit Switch adaptations.

The input bundles and language resource are authenticated before their artwork
is decoded. No retail file, original PC profile or original executable is
modified or launched by this pass.

## Race pause and exit

Plus or Y opens the race pause menu. Simulation and race effects remain paused
while navigating. Resume restores the same race, its HUD and GPU effect
allocations; it does not rebuild the world or advance the physics debt accrued
while paused. The ordinary OS suspension path enters this menu as well.

Restart and Leave require their separate original confirmation dialogs. Cancel
returns to pause. Returning to the track, restarting and leaving wait for the
original pause FadeOut. B and Plus are Switch back/cancel adaptations.
Practice uses the short quit prompt and does not write career losses.
A live tournament uses the separate4CDDB0/5A2F10 warning; confirmation
discards intermediate standings and restarts that event from its first round
on reentry, preserving previous event wins and already banked rewards.

The temporary overlay replaces only fenced HUD allocations. Unlike the final
post-race transition, it retains the speed-blur and light-history allocations;
the HUD atlas is restored before ordinary racing resumes. It does not allocate
a second world renderer. The technical result page remains behind explicit
profiling and cannot appear after an ordinary pause-menu exit.

The Options destination currently exposes the implemented race controls:
music volume, effects volume and HUD visibility. These apply to the current
race session. The original full audio/camera/controller/display/Trax option
frontend and durable option preferences are not claimed restored by this pass.

## Results and statistics

Sprint/point-to-point statistics bind finish time, gap, top speed, average
speed and trip distance. Original49DB30/49DC20/49DD10 formatting and USA unit
factors replace generic conversions;49D4C0/49D5B0 retain the no-time marker
and terminal reason in the time-behind field. Circuit lap fields and their unused backings are
hidden. Circuit, drag and drift retain their separate field bindings.

Physical racer telemetry is accumulated independently at committed simulation
steps. Each racer has its own top speed, distance, elapsed simulation time and
lap positions, and stops accumulating at its own terminal result. Duplicate
observations do not count a step twice. Slow-motion time uses actual world
frame seconds rather than display cadence. This adapter is not a claim of
bit-identical original race telemetry.

Statistics opens at the human result-order row (49BEE0) and can select
physical racers with Left/Right, constrained to the original range. Binding a different racer replaces its detail fields and
identity without overwriting race reward/style values. Result-order labels
also remain meaningful for unfinished rows. The single visible
player in drift does not acquire fictional physical rival statistics.
Navigation and page actions play the recovered common frontend cues through
the existing race audio device. There is no second music/output device.

Original frontend cue decoding/mixing now has one shared library used by both
frontend music and race UI. The default race audio gain path is retained;
non-default pause volume controls apply user attenuation separately.

## Profile creation and storage

Press X in the main menu to load or create a profile. Go Underground opens
profile creation when no career exists. A loads the selected profile; X creates
one; B returns. The active selection is retained across process restarts.

Creation opens libnx's normal software keyboard, with the recovered name length
limit and an ASCII check matching the currently available font. The system
keyboard provides touch input in handheld mode. Its actual invocation, touch,
cancel and focus return still require the packaged NRO's Switch acceptance;
a successful SDK build cannot prove those behaviors.

Names are display text, never filenames. Files use ASCII numeric IDs under
switch/nfsu-saves/profiles/pXXXXXXXX. Each has checked name metadata and its own
career.0/career.1 redundant save pair. Duplicate names ignore ASCII case.
Cancel or failed validation does not create a profile or reset a career.
Malformed/incomplete profile directories are preserved and never reused.
The last save date is displayed from the selected save files' timestamps.

The pre-existing switch/nfsu-saves/career.0/.1 pair appears as PLAYER. Merely
creating or selecting another profile does not rewrite that legacy pair.
The new profile starts at the original career introduction with no purchased
car, upgrades or event progress. Selecting PLAYER returns to existing progress.

Save schema9 appends race win/loss/restart counters. Prior schemas remain
readable. Before an old valid slot is first upgraded, its exact bytes are
retained as career.before-v9.0 or career.before-v9.1; those backups are never
overwritten. An older NRO does not understand schema9; rollback requires
restoring the pre-v9 copies to the corresponding career slots after exiting
the game. Failed backup verification refuses the write instead of discarding
the older save. Newly created profiles start at schema9 and have no old files
to back up. Per-profile names are in separate metadata, not a changed retail
PC save format.

## Validation and remaining acceptance

Focused tests are in frontend_tests, career_tests, crash_camera_tests and the
existing race audio tests. They cover native selector fixtures, confirmation
cancellation, real FNG decoding/rendering, per-racer counters, slowed time,
profile isolation, duplicate/invalid names, durable selection, prior-schema
migration and exact rollback backup bytes. Pinned values belong to those tests.
The software reference renders are UI layers on a synthetic neutral background;
they are not screenshots of a running race or the original game.

Required final gates are full Default and Release configure/build/plain CTest,
with the retail root and no skipped retail check, plus the affected SDK NRO,
shader and packaging checks. The receipt records actual results and source
identity; a build or targeted pass alone is not a complete validation.

Console acceptance must check pause -> cancel/resume -> driving, restart,
leave, results -> statistics -> another racer -> back, and creation/cancel/load
of a second profile with keyboard touch and focus return. These checks remain
explicitly unverified until the owner runs the candidate. No claim of complete
original UI fidelity or console acceptance is made from this source pass.

Other findings in UI_AUDIT_R99.md remain separate: Underground ranking/career
statistics screens, conditional loading tips, general career panel timelines,
ordinary shop/save dialogs, full magazine frontend and remaining options/help
coverage. This pass does not silently mark those implemented.
