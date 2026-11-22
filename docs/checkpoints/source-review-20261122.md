# Physics and source review, 2026-11-22

CHECKPOINT

- Worker: Codex, public hard-opponent-pace branch, draft PR 6.
- Base: 7656e7ea2375b4b52bed412c6738b8e36db7918d.
- Evidence: PC 1.4 USA speed.exe, SHA-256
  c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
- In 46CA90, heating tests the previous temperature strictly below one.
  It can cross one on the last update. See original_drag_engine in
  nfsu_player_powertrain_tests. In 46D0C4..46D101 the multiplier is
  unclamped. The asymmetric final-drive paths are documented on VehicleHandling.
- The contact regression exercises a pinned pair, one pinned body, all
  unusable box axes and a standalone prop supplied with a larger hull.
  Queued contacts between two pinned props already bypassed the standalone
  inverse-mass division. See nfsu_world_object_contact_tests.
- 59AF20 bounds its initial route search to 100 entries. Subsequent ordinary
  traffic spawns traverse the stored routes. Actor slot IDs are separate.
- AI observations and native fixture scope: docs/AI_ORIGINAL_MAP.md,
  nfsu_ai_driver_tests, nfsu_ai_path_tests and nfsu_ai_track_tests.
- The diagnostic free-camera yaw rotates about the same axis used to recover
  its right vector. See test_camera_moves in nfsu_track_probe_tests. The KR
  logo is square; nfsu_kr_vinyl_retail covers the playable car roster.
- StereoRing waits on space under its mutex and closure wakes blocked writes.
  No console timing measurement attributes the Lite stutter to this primitive.
- At ce6149b, the full host run reported 27 failures out of 362 tests in the
  default configuration and 28 out of 365 in Release. Both failure sets were
  present in the earlier run. Event 19 parity remains open.
- Packaging correction for ce6149b: building nfsu_switch_race linked the ELF
  but left the history-source-r74 NRO beside it. The former package check
  looked for the configured tag only in the ELF. The NRO check now also
  reads the tag from the packaged read-only segment and rejected that stale
  artifact. Building nfsu_switch_race_nro produced the contact-heat-audit
  package. Its text, read-only data, initialized data and GNU build ID were
  compared with the linked ELF. Console execution remains unverified.
