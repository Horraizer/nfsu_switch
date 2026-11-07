# Checkpoint: the time-trial events (owner task, 2026-10-18)

```text
CHECKPOINT
role: worker
agent: GLM (claude-code session, day20261018-glm-timetrial)
updated_utc: 2026-10-18T12:00:00Z
assignment: owner instruction 2026-10-18 "restore the time-trial tracks"
  (child of #299)
phase: implementing
branch: codex/day20261018-glm-timetrial
head: (base c9ac6e7, no commit yet)
base: c9ac6e765fd4dfaa78d3ebfbe83be778456573c0 (knockout head; PR #337,
  stack now #332..#337 - the sprint PR #336 and knockout PR #337 opened
  today once Gitea returned)
worktree: build/day20261018-glm-timetrial (E-drive checkout, Windows)
saved: nothing committed yet; Gitea reachable again.
verified: retail map re-probed today (session log). All seven time-trial
  events are kind-2/kind-3 single-race unlockable races, one
  point-to-point pass each on sprint tracks the sprint family carries.
decisions: (1) scope = all seven (7, 15, 26, 47, 68, 77, 97); the kinds 2
  and 3 are exactly the unlockable races the availability rule treats
  specially - supporting them is the same fail-closed row pattern. (2) A
  grid marker with no road below (TRACKB1102's backward grid hangs over
  the void at the world edge, no surface within 300 units) starts the car
  at the nearest path node on the road under it, facing along the spline
  - a documented project rule. (3) base = the knockout head.
remaining: full default suite (running), Switch, Release, ASCII, commit,
  push, PR stacked on #337, REVIEW_REQUESTED.
next: gates, then the handoff.
blockers: none.
ownership: this session (worker); no coordinator assigned by the owner.
```

Session log (append-only):

- Retail time-trial map: 1102 rev {7}; 1104 fwd {15}; 1105 fwd {26};
  1103 rev {47}; 1106 fwd {68}; 1107 rev {77}; 1109 fwd {97}. Kinds 2 and
  3, single races, two stored laps on lap-skip-1 sprint tracks.
- Wiring: kSupportedTimetrialRows + the family branch in
  load_supported_race (type time_trial, region from the row, lap-skip 1),
  set_event, the track_slice gate and both --event parsers. No new
  manifests, budgets or loaders - the sprint sets carry everything.
- GRID FALLBACK: event 7's backward grid marker sits at (2320.84,
  -333.32, 57.70) - the world edge, with NO surface in a 300-unit column
  (probe-grid evidence). vehicle_track_grid_pose now falls back to the
  nearest path node with a road column under it, heading along the
  spline tangent (reversed for reverse routes).
- Tests: tests/timetrial_tracks_tests.cpp (guards - kinds 2/3 refused
  nowhere else - + load pins for all seven rows with the sprint facts,
  lap_skip 1, lanes = progress spline asserted) and two retail
  registrations; the five families' refusal lists stopped refusing
  events 7/15/26 (26 was a replacement for 5/10 a round ago - the lists
  churn with every family; replaced with 118/125/126/127). Retail run:
  all seven load and start (13s).
- Docs: DESKTOP_RACE, RACE_EVENTS.
- Gates: full default suite running; Switch and Release queued.
