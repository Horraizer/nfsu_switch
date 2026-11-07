# Free drive selection r40

role: worker
agent: root
assignment: owner request for every playable track/car and unrestricted tuning
phase: validating
worktree: build/day20261008-root-combined
saved: local only; build/free-drive-r40/baseline and initial.json
constraints: preserve dirty combined integration and Git publication hold; solo; no console send

Implemented: isolated transient free-driving catalog; authored track/direction
selector, all playable cars, unrestricted compatible visual/performance garage,
per-car session settings and real dressed race binding. Career saves are refused
for this catalog. Free practice suppresses race deadlines/finish and rivals. Solo drag retains
manual gears and the human lane planner with no synthetic opponent cars.

The manual retail gate loads tuned previews for every playable car and every
exposed course through production load/simulation/recording. This is bounded
start coverage, not complete laps of the cross product. Its optional CPU capture
was inspected for the setup screen. Unique visual level4 must not select an
out-of-bounds aero table: original575B30 guards each table to levels1..3;
Ghidra evidence is build/free-drive-r40/root-aero-guard.json.

Remaining: required Default/Release retail gates, SDK packaging, Bash ASCII
scan, sealed NRO/source receipt; console memory, controls and visual acceptance.
Next: finish the manual course gate, then freeze sources and run mandatory gates.
Do not send to Netloader until the owner authorizes it.

## Sealed local r40 2026-10-23T11:26:57.731529+00:00

phase: awaiting-evidence
verified: mandatory Default/Release with retail fixture, SDK and ASCII PASS.
Commands, exit codes and log hashes: build/free-drive-r40/artifacts/BUILD_RECEIPT.json.
Frozen build source: build/free-drive-r40/validation/root1-source-freeze.zip.
The post-build checkpoint has its own hash in the receipt.

Includes the prior full campaign integration and configurable free practice.
Every playable car and exposed authored course passed bounded desktop loading,
simulation and draw checks; menu CPU capture was inspected. No claim of full
cross-product driven laps, original menu parity or Switch runtime acceptance.
No Netloader send or Git publication/index/ref mutation occurred.
Next: owner-authorized console acceptance of selection, tuning, Plus return,
car switching and representative circuit/sprint/drift/drag courses.
