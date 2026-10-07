# Handling251 F2/F3 numeric-domain correction

CHECKPOINT
role: worker
agent: GPT-6 visible handling251 worker 01a1161e-2ee4-7d21-9d7d-91774b3af1be
updated_utc: 2026-10-07T23:49:05Z
assignment: issue251/item2, confirmed independent65f8 findings F2/F3
phase: local successor; exact-head re-review pending
parent: 65f896d21a74fab7470b07602481b370f18bb3fc
base: 743ebc06b1859e7ef5bdb9ffd2a64789628b0c45
branch: detached WIP; eventual codex/251-handling-tyres-tach-followup; publication held
worktree: owned handling251-followup/nfsu-switch
ownership: same worker; root01a0f5d2 owns wake/correction/re-review/acceptance; reviewer01a11165

## Confirmed findings and correction

The independent65f8 verdict is REVIEW_CHANGES_REQUESTED. The root-preserved
verdict SHA-256 is
0c069e3450918ab0766b04128ed48db733b306b4129be97f9e46dbbfe608a964,
receipt fb0dd363f02b1727f7d967ca978b4548b9d8eab1e88898215fc82d6cbdedc11f.
Read-only reviewer harness inspection and fresh owned cba0/65f8 executables
reproduced both new regressions twice. No reviewer/retail/main file was changed.

F2: a positive accepted double solver_dt of1e-50 narrows to binary32 zero.
The compression-rate division then produces0/0 and the nominal force floor
silently erases support. The successor explicitly refuses external solver
time that cannot remain positive in the component solver's binary32
representation. This is a documented API-domain refusal, not a minimum
gameplay timestep or a tuning threshold. The check precedes caller mutation
and publication. The smallest representable positive float time remains
accepted in a finite flat-ground context. Internal initial preparation still
uses zero rate, while an externally supplied zero step remains rejected.

F3: finite unit_scale of1e-40 makes the finite double bump coefficient exceed
float range. Narrowing then multiplies infinity by zero overflow, poisoning
the otherwise valid spring/preload sum. A zero-overflow strut now does not
divide or narrow the unused bump coefficient. Tiny positive unit scales remain
valid and retain finite positive contact loads with the same normalized
binary32 force. A nonzero overflow must have a representable binary32 excess
and coefficient; an unrepresentable active contribution is refused.
Nonfinite computed rate or nominal force also refuses preparation.

Preparation reports failure to the step. All work remains in its local state
copy until the complete step succeeds, so an active-contribution refusal also
preserves the caller state and telemetry. These guards introduce no force
clamp, replacement tolerance, suspension tuning or gameplay pin/schema change.
The original attachment and nominal force helpers are untouched. Normal
binary32 operation order and immediate partner epoch are unchanged.

The inherited original cached-rate gate for small dt and original overflow
cap remain unrecovered adapter limitations, as recorded in the independent
verdict. These F2/F3 corrections do not establish those missing producers.
The synthetic failures are not demonstrated causes of ordinary F1. ClosedA1
and all inherited handling behavior/hash failures stay distinct and unresolved.

## Meaningful regression evidence

nfsu_player_powertrain_tests now checks:

- positive double time that narrows to zero refuses without changing state
  or canonical telemetry; the next valid recorded future equals the untouched
  control state;
- the minimum positive binary32 time accepts with finite positive support;
  a finite time giving an unrepresentable active compression rate refuses;
- tiny positive SI display scales retain normalized ordinary contact load;
- an active unrepresentable bump coefficient refuses without changing state,
  telemetry or the next valid recorded future;
- an explicitly zero bump coefficient permits excess compression at a tiny
  unit scale, retaining finite positive spring/preload support.

The reproduction harness was copied read-only from the reviewer and scanned
before use. Owned old/fixed executable outputs cover ordinary support, misses,
invalid distance/normals, droop/bump boundaries, lifts, external zero time,
the confirmed tiny-positive-time/unit cases and minimum float time. Every
case repeats exactly. The ordinary cases match65f8 exactly. The short actual
production brake stream and passive strut callbacks also match65f8 byte for
byte. Native witness inputs remain equal, so this correction needs no repeated
original trajectory matrix. Normal source pins independently supported at65f8
remain in the unchanged helper and current Player test.

## Commands and results

All scripts run through standalone Python3.12 from the actual non-ASCII main
workspace, derive their owned source root from the script location, and put
the standalone GCC toolchain on PATH. No non-ASCII command/path is generated.

- python -B build/handling251-worker/handling251-worker-strut-domain-repro-r1.py:
  exit0, PASS own reproduced F2/F3 at pinned cba0/65f8 twice.
- python -B build/handling251-worker/handling251-worker-strut-domain-check-r1.py:
  exit0, PASS19 old/fixed domain cases twice, ordinary15 outputs exact.
- python -B build/handling251-worker/handling251-worker-f1-strut-port-r4.py:
  exit0, fresh actual eight-slice production stream and36 passive callbacks
  twice exact; rows/callback bytes also equal65f8 port-r3.
- python -B build/handling251-worker/handling251-worker-strut-domain-targeted-r1.py:
  exit0 for the harness, nine completed CTests and unchanged focusedA1 twice.

Targeted build command:
cmake --build build --parallel 2 --target nfsu_player_powertrain_tests
nfsu_vehicle_tests nfsu_vehicle_track_tests nfsu_performance_levels_tests
nfsu_drive_tests nfsu_race_session_tests nfsu_race_particles_tests
nfsu_race_audio_tests: exit0.
Each test command is ctest --test-dir build -R ^NAME$ --output-on-failure:

- nfsu_player_powertrain_tests: exit0, 100% tests passed.
- nfsu_player_aero_setup_tests: exit0, 100% tests passed.
- nfsu_vehicle_tests: exit8, 51 vehicle test failure(s).
- nfsu_vehicle_track_tests: exit8, 20 check(s) failed.
- nfsu_performance_levels_tests: exit8, retained decoded-only assertion at
  offset900 after selected/tuned aero setup passes.
- nfsu_drive_tests: exit8, 2 test failure(s); printed top speed alone does not
  prove the conjunctive throttle-phase predicate.
- nfsu_race_session_tests: exit0, 100% tests passed.
- nfsu_race_particles_tests: exit0, 100% tests passed.
- nfsu_race_audio_tests: exit0, 100% tests passed; bare fixture only.
- build/nfsu_vehicle_track_tests.exe --seams-only: exit0 twice, all checks passed.

All four failed suites' exact failure lines equal65f8, including measured
output and unchanged gameplay hashes. That equality is a regression check,
not acceptance of the existing failures.

## Remaining and next

Required full Default/Release configure-build-CTest with every retail fixture
and no skips, merged-current-main validation, non-ASCII build topology and
Switch/hardware are UNRUN and block whole acceptance. Root holds publication
and the heavy slot while Atlantic work owns it. No push, PR/Gitea mutation,
merge, deployment or hardware claim is made.

Freeze the exact successor and source/evidence correspondence, send root the
path list before narrative, and re-review with the same independent reviewer.
Root retains the existing heartbeat and direct same-chat completion/correction
mechanism; the owner is not a relay. Only after F2/F3 and the bounded correction
are accepted should the separate finite next body-force/torque discrepancy
advance. Ordinary F1 and complete issue251 acceptance remain open.
