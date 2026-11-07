# Introductory race upload memory correction

- Owner reproduced the SD launch failure through nxlink using unchanged
  `ug0-live-r1`, source `9c3c3b0b61078fdf1d3c160fa13282f6ebcd147d`.
- Root evidence: `build-artifacts/draw-distance-20261008-root/`
  `nxlink-ug0-live-r1-run1.log`. Preparation reached audio and image upload;
  renderer refused `depth image: GPU memory budget exceeded`, returned to
  frontend, and later exited normally. This was not a confirmed process crash.
- Texture staging remained physically allocated after synchronous upload and
  ledger retirement. Four-car preparation exhausted the renderer budget when
  creating depth targets. No allocator/OOM failure was reported.
- Release texture upload scratch only after submit has waited for its fence,
  ledger staging is retired, padded staging released and command list cleared.
  Release HUD atlas scratch after its own synchronous upload. Clear borrowed
  handles, destroy the unique owner and subtract the actual block size.
  Keep render images/buffers, budget limits and visual settings unchanged.
- Add requested/resident/limit diagnostics on budget refusal and actual released
  byte counts on upload retirement. Both records are emitted only at loading.
- Actual SDK build passed. Targeted CTest: race NRO, embedded shaders and
  frontend profile contract, 3/3 passed. Log `ug0-memory-sdk-r2.log`.
- PC simulation was not rerun: this change only touches Switch deko3d lifetime;
  PC rendering does not exercise it. Earlier PC checks did not prove console
  allocation headroom. Hardware menu, UG loading, driving and menu re-entry
  remain required. No full native matrices or additional reviewers requested.
- Candidate `nfsu-ug0-live-r2.nro`, SHA-256
  `49dcadca1db1600edf7bac13ec5445aeb637fbedaaf92a97c2fb15cb76e18975`.
- This remains the prototype UG0 race with stock rivals and port route driver.
  No original AI, career or hardware acceptance claim. Local only; no push/merge.
- Next: owner confirms fresh Netloader readiness, send r2, preserve its log and
  verify upload retirement, depth creation and actual race/menu lifecycle.
