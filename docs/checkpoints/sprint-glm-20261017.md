# Checkpoint: the sprint tracks (owner task, 2026-10-17)

```text
CHECKPOINT
role: worker
agent: GLM (claude-code session, day20261017-glm-sprint)
updated_utc: 2026-10-17T00:00:00Z
assignment: owner instruction 2026-10-17 "restore the sprint tracks"
  (child of #299)
phase: implementing
branch: codex/day20261017-glm-sprint
head: (base ccb480d, no commit yet)
base: ccb480dfc364ec8832fabb77330769ddb07918bd (drag head, PR #335;
  stack: #332, #333, #334, #335)
worktree: build/day20261017-glm-sprint (E-drive checkout, Windows)
saved: nothing committed yet; Gitea reachable.
verified: retail map re-probed today (session log); the open-spline,
  point-to-point timing and road-column grid machinery the drag task
  landed is the foundation - the sprint tracks 1102-1109 are open
  lap-skip-1 courses in the L1RA region, whose region stream the manifest
  already carries.
decisions: (1) scope = the 20 single-race sprint career events of kinds 0
  and 6 on tracks 1102-1109 (tournaments 66 and 102 excluded; the kind-5
  event 112 excluded and documented - every supported family so far covers
  kinds 0 and 6 only, and kind 5's meaning is unrecovered). (2) base =
  the drag head; the new PR stacks on #335.
remaining: rows+manifests+sets; load survey; absent-hash verification;
  lanes and drives; pins, tests, docs, gates, commit, push, PR,
  REVIEW_REQUESTED.
next: wire kSupportedSprintRows and the eight first-lap manifests, build,
  survey.
blockers: none.
ownership: this session (worker); no coordinator assigned by the owner.
```

Session log (append-only):

- Retail sprint map (single-race, kinds 0/6): 1102 fwd {2}; 1103 fwd
  {12, 41} rev {22}; 1104 fwd {23, 67} rev {43}; 1105 fwd {54} rev
  {34, 61, 101}; 1106 fwd {27} rev {57, 100}; 1107 fwd {74}; 1108 fwd
  {78, 82} rev {95}; 1109 fwd {94} rev {110}. Every race stores 2 laps on
  a lap-skip-1 track (one point-to-point pass). Excluded: tournaments 66
  and 102 (three races each), kind-5 event 112 (single sprint on 1108
  reverse; no kind-5 event has ever been supported and the kind's meaning
  is unrecovered).
- File identities (bytes, sha256) - the per-track packs are ALL EMPTY like
  the circuits' (the sprint content lives in STREAML1RA.BUN, already
  carried), and the TRACKB bundles are the family's largest (0.6-1.0 MB):
  STREAML1RA_1102..1109.BUN all 0
  E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855;
  TEX1102TRACK.BIN 1086216 7625EECF2816D6EBD79A9F6E523EAB337C22D4B500FB
  BCB46F292E2D8EB24A09; TRACKB1102.lzc 644372 A60B908AA5C054A258788120C9
  AB4B1A2CB584FD960365A6316F0F365BADC4F9; TEX1103TRACK.BIN 1086216 F5CB
  FE5442C153FA314DE7BB18F5F180062B6C3E83F95D4D4C688D8E379AA49D;
  TRACKB1103.lzc 718765 C22E35ED7C91A9467691B3A9896CD0BDF13F91DA155C2E39
  12F88E567E4FF2B8; TEX1104TRACK.BIN 1086216 262D2A55EAEE43F7F90BC9CCB5C
  1DAE1E1A5E9FCBE81F2B50F1053AAB78FDDF4; TRACKB1104.lzc 873077 7D689551
  AF173BD4D7C04C0F7A224414D9A2FD2D80A23B7BEDEDB60526CC9F00; TEX1105TRAC
  K.BIN 1086216 479712F491953A3A6B42C269AB4D6331CC5E7361105DDF30CDCF9F27
  2487EC11; TRACKB1105.lzc 853671 0A26D41A043180391236D5A14A00F12C16D58B
  8BAFA0AAFF9FE7892E162C8CE3; TEX1106TRACK.BIN 1086216 FCBBAE765068F0809
  AED8F114399F59DF202CCA0C7E9D769EEF0D3877EBDAC5A; TRACKB1106.lzc 927849
  FC772389B794008002F70E2EFBD528E78E065CFA9FAE6763E3FA6019432D4172;
  TEX1107TRACK.BIN 1074952 8BB7992EB4A0BE73971D3725C60DF775613ACA6BC4E85
  9B53FC43645B3930BF0; TRACKB1107.lzc 1005788 06E9055955A61B6FAFE583559
  21B54B9FF2E758200DE74CC61FD8291F9FA5177; TEX1108TRACK.BIN 1086216 6E43
  65C4987F85E46F74B361C04D294DD44FBE9FACA8D303046D29FCB76986DF;
  TRACKB1108.lzc 822136 4024099C291D68D4DFE81B9FD1B4118ED50DD4217C147B90
  BC5A07042E535B60; TEX1109TRACK.BIN 1086216 18B68CCC8D26A0F27451DA9B83A
  13B2F037A1A6499543F625BE7B3065A9DB47C; TRACKB1109.lzc 744101 4714E5D4
  AA2CCFBC5A51740442F0BEC110CAA74D09195E1F20BE188309D09E0C.

- FINAL LOAD FACTS (survey-final2, substitution in, region 24 MiB): 1102
  105/14351488/29719156 spillover 15 unresolved 0; 1103 169/18263584/
  40710532 sp12 unres 0; 1104 195/19122592/43747900 sp12 unres 0; 1105
  208/18817184/45396032 sp10 unres 0; 1106 197/19064896/48663340 sp9
  unres 74CFB820; 1107 199/19209280/49078744 sp9 unres 74CFB820; 1108
  175/19010112/46369200 sp11 unres 74CFB820; 1109 161/18980928/42647232
  sp12 unres 0. Absent sets: 1104/1105 828DCF0B; 1106-1109 2F93D9E1 +
  828DCF0B (verified absent on own sections; the L1RA family's hashes).
- BUDGETS MOVED: kStreamingRegionBudgetPlaceholder 18 -> 24 MiB (sprint
  decoded textures reach 19,220,768 and whole-track payloads 68.4 MB,
  over both the 18 MiB decode cap and the 54 MiB 3-region plan); arena
  56,623,104 -> 75,497,600 cascaded through every pin (profile pages,
  switch race/track mains, loader peak tests and texts, contact/stream
  path/whole guards - the whole-guards fixture SHA twice, and the
  probe-computed figures 75497472 / 75497568 / 75497440 which subtract
  headers from the arena). Race command cache 12 -> 16 MiB: the sprint
  wet lamp sets measure up to 14,063,768 bytes (survey-wet).
- Loader peaks measured: 50,659,610 (1102) up to 72,093,457 (1107) against
  the 75,497,600 ceiling.
- DRIVES: probe drives stall (event 2 at 8.3 percent, event 12 at 30.8) -
  the drag follow-up class; all twenty events pinned load-only.
- Tests: tests/sprint_tracks_tests.cpp (guards + load pins, 9 rows,
  drive false), nfsu_sprint_tracks_retail + measure + eight loader-peak
  registrations; refusal lists updated (events 2 and 12 now supported -
  the circuit, drift and drag tests refused them by number; the circuit
  naming pin moved from 1102 to 1101); asset set-list pins gained the
  eight sprint sets (after the drag sets, insertion order). Family run
  25/25 PASS including race_run_tests (event 1 replay under the 16 MiB
  cache) and race_route_tests.
- Docs: MEMORY_BUDGETS (the 24 MiB provenance row), DESKTOP_RACE,
  RACE_EVENTS, SWITCH_ASSETS, TRACK_MATERIALS.
- Gates: running the full default suite, then Switch and Release.
- GATES at the final tree (commit 8d16cc7): desktop default 175/210 with
  exactly the 35 pre-existing failures; Switch 49/49 (ASCII dir
  C:/nfsu-builds/glm-sprint-switch); Release 175/214 with the failure set
  byte-identical to the recorded 39-name baseline; ASCII name/content/
  mode scans clean on the commit (31 files, +535/-69).
- PUSH BLOCKED: Gitea went down again at handoff (ssh 2223 refused,
  https TLS fail - the same connectivity outage as 2026-10-10..12). The
  branch codex/day20261017-glm-sprint and commit 8d16cc7 live in this
  worktree only; push, PR (stacked on #335) and REVIEW_REQUESTED are
  queued for the moment access returns.
