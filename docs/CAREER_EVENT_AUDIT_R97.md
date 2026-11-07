# Campaign event integration audit

## Scope and original input

This pass covers the numbered campaign through event113, including each
individual tournament round. Event85 is absent in the original table; the
separate debug events are outside this campaign. Earlier events are included
as regressions, not treated as already accepted.

The inventory comes directly from FrontB/GLOBALB, each TRACKB image and each
required regional STREAM header. The original PC USA1.4 executable is the
same authenticated input used by the native witnesses. Retail data remains
read-only. The generated inventory is in the local r97 evidence bundle.

## Corrections

### Every drift event uses its own regional start data

RaceRun and RaceGpuScene previously special-cased the first drift event.
Later drift races could start without its authored crowd/camera clock and
without the matching final car pose. The common drift start adapter now
loads scene2333 from the selected event/round's actual region, applies its
release/GO timing and uses that region's final animated car placement.
The selected player's real car geometry is also used by the runtime audit.

This restores the existing scene2333 policy across all drift tracks; it does
not restore the original random choice of all one-car start variants.
Free driving on Switch explicitly suppresses start scenes.

### Physical objects are connected in drift worlds

A drift race used to bypass the shared world-prop setup because it has no
physical rivals or traffic pool. It now loads the same authored placement
world. The audit compares the admitted object count with an independent
raw-data inventory, including the placements on1303 and1304. It does not
invent traffic or physical opponents for the drift competition.

### Rival placement settles before rest admission

The all-event motion audit exposed a stationary Supra in tournament30,
round1 on Hard. Loading preparation had left it asleep on its front axle:
its driven rear wheels had no contact and its disabled gravity prevented
recovery. The ordinary driver correctly refused throttle without driven
contact, so changing its throttle or engine constants would hide the fault.

The loading-only placement phase now excludes rest admission until the
existing body/strut solver settles, just as player placement already does.
Normal selector3 rest resumes on the first live update. The original
47AC00 rest predicate, forces, suspension constants, difficulty settings
and catch-up calculations are unchanged. The retail failing case and a
synthetic placement/rest regression exercise the correction.

## Verification and reproduction

The detailed commands, executable/source identities and results are recorded
under build/fidelity-r97. They are local evidence under the owner's Git,
index and publication hold, not an independently reviewed release.

- root-inventory-r97.py decodes the complete event/round/track/direction,
  region, traffic-pool and prop-placement inventory.
- nfsu_career_races_tests with audit-setups checks each authored grid and
  traffic selection on all difficulties, and each drift performance class.
- NFSU_CAREER_DRIVING_CHECK runs the real race simulation beyond authored GO,
  observes finite rival motion, and builds the actual gameplay HUD/minimap
  and scene commands. Short cinematics cannot count as driving coverage.
- NFSU_CAREER_EXPECT_PROPS checks the independent placement inventory.
- NFSU_CAREER_TRAFFIC_SWEEP moves an observer along the complete course and
  advances the real traffic owner at each sample. It requires actual spawn,
  active updates and finite nonzero motion. This supplements initial-area
  driving checks, which can legitimately see no traffic.
- nfsu_career_tests with a fresh output directory exercises the campaign
  progression, saved tournament rounds, briefing/result/reward UI, unique
  choices, media and unlocks at every difficulty through the final event.
- nfsu_ai_track_tests checks loading settlement and restoration of live rest.
- The scoped CTest and SDK receipts identify the additional selected gates.

Examples, from the actual worktree with its toolchain on PATH:

```text
build-release/nfsu_career_races_tests.exe <retail-root> audit-setups
build-release/nfsu_career_tests.exe <retail-root> <fresh-output-directory>
```

For a selected stage, set NFSU_CAREER_TEST_ROUND,
NFSU_CAREER_TEST_DIFFICULTY and NFSU_CAREER_TEST_CAR before invoking
nfsu_career_races_tests <retail-root> <event> <driving-frames> with the
appropriate diagnostic environment flags above. This is test automation;
ordinary player input and save files are not overridden.

## Acceptance boundaries

A finite motion check is not an original-input trajectory replay. A course
spawn sweep does not certify every natural traffic collision, intersection
history or contact ordering. UI progression checks exercise production state
and draw commands, not every original FNG transition pixel for pixel.

The owner still needs to accept the changed later-event starts and props on
Switch. The short post-GO checks do not certify full-race performance, all
player cars/tuning combinations, or natural completion of every event on
hardware. General source/native adapter limits remain in FIDELITY_STATUS.md.
No whole-game parity or independent release review is claimed.
