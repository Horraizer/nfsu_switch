# Tournament startup and results overlay recovery

CHECKPOINT
role: worker
agent: Codex root
updated_utc: 2026-10-23T09:09:27.776339+00:00
assignment: Owner r36 startup camera and return to menu after first tournament round; solo.
phase: awaiting-evidence
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; owner combined dirty integration with pre-existing MERGE_HEAD
worktree: build/day20261008-root-combined
saved: local frozen source and sealed NRO; Git publication hold preserved
verified: full Default/Release retail gates, SDK gates, original bounded playlist/lens/projection witnesses, all three tournament startup rounds through GO. Exact commands/results in BUILD_RECEIPT.json.
remaining: owner Switch startup framing and results-to-next-round acceptance; independent review.
next: owner-authorized Switch tournament run, inspect RACE_START, post.atlas, post.shown and tournament round transition in network/SD logs.
blockers: no current implementation/gate blocker; hardware evidence pending.
ownership: root worker; no delegation.

## Diagnosis and changes

The r36 run1 network log reports first-place finish in event6 round0 followed
by post.failed: HUD image: GPU memory budget exceeded. The entire cumulative
frontend atlas was uploaded alongside retained race geometry. The compact
post-race atlas contains only the original two fonts and two result sprites;
all result-page CPU pixels, GPU geometry/colors and sampled texture bindings
are compared by nfsu_frontend_retail. GPU limits and the live race background
are preserved. Earlier command-capacity corrections remain included.

Ordinary road/tournament races previously had no connected original start
camera. The new path selects a stable variant from the original grid-specific
INGAMEB playlist, reads authored EAGL camera clips and timing, and anchors at
the correct forward/reverse start marker. Original authored lens conversion
also replaces stale chase projection overrides. Every rival now receives the
same cinematic interval as the human. Integration tests run all three event6
rounds through the cinematic and common GO, keeping the grid held until GO.

Original RNG history, complete road crowd/car-motion scripts and complete
original chase mover equivalence are not claimed. Hardware acceptance is
still required. The existing UG0/drift crowd paths remain separate.

## Evidence and recovery

See ../RACE_CAMERA_ORIGINAL.md. Source archive and inventory:
build/tournament-start-r37/root7-source-freeze.zip and root7-source-freeze.json.
Build/NRO receipt: build/tournament-start-r37/artifacts/BUILD_RECEIPT.json.
The receipt records exact commands, results and hashes; HEAD alone does not
identify this dirty owner integration. The source freeze precedes this
checkpoint-only document, which is separately hashed in the receipt.
No stage, commit, push, merge, checkout or ref/index mutation was performed.
The NRO is unsent at sealing; no SDK pass is console-runtime acceptance.
