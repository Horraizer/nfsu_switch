# Underground Rankings and Statistics

Rankings and Statistics in the Underground menu open real retail packages over
the current garage scene. Left/Right selects the race mode; B returns. Selection
clamps at the first/last page and makes no navigation sound at an unchanged edge.
The car, scene, GPU atlas and music remain resident while browsing these pages.
The screen selector is transient: opening it never changes event availability.

## Original evidence

Read-only PC1.4 USA speed.exe SHA256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Decoded FrontEnd/FrontB.lzc SHA256:
94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77.

- 4E9CE0 constructs Rankings; 4E9E90 binds the rank and driver objects.
  4EA090 handles its bounded Circuit/Sprint/Drag/Drift category selector.
- 5A2690 selects the highest numbered won event whose kind is not8. Event113
  aliases112. Chunk34A03 supplies the driver names and event-specific rosters.
  Its row metadata is not an availability flag: the original selects by row
  index, including the authored placeholder record at60.
- 5A1340 searches won kind6 events and takes the best rank at+36 for the
  category at+34. The selected map event and number of wins are not substitutes.
  4E9E90 walks the roster backwards and replaces the player's actual rank row
  with the profile name. It shows the separate UNRANKED_GROUP only if unranked.
- MU_UG_RankScreen.fng at0xbc640c and MU_UG_StatsScreen.fng at0xbc818c provide
  the original geometry, text positions, resources and script timelines.
- 4EA210 constructs Statistics. 4EA460/4EA510 select Circuit, Sprint, Drag,
  Drift and GLOBAL. 4EA810/4EA6B0/4EA720/4EA7A0 bind starts, wins and mode style
  points; Drag adds Times Totaled. 4EA880 binds total starts, wins, losses,
  integer win percentage, mileage and the cumulative style-point bank.
- 4AB880 classifies statistics by authored track mode flags. A circuit-based
  knockout and a sprint-based timed challenge follow their road categories.
- 4ABAA0 records starts, excluding the introductory Underground event.
  4A9FD0 updates outcome, signed mode style, distance and terminal reason7 in
  drag. The adapter retains physical simulation-step mileage already measured
  by RaceResultStatistics; bit-identical original telemetry is not claimed.
- 4EA8E1..4EA985 handles percentage, USA/metric distance and style-bank display.
  4EA5D0/59FC90 format mileage. The original retains two fractional digits.

The rotating circle decorations use the retained native fixed-sine quaternion
interpolator. Statistics activates TYPE GROUP using its authored FadeIn state.
Changing a category replaces data without restarting the whole screen Init.
Switch control hints replace original PC/controller helper labels.
4F0660 rejects nonpositive transformed leaf depth, including the otherwise
visible White16x17 helper rectangle. 4F16D0/4F5090 reverse equal-depth package
ties before submission. These rules apply to the new information packages;
they do not silently change the existing garage/post-race renderers.
The four original arrow SA quaternions are checked against native image
transforms, including the right-arrow Y half-turn and diagonal down-arrow
rotation. Reading only quaternion Z/W would reverse these controls visually.

tools/career_info_native.py executes the original controller, rank-selection
and statistic arithmetic instructions. Rendering, sound and unrelated record
book side effects are captured or stubbed. tests/career_info_native.inc is the
fixture of record; tests/career_info_tests.inc compares production behavior,
retail artwork and persistence against it. No original game is launched.

## Persistence and existing careers

Port save schema10 appends detailed statistics to each profile's checksummed
redundant career slots. Successful returns, repeated races and each tournament
round contribute once; a failed load contributes nothing. Quitting a started
race counts as a loss. Introductory event0 and free practice do not write these
career statistics. Profile statistics never cross between named saves.

Prior schemas remain readable. Earlier saves did not record mode/mileage
history, and event-completion bits cannot reconstruct retries and losses.
Their detailed counters begin with this version and the screen explicitly
states that earlier history is unavailable. Existing money, event progress,
car, tuning, style bank and legacy aggregate counters are preserved.

Before upgrading an older slot, its bytes are retained as
career.before-v10.0/.1 beside that profile's career files. Existing pre-v9
backups are retained as well. An older NRO requires restoring its compatible
backups to career.0/.1 after exiting the game. Never change saves during play.

## Validation boundary

nfsu_career_info_retail covers original native fixtures, all information pages,
rendered text visibility, bounded GPU submissions, real track classification,
profile-slot round trips, legacy partial-history migration and exact rollback
bytes. nfsu_career_retail retains the wider career/profile compatibility checks.
Full Default and Release gates, retail read-only fixtures and affected Switch
NRO/shader packaging gates are required for a candidate.

Reference captures are software renders of retail UI on a neutral background,
not original-game or live Switch screenshots. Console acceptance must still
check opening both destinations, category navigation, readable values, entry
animation and sounds over the live car/music, then persistence after a race
and profile reload. No independent review or hardware acceptance is implied
by a successful build or host test.
