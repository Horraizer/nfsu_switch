# Owner-accepted visuals-r1 development baseline

Date: 2026-10-08
Role: coordinator, existing root session
Base: b009de72c9ac77e53163b4ae4d93eb437681c941
Previous origin/main: 743ebc06b1859e7ef5bdb9ffd2a64789628b0c45

## Owner decision

The owner tested the delivered visuals-r1 Switch NRO, supplied driving media,
and reported a very good result. The owner then requested making this current
build main and using it as the base of subsequent checks. The owner explicitly
stopped reviewer delegation and selected sequential work with direct hardware
feedback. This checkpoint records acceptance as the development baseline,
not complete game fidelity or a fully validated release.

The current instruction authorizes publication of this accepted source baseline
to main. The earlier rejected board-assignment action is not retried or routed
through another agent. No board write is part of this baseline operation.

## Exact delivered artifact

Tag: visuals-r1
NRO SHA-256: e4300bbaac1bfcfcabe9dac117baad606faed934f5e9128bc5e8dd430f8b88e6
Saved local evidence: build-artifacts/combined-20261008-root/visuals-switch-r1
The manifest records the source hashes, NRO identity, command logs and owner
media. The local package includes working.patch and new-source snapshots.
Generated binaries and owner media are not committed.

## Included work

- Combined handling and Atlantic source already present at the base commit.
- Road material, baked color, filtering and reflection-source corrections.
- Selected-bounds projected car shadow and compiled shader interface repair.
- Original-texture projected headlights on ordinary and wet world materials.
- Car environment capture at 128 pixels per face, updating one face per frame.
- Corresponding CPU rendering support, resource validation and affected tests.

## Verified evidence for this snapshot

- SDK Release build, offline shaders and compiled shader interfaces passed.
- Complete Switch CTest: 49/49 passed.
- Affected native track-render, shader-manifest, GPU-upload and race-effect
  checks: 4/4 passed.
- Actual race command validation passed for all six environment capture faces.
- Source hashes matched the packaged artifact manifest before this commit.
- Owner ran the exact NRO on Switch and supplied six stills and a driving clip.

## Open limits carried into main

Full native Default and Release matrices were not rerun for this experimental
combined snapshot; inherited handling assertions and changed visual reference
checks are not certified green. The owner chose this hardware-tested snapshot
as the next development baseline. No checks were disabled or blindly repinned
as part of this decision.

Remaining work includes handling/tyre behavior, tachometer behavior, draw
distance and frame timing, interface work, car glass material appearance,
shadow visibility and original visual fidelity. Dynamic car diffuse/specular
lighting remains approximate. Capture scene categories and live context remain
limited. A 30 fps recording is not an actual game frame-time measurement.

## Next work policy

Keep this exact baseline recoverable. Discuss one task with the owner, change
that task only, run relevant checks and show a PC frame or hardware build.
Do not start workers or reviewers without a new owner instruction. Do not
delete historical branches, worktrees, evidence or the delivered NRO.
