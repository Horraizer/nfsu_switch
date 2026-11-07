# Original loading presentation - root solo

CHECKPOINT
role: worker
agent: Codex root
updated_utc: 2026-10-23T09:33:36.571444+00:00
assignment: Owner asks current campaign count and original loading instead of black technical rows.
phase: awaiting-evidence
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; owner combined dirty integration with pre-existing MERGE_HEAD
worktree: build/day20261008-root-combined
saved: local frozen source and sealed r38 NRO; Git publication hold preserved
verified: Default/Release retail and SDK gates; existing retail loading CLI cache/reference check and four rendered frames. Exact commands/results in BUILD_RECEIPT.json.
remaining: Switch loading/menu/cancellation and tournament-next-round runtime acceptance; independent review.
next: owner-authorized combined r38 Switch run. Do not send until owner requests.
blockers: hardware evidence pending; no implementation/gate blocker.
ownership: root worker, solo.

Campaign connection is events1..30 and separate event0. Supported race data
beyond30 does not make those events available in campaign. Existing
rewards/unlocks/tournaments remain connected; complete original whole-game
frontend parity is not claimed.

Only event0 previously enabled RaceLoadingArt. Normal frontend startup,
career preparations, free drive, restart and tournament round loads now use
that existing authenticated PC_Loading generic art. Screen owns its transient
cache; LoadingScreen reuses it across outer preparation/run scopes and clears
it before GPU upload or on exceptions. Explicit diagnostics retain their
text/log path. No GPU memory limit, game audio or progress percentage changes.
A synchronous operation may hold the current loading frame; an independent
background loader/animation thread is not part of this change.

The original generic artwork and four activity positions are saved in
build/loading-original-r38/root-art-loading0.tga through loading3.tga.
The existing loading CLI also compares cached/reference draws and stride
padding. A PNG preview is root-loading-preview.png. See ../LOADING_PRESENTATION.md.

Source inventory/archive: build/loading-original-r38/root1-source-freeze.json
and root1-source-freeze.zip. Build/NRO receipt:
build/loading-original-r38/artifacts/BUILD_RECEIPT.json. HEAD alone is not this
dirty integration. This checkpoint is separately hashed after source freeze.
No Git stage/commit/push/merge/checkout or ref/index mutation occurred.
The sealed NRO remains unsent; r37 camera/results changes remain included.
