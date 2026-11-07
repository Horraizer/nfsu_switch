# Real AI capture comparison

CHECKPOINT
role: worker
agent: Codex root
updated_utc: 2026-10-28
assignment: Owner asks to use the recorded event8 AI states to compare our implementation
phase: implementing
branch: codex/draw-distance-original; publication/index HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; accumulated local tree, no ref operations authorized
worktree: build/day20261008-root-combined
saved: local-only; build/ai-capture-r63/root-entry.json and this checkpoint
verified: Original replay report build/ai-capture-r63/native-replay/report.json reproduces all but seq37
ownership: Codex root; active

Capture ai-20261028T172039Z-1fa2d01b.zip has a validated manifest and complete
stop/detach. The overall sample limit is shared by controller and driver calls
of all three rivals. It is sampled isolated state, not a full chained race.
No new owner drive, live original game, console, Git or board mutation.

Seq37 differs only inside header bytes1..15 and fourth words of 16-byte
path points. Native458150 copies only count, XYZ and identified metadata;
45FAF0 copies the whole block, including padding. Native44C550 publishes the
action path then458F10 resets the action count. Thus action count0 versus
driver count12 at exit is expected, not a base-offset error. Raw mismatch
remains a failure; it is not masked for admission to port comparison.

New tools/original_ai_port_compare.py observes target45FB30, errors45E370,
pedals45FC90 and path continuation45C370 during original replay, then invokes
the actual port component library on extracted inputs. Intermediates are
emulator-derived, separately gated by full native output reproduction; they
are not direct native-intermediate capture and not whole AI parity.
Next: smoke the consumer on this capture, investigate semantic mismatches,
then add fail-closed tests, documentation and required validation.
blockers: none for offline component comparison; whole chained race and
independent/hardware acceptance remain outside the evidence collected here.

## Whole-driver comparison and validation checkpoint

Observed real capture input identity:
ai-20261028T172039Z-1fa2d01b.zip SHA256
652a2af03f505d4345152e5b0a5ed2ead18d365b6c983ae9df35f9798a6b7278.
The input manifest and archive remain untouched. Source/evidence are local
only under the existing publication/index HOLD.

Initial path differences were an adapter error, not a demonstrated port bug:
45C370 sentinel99999 must be resolved by45C550 before ai_path_continue. The
observer now reads the actual resolved step at45C640. No source path algorithm
was changed to fit those initial incorrect comparisons.

On the local source frozen by root-cpp-validation-freeze.json, port-r4 compares
149 whole driver updates,150 targets,150 heading-error pairs,149 pedal pairs,
12 forward continuations and4 fitness calls with zero numeric differences.
These are historical results of this exact real archive and local source,
not a pinned promise about other records. Whole-driver comparison includes
all four retained history rings, controls, direction latch and gear request.
One whole-driver mapping (seq130, no wheel support) lacks an original status
page: native did not consume that page on this branch. It is unsupported,
not filled with a default or counted as a match. The raw seq37 mismatch is
still excluded; no masked semantic comparison replaced its failed verdict.
The real capture comparison deliberately exits1 for these two open records.

AiDriverHistory now exposes validated fixed-size snapshot/restore for offline
isolated tick replay. It retains reset caches and cursor positions. Normal
runtime does not call this API. Targeted Python tests passed, including
observer non-interference, missing-page refusal, real x87 return decoding,
manifest tamper detection and foreign-build refusal. The full required C++
Default/Release configurations are running serially, logs and command receipts
under build/ai-capture-r63/root-validation.json. No C++ files changed after
that validation source freeze. Documentation updates do not change the freeze.

Next: complete the mandatory matrices, verify ASCII/index/source identities,
and record a precise usable-consumer handoff. Whole controller/route-selection
replay and console behavior are not claimed by these component results.

## Cadence gap found after owner asks why console behavior differed

The previous component matches are conditional on original supplied state;
they do not establish that the port produces the same state. A native source
inspection found a concrete missing caller boundary in IntroAiPlanner:
44B1B0's ordinary role2 scheduler selects one rival via frame%8 and requires
at least0.2 seconds since controller4C0 for a full update. Initial missing
action bypasses that gate. Most captured scheduler calls therefore do not
run action predicates, strategy, catchup or speed publication at all.

On an ineligible frame, physics4 returns early. Other physics kinds update
blocking action5 and publish speed, or refresh a side path only when its
maneuver descriptor is2/3, its reference is human and it remains in the
current neighborhood (44A060/57A6B0/57B520). Ordinary descriptor1 keeps the
published path and speed. This is not a blanket throttle of driver/physics.
The port currently runs the full ordinary action stage on every world step,
changing query/RNG cadence and path/speed refresh. It is a confirmed code
integration difference, not yet proof of the complete observed console cause.

Next: add the authentic rival cadence selector and distinguish full/blocking/
human-side-path/retained publications in the live planner. Verify the source
gate against native instructions and recorded scheduler decisions. Default
mandatory tests passed on the snapshot-only pre-cadence tree. Release is
still completing on that same pre-cadence C++ source; it cannot validate the
cadence change that is about to be implemented. Preserve those logs as history
and rerun both full matrices once the new runtime pass settles. Publication,
Git/index and console HOLD remain unchanged.

## Cadence and path publication implemented; final validation starts

Original cadence fixture from ai_controller_cadence_vectors.py passes all
768 declared gate inputs. The real capture consumer matches149 cadence
branches. It compares14 original driver publication calls and149 complete
ordinary control/history updates; all numeric differences are zero after
correcting the publication sum to the actual x87 `(dz*dz+dx*dx)+dy*dy` order.
Native seq37 and unsupported whole-driver seq130 remain excluded explicitly.
The capture's raw full-update gate outcomes are distinct from its call count;
most sampled controller entries took the retained branch.

IntroAiPlanner now preserves prior publications between full decisions,
updates blocking5 without new selection, and can refresh a retained side
maneuver targeting a present human. Full updates preserve native actor order.
New path_published distinguishes an actual publication from an unchanged path
so retained geometry is not accidentally resampled twice. Both ordinary and
drag publications use original459980 redistribution. Snapshot/restore remains
an offline-only API; no ordinary gameplay tick serializes its history.

Targeted driver/strategy tests passed and the cadence-only integrated UG0 and
event8 checks passed. The final cadence+publication tree is frozen in
build/ai-capture-r63/root-source-freeze-r2.json. Its required serial Default
and Release matrices run via root-validate-r2.py, with separate root-r2 logs
and root-validation-r2.json. A fresh build-switch-ai-r63 with tag
ai-cadence-r63 builds via root-nro-build-r63.py; it is not authorized for send.
The earlier full Default/Release passes are pre-cadence history, not acceptance
of this new tree. Only checkpoint/documentation may change during these gates.
The publication/index HOLD remains; independent review and console behavioral
acceptance are unperformed. Original frame origin/RNG initialization, physical
LOD and full route/action/race parity remain unverified boundaries.

## Final local handoff

phase: awaiting-evidence
ownership: Codex root; local implementation/validation finished, console and publication HOLD retained
saved: local-only; no commit, push, fetch, board post, console send or live original control

Required validation on the final cadence+publication source freeze:

- cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground: exit0
- cmake --build build -j 5: exit0
- ctest --test-dir build --output-on-failure -j 5: 284/284 passed, retail included, no skipped test
- cmake -S . -B build-release -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground -DCMAKE_BUILD_TYPE=Release: exit0
- cmake --build build-release -j 5: exit0
- ctest --test-dir build-release --output-on-failure -j 5: 289/289 passed, retail-full included, no skipped test

Exact argument arrays, return codes and logs are in
build/ai-capture-r63/root-validation-r2.json. The final completion receipt
contains those arrays verbatim. Switch configure/build succeeded with tag
ai-cadence-r63; selected package/embedded-shader/shader checks14/14 passed.
The20 recorder-consumer tests passed, and both native fixture files were
regenerated into a fresh scratch directory and matched byte-for-byte.
All1299 source files/paths passed ASCII; HEAD scans returned1 with empty
outputs as required. Index SHA256 and HEAD are unchanged from entry.
The sole post-freeze source edit is this ASCII checkpoint.

Both Default and Release real-capture consumers produce zero numeric
component differences. Cadence coverage is5 full and144 retained branches;
publication14, target150, heading errors150, pedals149, complete ordinary
control/history updates149, forward continuations12 and fitness4. These are
historical results for the identified capture/source freeze, not whole-race
pins. Both consumers deliberately exit1: raw native seq37 mismatch and
unsupported whole-driver seq130 are visible and excluded, not converted to
matches. Optional full-route speed publication was not admitted by the
normal run; its attempted mapping needs route pages that this capture did
not consume. No unseen route cells were fabricated.

The source freeze, comparison report identities, gate receipts and NRO
identity are in build/ai-capture-r63/root-completion-r63.json. NRO location:
build-switch-ai-r63/switch/nfsu_switch_race.nro. It is built and packaged,
not sent and not tested on hardware. The previous r62 NRO remains separate.

The owner's question has two confirmed code integration differences:
full rival decisions previously ran every world frame instead of44B1B0's
cadence, and raw action paths bypassed459980 before reaching the driver.
Both boundaries are now connected and checked against authentic original
instructions and the existing recording. No difficulty/engine parameter was
tuned to make the comparison pass. These fixes do not establish identical
closed-loop racing: physical LOD, original frame/RNG initialization, complete
controller/route decision production and world scheduling remain unverified.
Independent review is not performed under the current publication HOLD.

Next when console work is authorized: send only ai-cadence-r63 with a fresh
log, compare the event8 route/behavior and timing. Keep the raw unsupported
records and source hashes intact. A later AI capture should distinguish real
full controller updates from retained-call samples rather than assuming that
300 captured entries are300 decisions. No new owner recording is needed to
reproduce the component results already stored here.
