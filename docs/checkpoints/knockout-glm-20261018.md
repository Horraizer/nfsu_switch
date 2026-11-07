# Checkpoint: the lap-knockout tracks (owner task, 2026-10-18)

```text
CHECKPOINT
role: worker
agent: GLM (claude-code session, day20261018-glm-knockout)
updated_utc: 2026-10-18T00:00:00Z
assignment: owner instruction 2026-10-18 "restore the lap-knockout
  tracks" (child of #299)
phase: implementing
branch: codex/day20261018-glm-knockout
head: (base 8465bb3, no commit yet)
base: 8465bb37bb2fc35321203af5011b919eeef57238 (sprint head; the sprint
  push itself is still queued - Gitea down at its handoff)
worktree: build/day20261018-glm-knockout (E-drive checkout, Windows)
saved: nothing committed yet; Gitea down (ssh 2223 refused).
verified: retail map re-probed today (session log). The twelve
  lap-knockout events are kind-0 single three-lap races on the closed
  circuit tracks, whose manifests, loaders and lanes the circuit families
  already carry; the only new lane is track 1003's reverse, which no
  circuit event races.
decisions: (1) scope = all twelve single-race lap-knockout events (5, 10,
  24, 31, 39, 49, 58, 60, 72, 84, 91, 103); none are tournaments. (2) The
  elimination rule needs opponents the solo port does not simulate, so the
  port runs each knockout as its three laps - documented, not silent.
  (3) base = the sprint head (itself unpushed).
remaining: drive results; pins, tests, docs, gates, commit; both the
  sprint and knockout pushes queue behind Gitea's return.
next: read the 12-event drive run.
blockers: Gitea unreachable (both this and the sprint branch wait).
ownership: this session (worker); no coordinator assigned by the owner.
```

Session log (append-only):

- Retail lap-knockout map: 1003 rev {5, 10}; 1006 rev {24, 39} fwd {60};
  1005 fwd {31}; 1008 fwd {49} rev {58}; 1001 fwd {72, 103} rev {84};
  1007 fwd {91}. Every race is kind 0, three laps, on L1RA closed tracks.
- Wiring: kSupportedKnockoutRows + the family branch in
  load_supported_race (type lap_knockout, region L1RA, lap-skip 0),
  set_event, the track_slice gate and both --event parsers.
- New lane: circuit1003_reverse derived from TRACKB1003's collision world
  (240 samples, none missing, period 30); the other eleven events ride
  the lanes their tracks already have (event 31 on 1005 forward takes the
  authored event8_forward line).
- The first 12-event drive run went out WITHOUT the lane wiring (a
  background-shell quirk picked up ETAP's Python 3.8 and the lane script
  died in its subprocess step while the chain went on) - all twelve
  stalled on the progress spline. Re-applied directly and re-driven
  (drive-all2.txt): events 31 (1005 fwd, the authored event-8 lane),
  72 and 103 (1001 fwd) FINISH three laps - 31 in 72,649 steps with zero
  resets, 72/103 in 102,253 steps with three. The other nine stall at
  8-92 percent - the circuits' documented lane gap, no new regression.
- Tests: tests/knockout_tracks_tests.cpp (guards + load pins for all
  twelve, drive=true for 31/72/103 with reset budgets 0/3/3, the Atlantic
  drive loop), two retail registrations; the four families' refusal lists
  stop refusing events 5 and 10, the circuit extras list moves off 10
  (events 26 and 30 replace 5 and 10), and the test's lap_skip pin is 0
  (a template slip: the drag generator left `== 1`). Family run 12/12
  PASS; the measure run drives the three completing lanes (324s).
- Docs: DESKTOP_RACE, RACE_EVENTS.
- Gates: default 178/213 with exactly the 35 pre-existing failures;
  Switch 49/49 (ASCII dir C:/nfsu-builds/glm-knockout-switch); Release
  running.
