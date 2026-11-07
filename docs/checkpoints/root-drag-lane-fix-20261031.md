# Drag lane-change corrections

role: worker
agent: Codex root
assignment: Fix all three confirmed source audit discrepancies
phase: NRO built; mandatory gates passed; console acceptance/review pending
branch: codex/draw-distance-original; Git/index/ref/board and review HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: local-only; build/fidelity-r88 and this checkpoint
ownership: Codex root, active; no delegation

The owner explicitly authorized correction after the source audit. Evidence
and original function identities are in root-drag-lane-audit-20261031.md.
Retail remains read-only. No original-game drive or computer use is required
for this stage. No console transfer has been authorized for the new build.

Scope: preserve short left-stick and D-pad lane commands without buffering
every analog magnitude; use the original speed-dependent retained horizon for
the human drag controller; choose the manual lane on the retained road before
the common context update. Keep rival category escapes and the raw retained
human action path separate from the resampled driver publication.

Before-source copies and Git/index identity: root-entry-r88-r1.json.
Required Default/Release retail gates, SDK/package checks and ASCII validation
will run on frozen source before packaging. Natural original/Switch lane-change
parity and independent review remain unverified, not implied by those gates.

## Implemented and checked

- DriveButtonQueue optionally retains calibrated axis direction transitions.
  Magnitude-only changes never enqueue a state. DragPadInput reads historical
  left X and D-pad transitions, keeping the newest HID magnitude authoritative.
  Gas, unselected axes and other nonselected buttons stay fresh. Right-stick
  shifting, R/A, pause, reconnect and source/style rules remain in force.
- Human drag calls the same original road-horizon producer as the racers.
  Initial and retained horizons remain distinct. The read-only human snapshot
  permits a production-caller test against the existing native supplied-speed
  witness; it does not claim natural whole-race timing or lane-change duration.
- Human manual selection consumes the retained road before the common context
  update. Its first lazily created road is bootstrapped first. Manual changes
  retain the compact chain; the common unavailable-lane escape is not undone.
  Rivals retain their separate initialization/escape path.

`tools/drag_lane_step_native.py` executes unchanged4605E0 and45D800 on an
original-initialized road, supplied pose/events, and the actual drag vtable.
It stops at45D908 before462430; no semantic stage callbacks were used.
The C++ manual lane state matches every row in its test fixture. This bounded
manual-stage evidence is distinct from a complete human controller or race.

Targeted commands/results are in root-target-r3.json and its fresh logs.
The real event4 and all three event9 grids pass pad-to-wheel steering checks.
The event9 moving-state tests advance the retained road and compare the manual
state before common admission. Their explicit after-context negative control
produces different exclusion bounds, so these cases distinguish the old order.
The strengthened witness assertion is included in the mandatory frozen gates.

New native-stage CTest is registered beside related retail checks with the
read-only retail fixture. No Git/index/ref/board writes, delegation or upload.

## Frozen validation checkpoint

Source freeze: build/fidelity-r88/root-code-freeze-r1.json. The corresponding
change manifest also preserves the preceding task's original-world-recorder
changes; those are not newly attributed to this lane correction.

Both host configurations/builds have passed and their full CTest runs are
active under root-host-gates-r88-r1.py. The SDK configuration, NRO build and
selected package/shader CTests passed under root-switch-build-r88-r1.py.
Exact commands, exit codes and fresh log paths are recorded in the validation
JSON files beside the freeze. Their source-hash checks bracket each phase.

No source edits are permitted during these measurements. Only local artifact
packaging and this checkpoint proceed while the mandatory host tests finish.
Final acceptance is recorded separately by root-final-r88-r1.json once every
required gate passes. The console artifact is not yet transferred or accepted.

## Mandatory gates passed and local handoff

Both full host configurations, builds and CTest runs completed successfully
with the retail root, no skipped retail test, and the required toolchain on
PATH. The exact command arrays, exit codes, timestamps and output logs are in
root-default-release-validation-r88-r1.json. The new native manual-stage test
and the production call-order witnesses passed in both configurations.

SDK/package/shader command arrays and successful exits are recorded in
root-switch-validation-r88-r1.json. The actual NRO is in
build-switch-fidelity-r88-r1/switch/nfsu_switch_race.nro. The local package is
out/nfsu-r88.zip. root-final-r88-r1.json seals its hashes and the source archive
after ASCII scanning, source-freeze comparison and Git/index identity checks.

The preserved source includes the preceding PC-recorder assignment as well as
this correction. No retail input was modified. No original game was launched,
and no console transfer or independent approval was performed. Git publication,
board writes and independent review remain on the owner's explicit HOLD.

Unverified: natural original/Switch lane-change and whole-race equivalence,
moving train activation from earlier work, and console runtime acceptance.
Next authorized boundary: owner tests this local candidate when available;
compare its lane/shift behavior and, when a PC recording is available, the
recorded original controller decisions. Do not substitute these bounded
function/context checks for a claim of whole-race parity.
