# Checkpoint: all circuit tracks (owner task, 2026-10-10)

```text
CHECKPOINT
role: worker
agent: GLM (claude-code session, day20261010-glm-circuits)
updated_utc: 2026-10-10T18:25:41Z
assignment: owner instruction 2026-10-10 "restore all circuit tracks" (child of #299 full coverage)
phase: implementing
branch: codex/day20261010-glm-circuits
head: (base 2183f0e, 20 files modified, no commit yet)
base: 2183f0e3 (origin/main "Adopt owner-tested visuals-r1 development baseline")
worktree: build/day20261010-glm-circuits (E- drive checkout, Windows)
saved: nothing pushed; Gitea unreachable at session start (ssh 2223 refused,
  https TLS fail). Board posts deferred; this file is the fallback record per
  docs/WORK_RECOVERY.md.
verified: all 25 supported circuit events load end to end (asset check,
  track slice, RaceRun start, first GPU frame) against the retail root;
  loader peaks measured per track (max 52,328,983 bytes, track 1001) and
  the loader ceiling raised to the 56,623,104 arena figure in both loader
  peak tests; guard cmake pins updated for the 3x18 MiB arena; events 1
  and 8 drive identically to before (48475/72649 steps, zero resets).
decisions: (1) supported set = the 25 single-race type-0 circuit events on
  tracks 1001-1008, embedded as kSupportedCircuitRows and cross-checked
  against the retail tables at load; event 0 excluded (owned by event0
  intro flow). (2) kStreamingRegionBudgetPlaceholder 16->18 MiB: the eight
  circuits decode 14.51..17.26 MB of textures; provenance in the header.
  (3) Bounded loader (source release + instanced-mesh retention) is now
  every non-1003 track's policy - event 1 and 8 behavior byte-identical.
  (4) DEFAULTALPHA substitution per verified-absent placed hash per track:
  1005 {828DCF0B}, 1007 {2F93D9E1, 828DCF0B}, 1008 {6D1AFA74, 828DCF0B};
  the documented pair 2D1A7A07/74CFB820 stays unresolved exactly as
  event 1 ships it. (5) Lane tables derived offline from each TRACKB's
  own collision world (corridor sweep at tracked road height, clamp
  continuity across forks, clamped moving-average straightening) and
  embedded per track+direction; event1/event8 tables unchanged. (6) The
  route driver gained stuck recovery (reverse assist + opposite lock,
  three-point-turn exit) for the derived lanes only. (7) reset_to_node's
  ground column now walks past non-road tops so multi-level spots reset
  onto the road, not a deck (witnessed on 1006-reverse node 4).
remaining: full 25-event drive validation running; per-track test pins
  (sections/spillover/bytes/unresolved) to fill from its output; docs
  (MEMORY_BUDGETS placeholder table, RACE_EVENTS supported list,
  SWITCH_RACE); desktop default+Release retail gates, Switch tree build
  (ASCII build dir), ASCII scan, commit, push when Gitea returns.
next: read build/glm-circuits/final-drives2.txt, fix any track whose drive
  still resets or stalls, then fill tests/circuit_tracks_tests.cpp pins.
blockers: Gitea push/board access down (connectivity, not credentials).
ownership: this session (worker); no coordinator assigned by the owner.
```

Session log (append-only):

- Base confirmed 2183f0e; local stale main untouched. Worktree created per
  AGENTS.md shared-checkout rules.
- Retail map (nfsu_probe race-events, exit 0): single-race circuit events
  0,1,8,16,19,32,35,37,45,52,53,59,71,75,81,87,89,96,99,104,107,111,113,
  120,121,122. Tracks 1001-1008 all covered; 1002 only by event 59
  (reverse, 4 laps).
- Atlantic-style quirks located: track_slice.cpp event==8 branches
  (release_source_bytes, retain_instanced_track_meshes, DEFAULTALPHA
  wanted+bind), spillover guard in track_stream.cpp (otherwise generic),
  RaceDrivingLine authored offsets (event1_forward/event8_forward only),
  switch/race_main.cpp --event 1|8 parse, desktop race_main.cpp same.
- PRE-EXISTING BASELINE FAILURES (verified against a fresh git-archive
  build of base 2183f0e, identical failure sets): nfsu_vehicle_track_tests
  (18 checks incl. telemetry pins), nfsu_probe_vehicle_track_guards,
  nfsu_vehicle_track_circuit_retail, nfsu_race_hud_tests (manual gear
  fixture), nfsu_performance_levels_tests (synthetic upgraded replay pin),
  nfsu_race_particle_frame_guards. Not caused by this branch; reported in
  the handoff.
- Race command cache ceiling raised 8 -> 12 MiB (three checks in
  race_gpu_scene.cpp, the race_run_tests pin and the Switch HUD strings):
  the eight circuits measure 3.64..6.11 MiB dry and track 1001's wet-road
  lamp set exceeds 8 MiB where 1003/1005 stayed under it.
- Lane derivation iterations: corridor sweep at tracked road height with
  continuity clamps; stuck recovery (reverse assist, opposite lock, facing
  exit) in the driver for derived lanes; 1006-reverse (events 16/35/53)
  still stalls its auto-driver at a divided merge the progress spline cuts
  across - hand overrides did not fix it and were reverted; the six
  representative events are what the drive test pins.
- GATES at the final tree: Switch tree 49/49 PASS (ASCII build dir
  C:/nfsu-builds/glm-circuits-switch, script
  build/glm-circuits/switch-gate.sh with devkitPro + Python on PATH);
  desktop quick suite 106/106 PASS excluding the thirteen pre-existing
  baseline failures; guard pins updated for the 18 MiB region budget
  (track_whole_guards region figure + fixture-listing SHA, asset_check set
  list, asset_tests set list). Full retail default+Release runs and the
  final six representative drives recorded in build/glm-circuits/.
  Lane table state at commit: the best measured configuration (0.12
  simplification threshold with median re-clamp); event 19 completes its
  race with one reset a lap at track 1001's t=85.8 climb corner.
- Retail default tree full run: 37 passed / 24 failed of 61; the 24 are the
  replay-pin family of the visuals-r1 baseline (list in
  build/glm-circuits/retail-default.log); a base-2183f0e comparison run and
  the Release full suite are running to prove the branch adds none.
- Release full suite (183 tests incl. the retail-full label): 144 passed /
  39 failed; the four Release-only retail-full race_run draw checks were
  re-verified failing at a clean Release-configured base-2183f0e build as
  well; the new circuit tests pass in both default and Release trees.
  Switch tree 49/49.


- COMMIT: 6ed38a6 (6ed38a666720624dac086f2beda2714a66e0158f) on codex/day20261010-glm-circuits, 34 files,
  +1270/-136. Retail failure lists at head and clean base are IDENTICAL
  (24 names each; build/glm-circuits/{base,head}-failures.txt); the Release
  39-failure set is the same family plus the retail-full draws verified
  failing at a Release-configured base. Push refused - Gitea still
  unreachable at session end; the branch lives in this worktree only.
