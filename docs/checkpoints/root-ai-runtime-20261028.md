# Rival action runtime implementation

CHECKPOINT
role: worker
agent: Codex root
updated_utc: 2026-10-28
assignment: Owner requests per-car trajectory category, original path retention and action selection without new owner recordings
phase: local ordinary-action integration validated; full native-runtime/hardware acceptance remains open
branch: codex/draw-distance-original; publication/index HOLD remains active
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; accumulated local integration
worktree: build/day20261008-root-combined
saved: local sources plus build/ai-runtime-r61; no stage/commit/push/board write
ownership: Codex root; active

## Corrected retention interpretation

Static inspection of45AE10 alone omitted the selected strategy's virtual1C
return. Authenticated original strategy1 table6C98D0 and strategy2 table6C98B0
both point to577FD0, which returns true. Strategy3 table6C97FC points to5314D0,
which returns false. Thus unconditional ordinary-racer rebuilding was NOT
proved wrong by the prior audit. Retention must follow this strategy result
and controller4BC. Do not replace original ordinary behavior with a blanket
retain policy. The earlier user-facing inference has been corrected.

## Local work

Individual rider performance categories now feed ordinary road contexts,
driver categories and neighbor road projections. Maneuver horizon7006EC is
separated from projection horizon6B7538. Added ai_rival_action controller
storage using the existing action owner and lazy strategy selection. Its
production wiring now consumes actual pair endpoints and retained contact
records, and publishes earlier actor paths before later controllers.

The new controller remains WIP. Native query/action leaves have been
decompiled into build/ai-runtime-r61/root-native-*.txt. Constructor/predicate,
geometry admission, speed-time and special branches need differential tests
and corrections before a parity claim. Do not deliver this WIP as completed
original behavior. The legacy ordinary recovery block is still present below
the new normal branch and needs removal after the new path is validated.

## Tools and validation

Own hidden Ghidra headless server started on loopback8092 from the existing
authenticated analysis copy of speed.exe. Parent process identity/command
are saved in root-ghidra-process.json. No original game process was launched
or modified. Initial decompilation returned no-function errors; analysis and
explicit function creation subsequently supplied meaningful decompilations.
No early error result is acceptance evidence.

Initial focused compilation completed with warnings. It overlapped later
edits and is not final validation. Rerun after sources settle. Required full
Default and Release configure/build/CTest, ASCII checks and independent
review have not been completed for this change. No new owner recording is
needed for the current code comparison.

Next: add native connected-action fixtures, compare and correct the new
controller/driver publication contracts, exercise event8 and ordinary races,
then run both complete required matrices. Preserve the entry index hash in
build/ai-runtime-r61/root-entry.json.

## Mid-pass update

Individual native default-action geometry now has authenticated generated
fixtures for every supplied grade. New45BAE0 ordered target choice has
256 native selection-control fixtures with declared geometry services;
these are not a geometry or whole-race oracle. Connected contact action12
and blocking query/owner integration pass their focused native witnesses.
A following constructor timestamp error was found and corrected after the
first integration run. Reverse-recovery prediction no longer refuses the
other controllers' queries. Removed the unreachable legacy normal recovery
block from the drag-only branch.

A local event8 run with the real three-rival grid and traffic reached all
three rival finishes. The synthetic human pilot was still running; this
must not be reported as a completed human lap or native race-time parity.
A permanent retail integration test is being added for UG0/event8 and
post-finish rival stopping. Its first build lacked the frontend dependency;
that is being corrected, not recorded as a runtime pass.

The original ordinary and drag strategy both force path rebuild. There is
no evidence warranting a blanket retain policy for ordinary racing. Shared
world RNG/caller order, live type9 obstacle-object admission and actor reaction
timers remain wider native-runtime boundaries; full original AI equivalence
has not been established by the new component fixtures. Do not claim it.

An initial complete Default run is still active and has no reported failure
so far, but source edits continued during it. It is not the final settled-tree
gate. Repeat final configure/build/CTest in Default and Release after fixes.
The publication/index HOLD remains active; no board or Git mutation was made.

## Settled-tree validation checkpoint

The final focused run is root-focused-test-r12.log: all selected native path,
action and strategy tests, UG0/event8 rival-finish integration, and retail
read-only fixtures passed. Native default-action fixtures now include every
ordinary category. The original supporting bounds/category routines needed
for the highest category were explicitly declared in the generator; no
semantic hooks were added there. The shared world clock now supplies both
planner and action lifetime stamps. Traffic is excluded from racer-only
contact-action admission because original actor1C is absent for traffic.

The first full Default attempt ended with only the after-manifest fixture
failing: my overlapping focused run consumed that build directory's shared
manifest. That run is not a pass. Two later pre-freeze gate jobs were stopped
by verified PID/start-time/command identity after the final metadata fix.
Their partial results are not acceptance evidence either.

Final-r2 Default and Release jobs are now running from the same settled C++
source. They use separate build directories and no second CTest run in either
one. The source freeze and byte scan are root-source-freeze.json. The Git
index hash still matches entry. HEAD ASCII path/content scans returned empty
with exit1, and no symlink/submodule mode was found. Final results remain
pending. No hardware, merge, publication or independent review is claimed.

Scope and unverified native runtime boundaries are recorded in
`docs/AI_RIVAL_ACTION_RUNTIME.md`. This checkpoint supersedes earlier WIP
notes only where a result above explicitly confirms it.

## Validation progress at 2026-10-28T11:40Z

Default final-r2 finished every test. Its sole non-pass was the platform
source scanner timeout from the earlier dual-configuration CPU load. The
same test passed in a separate Default CTest retry after the full run had
ended; root-default-platform-retry.log records the command/result. The
original Default full-run output and LastTest log remain preserved, including
the timeout. No result was silently changed to a pass.

Release configure and complete build passed. The overloaded Release final-r2
CTest was stopped by verified process identity, then the full Release CTest
was restarted with four jobs (root-release-ctest-final-r3.log). Its platform
scanner and first completed full-draw controls passed. Release is still
running; do not record final acceptance before it finishes.

The last traffic/racer metadata fix followed focused-r12, so that focused
pass is supporting evidence, not the final evidence for that edit. The
frozen Default matrix includes and passes both actual rival-finish tests
and the native action/path/strategy tests with that edit compiled. Only
checkpoint/document files have changed since the code freeze.

## Final local handoff

Validation ended on the frozen C++ tree. The complete Release run passed,
including retail-full. Default completed all tests with its one source-scanner
timeout; the unchanged scanner passed its separate retry. Both full-run
retail after-manifests passed. No skipped retail test was accepted.

Commands and results (logs in build/ai-runtime-r61):

- cmake -S . -B build -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground"
  Result: exit0; root-default-configure-final-r2.log.
- cmake --build build --parallel 6
  Result: exit0; root-default-build-final-r2.log.
- ctest --test-dir build --output-on-failure --parallel 6
  Result: 283 passed, platform-neutrality timeout; root-default-ctest-final-r2.log.
- ctest --test-dir build --output-on-failure -R '^nfsu_platform_neutrality$'
  Result: 1/1 passed; root-default-platform-retry.log. No unresolved Default test.
- cmake -S . -B build-release -G Ninja "-DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" -DCMAKE_BUILD_TYPE=Release
  Result: exit0; root-release-configure-final-r2.log.
- cmake --build build-release --parallel 6
  Result: exit0; root-release-build-final-r2.log.
- ctest --test-dir build-release --output-on-failure --parallel 4
  Result: 289/289 passed; root-release-ctest-final-r3.log.
- Required Bash HEAD ASCII path/content scans: both empty, exit1 each;
  no forbidden entry modes. Working tracked/untracked source byte/path scan:
  no violations. Generated automation was exercised from this actual checkout.

Exact frozen-source comparison, native fixture provenance, all source hashes,
index identity and the non-erased failed/retried results are saved in
root-completion-r61.json. No compiled source changed during the final matrices.
Earlier helper/generator failures and interrupted overloaded runs are not passes.

Implementation scope: per-car ordinary path category, actual strategy rebuild
condition, retained action owner and connected ordinary action selection. Native
component and real three-rival finish/stop checks cover the implemented domain.
The module's supporting factories/driver/world adapters must not be described
as full original-race parity. Remaining broader work includes original shared
RNG/caller order,44C2E0 reaction timers, real type9 object admission and the
human intent path. The API supports alternative blocking priorities; the
ordinary runtime adapter still supplies its standard mode, so authored
alternative campaign-mode metadata is a remaining integration boundary too.

No new owner recording, original live game launch, hardware check, NRO send,
independent review, commit, push, merge, board post or index/ref mutation was
performed. Publication remains on the owner's HOLD. This is a local worker
handoff, not an independent REVIEW_PASSED verdict or a full-AI acceptance claim.
