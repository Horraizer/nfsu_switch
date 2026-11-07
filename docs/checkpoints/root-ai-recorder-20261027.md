# AI recording checkpoint

CHECKPOINT
role: worker
agent: Codex root
assignment: Owner request to record rival internal state for original/port comparisons
phase: awaiting-evidence
publication: HOLD; all work local, no Git index/ref or Gitea writes
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
source-set-sha256: 1a78010c86621a867b20eefdfa313f8e2d4e49398a4f3d639077866de40be35e
worktree: build/day20261008-root-combined

## Saved implementation

See docs/ORIGINAL_AI_RECORDER.md and tools/original_ai_recorder.py.
The AI launcher is installed as NFSU AI Recorder.lnk. The existing physics
host gained lifecycle extension points; production game C++ is unchanged.

## Evidence

At the head above, with the uncommitted source fingerprint above, commands
were run from the actual Unicode checkout. Full inputs, source hashes,
results and runtime boundaries are in
build/ai-recorder-r58/root-final-receipt.json.

- python tests/original_ai_recorder_tests.py: Ran14, OK.
- python tests/original_physics_recorder_tests.py: Ran16, OK.
- python tests/original_recorder_tests.py: Ran11, OK.
- python tools/original_ai_smoke.py --retail RETAIL --out build/ai-recorder-r58/driver-final --watchdog: PASS; exact report at that output.
- python tools/original_ai_smoke.py --retail RETAIL --out build/ai-recorder-r58/scene-accepted --scene: PASS; exact report at that output.
- python tools/original_physics_smoke.py --retail RETAIL --out build/ai-recorder-r58/physics-regression --build build --compiler C:/Strawberry/c/bin/c++.exe: PASS live x86 capture/replay/port/cleanup.
- Offline original_ai_replay.py on the generated captured inputs: PASS.
- Bash ASCII HEAD path/blob scans: exit1 with empty output, expected pass.
- HEAD symlink/submodule mode scan: no invalid entries.
- Changed/new source/doc byte and path scans: PASS.
- Installed source hashes and hidden Tk startup: PASS.

RETAIL was the existing authenticated PC1.4 USA installation. No retail game
was launched; fixtures were constructed under the fresh ignored output paths.
The C++ configure/build/CTest gate is not affected by these Python/JS/docs
changes. The existing baseline remains identified by the receipt.

## Remaining acceptance

The owner must make the first driven retail AI recording. Generated fixtures
prove capture/replay mechanics, not acceptance in an actual race. Native
reproducibility does not establish full port AI parity. Sampling has gaps and
is not a chained whole-race replay. Independent review was not performed.

Next: record an ordinary race using the installed AI shortcut, save ZIP, check
its native mismatch/unsupported counts, then run the offline replay and map
accepted decision inputs to port comparisons. No new drive is started by the
agent. This checkpoint and its evidence are local-only under the owner HOLD.
