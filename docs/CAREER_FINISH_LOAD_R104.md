# Career finish failure and scene load follow-up

This is a local follow-up to RACE_VISUAL_DRIFT_R103.md. Git publication and
retail-file writes remain held. This document distinguishes a confirmed
lost-result defect from the still-unidentified continuation exception.

## First sprint

The owner's SD journals nfsu-career-flow-0039.log and0043.log identify
career-menu-info-r102-r2, event2. Both record a valid normal finish, first
place and player_won=true, followed by failure before post.atlas. Neither
records the exception text. Therefore the failure precedes result-page
construction; neither the statistics save nor event_finished had run.

The Switch run catch now writes race.failed with the actual exception to
the same boundary journal. This adds no per-frame SD write. A subsequent
failure can be diagnosed without relying on the root-menu notice.

Previously any continuation or result-presentation exception suppressed
completed_place and threw past the caller's career commit. Terminal facts
are now published even when presentation fails. The caller defers that
exception, records statistics and commits the completed event/tournament
round, then rethrows it for a visible error. A failed load, uncompleted
race, explicit pause exit or restart does not gain a completion. Terminal
failure statuses retain their losing result, never a first-place prize.

This protects a confirmed finish; it does not identify or certify repair
of the original sprint continuation exception. Hardware confirmation of
the sprint and result screens is still required. Synthetic terminal
simulation and command generation succeeded on the host; those fixtures
are not a substitute for the owner's full route.

## Scene loading

StartCrowd's streamed-city metadata traversal formerly reopened the SD
file for each eight-byte chunk header and again for each solid identity.
The traversal now retains a bounded128KiB read-ahead window and stores
the solid identity with its chunk descriptor. Parent limits, nesting and
chunk ordering are unchanged. Selected payload reads still use the file
system service, and no city-sized allocation or SD cache is introduced.

The counted local before/after experiment is recorded under
build/career-finish-r104/root-crowd-io-*.log. The production regression
bound and functional checks live in nfsu_race_start_camera_retail. Host
wall time is not a prediction of Switch load time; the final NRO needs
the same-track console measurement.

## Dry scene rendering

A separate host reproduction found an invalid reflection lookup while
updating movable objects with wet-road reflections disabled. The table
exists only with that feature enabled. Object placements now update that
table only when it exists. nfsu_career_dry_scene_retail covers the loaded
sprint scene with objects and reflections disabled. This is a confirmed
defect, not an established cause of the owner's wet-road sprint failure.

## Remaining evidence

The missing original SHADOWRAMP geometry shadow is documented in
RACE_VISUAL_DRIFT_R103.md. Traffic-shadow fidelity remains open. No size,
darkness or substitute-texture change is claimed as restoration of it.
The portable720p menu and the preceding drift/chooser changes also await
hardware acceptance on the combined candidate.
