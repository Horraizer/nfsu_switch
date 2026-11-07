# Continuous drag RPM audit - owner follow-up

CHECKPOINT
role: worker
agent: GPT-6 root, solo
assignment: Owner asks to verify continuous neutral RPM, launch, acceleration,
shifts and tachometer against original code, beyond isolated function checks.
phase: implementing
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: runtime root/build/day20261008-root-combined
saved: Local uncommitted integration; previous r7 NRO/source ZIP preserved.

No agents/reviewers/new branches/publication. Owner deferred console upload.
Retail read only. ASCII technical artifacts. Minimal focused validation.

Continuous engine/clutch bench under build/drag-rpm-audit-r8 runs780 frames
per authentic CIVIC/SKYLINE record,4 solver slices/frame. The same processed
throttle and prescribed wheel-speed history feed original PE and compiled
current libraries. Independent persistent engine/gearbox state on both sides,
not per-case resets or per-frame synchronization. Original engine/gearbox
constructors, neutral request, real Euler kernel/component update and RPM
publication; no semantic hooks. Controller shaping is supplied on this seam.
Prescribed wheel history is NOT original body/tyre/track production, so this
bench does not prove whole-player acceleration or original full race parity.

Before comparison: first divergence at rev-cut timing, after exact initial
RPM rise. SKYLINE frame48 differs in cutoff enable; physical RPM frame49.
CIVIC physical/exported RPM diverge frame85. Full before outcomes and source
are preserved as comparison-before.json, probe-before.cpp/exe,
CIVIC/SKYLINE-portable-before.txt, compare-before.py, compare-r3.log.
Original0x46CC5E/46CC8C reads global world clock73AD34*0.00025, held fixed
across solver slices. Port used simulation_seconds incrementing each slice.
That changes disable/re-enable phase and subsequent RPM. Narrow drag-only
fix: grid engine and ordinary drag engine now use existing drag_clock/clock
for cutoff scheduling. Non-drag clock path unchanged in this scoped fix.

Native harness construction range omissions in compare-r1/r2 logs are
explicit failed harness attempts, not passes or killed cases. Third attempt
completed both full timelines and reported the original/port divergence.
Earlier comparison runner did not exit nonzero on mismatches; comparison
report is authoritative and no pass was claimed. Current runner explicitly
exits1 if either timeline differs.

Current-source PC build running. Next: repeat same timelines after clock fix,
classify any remaining differences, add a focused regression and verify
production release path. Freeze combined NRO only after source corrections
and focused SDK gates. No unchanged matrix expansion or tolerance changes.
blockers: none for local work; owner pause blocks console upload only.
ownership: root active solo.


Final local result:
- Corrected cutoff clock in src/vehicle_track.cpp and src/vehicle.cpp only;
  applies to drag. Existing drag clock already feeds quality/heat/pulse.
- Same timelines after correction: CIVIC780 and SKYLINE780 persistent frames,
  all12 emitted columns bit-identical, including physical angular speed,
  published RPM, current gear, phase, clutch, throttle multiplier, heat,
  quality, boost timestamp and absolute cutoff expiry. comparison-after-exact
  is the exact result; compare-after-r1 process exit0. No row resynchronization.
  Each original frame executes4 actual Euler slices; no semantic hooks.
- Independent source-state/input bench limits remain: external prescribed
  wheel-speed history, simplified engine/clutch load seam, supplied processed
  throttle; not original body/tyre/track production, whole-car acceleration,
  live original clock/cadence configuration, or full race trajectory proof.
- Actual production adapter regression added to career_races_tests behind
  NFSU_DRAG_CUTOFF_PROBE: active timer must live in the world-clock interval,
  with source maximum duration. A gate that never reaches cutoff refuses.
- cmake --build build --target nfsu_player_powertrain_tests
  nfsu_career_races_tests nfsu_race_hud_tests -j6 PASS, pc-build-r1.log;
  final focused fixture build PASS, pc-build-r2.log.
- nfsu_player_powertrain_tests PASS, powertrain-final.log;
  nfsu_race_hud_tests PASS, hud-final.log.
- NFSU_DRAG_CUTOFF_PROBE=1 NFSU_CAREER_SCENE_CHECK=1
  NFSU_CAREER_TEST_DIFFICULTY=0 nfsu_career_races_tests RETAIL4 6000:
  PASS, cutoff-finish-final.log. Actual CIVIC/3 rivals, scene command validation,
  complete bounded driver finish,66 active cutoff observations on world clock.
- NFSU_DRAG_LAUNCH_PROBE=1 NFSU_CAREER_SCENE_CHECK=1
  NFSU_CAREER_TEST_DIFFICULTY=0 nfsu_career_races_tests RETAIL4 600:
  PASS, launch-final.log. GO quality equals original request and reaches
  first-gear torque boost. The first benchmark's prescribed low wheel speed
  is not a live acceleration or player-feel acceptance result.
- build/drag-rpm-audit-r8/build-switch-drag-rpm-r8.sh: fresh SDK build exit0,
  selected7/7 packaging/shader/GPU-contract gates PASS, switch-build-r1.log.
- Bash ASCII/entry-mode/whitespace scan PASS, ascii-final.log. New source and
  automation byte/path scans and snapshot ZIP readback PASS in freeze receipt.

Frozen combined build identities: build/drag-rpm-audit-r8/rpm-r8-receipt.json.
Previous r7 and its exact source retained. Both original and portable timeline
rows, before failures and after exact results retained in the evidence tree;
final evidence ZIP includes authenticated provenance and reproducible bench.
Main checkout clean. No agents, reviews, branch/ref move, merge completion,
source push, release publication or console upload.

phase: awaiting-evidence
remaining: Owner Switch verification; complete original body/track acceleration
and whole-race equivalence still unverified. The owner asked for a check; it
found and fixed a concrete limiter timing defect. Do not describe the bench
as a full original car dynamics acceptance. Prior hardware upload pause stays.
next: On fresh owner send authorization, upload sealed drag-rpm-r8 with logging.
blockers: Owner upload pause for hardware; no extra software gate failure claimed.
ownership: root owns the saved candidate and follow-through, solo.


Evidence preservation: first native row files were replaced by the after-run.
Replayed the saved original before-run script with probe-before.exe into a
separate before-recovered directory. Before report and all portable rows
match the saved originals exactly; native rows are recovered there. This
reproduction is recorded as recovery, not a new independent model verdict.
Causal negative reproduces605/359 differences; after correction exact0/0.

Owner explicitly authorized send after local completion. Prior upload pause
is superseded for sealed drag-rpm-r8 run1 only. Frozen artifact identity
verified against rpm-r8-receipt.json before sending. nxlink72576 started
2026-10-18T16:57:42.3525811Z; server active confirmed, capture still running.
Upload identity/args/log paths: build/drag-rpm-audit-r8/upload-drag-rpm-r8-run1.json.
Game/menu/drag behavior not yet confirmed by this network upload result.
next: Read this run's log when the owner finishes; verify PID/start before
acting on its process. Preserve frozen source/NRO identities and earlier runs.