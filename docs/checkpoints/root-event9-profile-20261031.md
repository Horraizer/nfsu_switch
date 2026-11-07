CHECKPOINT
role: worker
agent: Codex root
assignment: Owner requests a profile ready to replay event9 drag
phase: complete
branch: codex/draw-distance-original; publication/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: local-only checkpoint; no profile changed
ownership: Codex root, active

The target platform is not explicit. An asynchronous question asks whether
this is for Switch r87 or the original PC version. Do not modify either
profile while that required distinction remains unanswered.

Original PC files exist in C:/ProgramData/NFS Underground, with profiles1/2.
Previous PC event8 preparation is preserved in build/pc-profile-sync-r1:
root-install-receipt.json and root-event8-candidate.ugd, with backup location
recorded there. Existing original profiles must be backed up before any write.
No game launch/control or new recording is authorized.

Switch r87 uses the separate ASCII SHA256 career.0/career.1 schema implemented
by src/career_save.cpp. Its live base is sdmc:/switch/nfsu-saves/career.
No matching save directory was found at the roots of mounted D/E/F/G drives.
No remote connection or save write was attempted. Retail inputs read-only.

Next: obtain platform answer. For PC, use the existing native save/checksum
research to rewind progress while preserving the current car and backup.
For Switch, locate accessible slots or prepare a valid standalone event9
profile through CareerCatalog/save_career; installation requires SD access.
Keep r87 NRO/source archives sealed and preserve Git/index/board HOLD.

## Switch profile prepared: 2026-10-31

- Owner confirmed Switch r87. Prepared a standalone test profile, not a
  clone of the inaccessible live Switch save. No remote install attempted;
  the known Switch address had neither standard FTP nor ftpd port available.
- Generator root-make-switch-profile-r1.cpp links the unchanged, validated
  r87 application libraries. It loads the actual CareerCatalog, selects
  Civic, marks prior events won, buys the recorded level1 engine/gearbox/
  tyres through catalog APIs and writes both slots using save_career.
- Both slots reload together and independently through load_career. The
  selection is event9 on the map, with no active/played tournament rounds.
  A copied profile begins at round0 and advances all three tournament rounds
  through the real career-domain API. Validation output is
  build/profile-event9/root-switch-profile-validation-r1.log.
- The first probe stopped before writing because it unnecessarily assumed
  the single-drag sender for tournament9. The tournament's sender is empty;
  removed that unrelated assertion. The subsequent generation/load check
  passed. No failed run is presented as a successful validation.
- Final packaging script: root-package-switch-profile-r1.py. Its success
  record is build/profile-event9/root-switch-event9-profile-final-r1.json.
  That record identifies out/switch-event9-profile-r87.zip and both slot
  checksums, verifies ASCII and unchanged r87 production source/Git state.
- Installation remains user-side: close the game, back up the original SD
  save directory, then replace BOTH slots from the archive at the SD root.
  No existing console save was read, backed up or overwritten by this task.
  Original PC profiles and sealed r87 NRO/source archives are untouched.
