# Load-time culling and command preparation

Refs issue 55. This implements the CULL/RECORD part of priorities 1 and 2 in
[FRAME_BUDGET_RESEARCH.md](FRAME_BUDGET_RESEARCH.md). The console motivation
is the historical direct-present measurement at `a1c72dc`, recorded in
[PR #211 comment 4871](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/211#issuecomment-4871).
Desktop results do not establish the Switch frame deadline.

## What is prepared

`PreparedTrackCull` validates containment and topology at load and retains
the original traversal's subtree counts, bounded stack, visible storage
and section summaries. Invalid containment or topology refuses the load;
there is no unchecked fallback. Traversal shares the reference culler's
six-plane box test, instance gates, pruning and child visitation order.
The prepared API deliberately supports the race's default policy: all
valid loaded sections, the existing mask and no projected-size gate.
It does not introduce retail LOD, a shorter distance or a different zone.
The source region must remain immutable and outlive the cache; the race
owner exposes it only as const and cannot be copied, moved or reloaded.

`RaceGpuScene` prepares mesh adjacency, dense source-instance lookup,
track placements, checked group templates, car templates and one combined
upload namespace. Empty groups still count toward the reference list's
far-plane retry threshold even though no hardware draw is emitted for
them. The selection buffer has the existing draw-capacity bound.
The track upload table is moved into the combined namespace and car handles
are rebased in place, avoiding a temporary copy of all track templates.
Geometry, upload fields and ordering are unchanged. Car placement and
lamp state still update from the same animated simulation state.

A normal frame selects checked group/instance handles into reusable
storage, updates camera and car constants, and returns a borrowed command
view. It does not build sets, diagnostic names, upload tables or owning
draw vectors and does not invoke static index validation. The reference
path remains callable with `RaceGpuScene::frame(..., reference=true)` and
still constructs fully checked owning commands. Cache sizing is covered
by `nfsu_race_run_tests`, including the persistent budget and a test pin;
this document does not duplicate those values.

## Consumer interface and lifetime

`RecordedCommands` can now be an owning reference recording or a borrowed
race selection. Consumers must use `resource_table()`, `draw_count()` and
`draw_view(i)` for either representation. The `uploads` and `draws`
containers are private, so direct reads fail to compile. Construction and owning-reference inspection use `owning_uploads()`
and `owning_draws()`; both const and mutable overloads throw on borrowed
frames instead of exposing empty storage.

```cpp
const auto& resources = commands.resource_table();
for (std::size_t i = 0; i < commands.draw_count(); ++i) {
    const auto view = commands.draw_view(i);
    const auto& definition = view.definition;
    // Geometry, program, pipeline and resource handles: definition.
    // Selected instance identity: view.instance.
    // Actual constants: view.placement and view.material.
}
```

The template's instance/placement is a load-time witness, not the selected
instance. `diagnostic_name()` derives the original name on demand; use it
for diagnostics or differential tests, never as per-frame work.

The frame borrows from its nonmovable `RaceGpuScene`. That owner must
outlive the renderer. The next optimized frame invalidates earlier frame
generations, including copied frame objects. Check `generation_valid()`
before consuming a frame; planners/update validation refuse stale views
and accessors also guard them. No CPU view is retained for deferred GPU
reads: the backend copies constants and records commands before another
frame can replace the selection. GPU slot/fence ownership remains the
renderer worker's responsibility. Reference recordings own their command
metadata and do not advance the prepared frame generation.

`combine_race_commands` accepts owning recordings only. Borrowed race
views already represent the combined stream and are refused as inputs.
Only `RaceGpuScene` can attach a prepared source to `RecordedCommands`;
there is no public flag that turns unvalidated user commands into a trusted
race frame. Range, capacity and generation checks remain in the fast path.

The merged `switch/deko_renderer.cpp` uses these accessors and selected
constant references in its uniform writer as well as command recording.
Both the panel and PERF resource counts in `switch/race_main.cpp` use the
same interface. The direct-present path retains its slot/fence sequencing.
There are no changes to presentation, command-buffer strategy, static GPU constants, state memoization or result-screen timers.
In particular, UPDATE still performs its full backend validation; removing
that work is priority 3, owned by the other renderer worker. The zero-index-
visit assertion here covers **CULL/RECORD**, not that subsequent UPDATE.

## Validation and measurement

`nfsu_track_visibility_tests` compares ordered selections and classifications
over changing cameras and checks containment, topology and stack-capacity
refusals. `nfsu_race_run_tests` compares every upload-resource field and
total, every ordered draw identity/state/constant, camera and frame counts
with the reference path. It also checks generation invalidation, invalid
draw handles, rejection of all owning accessors on borrowed frames, and
rejection of borrowed inputs to the owning combine API. The reference path
must return an owning recording before any draw comparison. The animation
guard inspects selected view placements and lamp state at its braking tick.
Replay values and the prepared-cache size are pinned in that test/code,
not here. Its retail frame comparisons assert zero post-load allocations
and zero static validation index visits; a positive reference fixture
checks that the index audit is actually connected.

Run the normal default, Release and Switch gates in CLAUDE.md and
[SWITCH_BUILD.md](SWITCH_BUILD.md). Every retail invocation below is guarded
by a before/after all-file manifest, including failed attempts. The output
directory must be fresh. Run from the worker's ASCII worktree with the
compiler runtime and installed Python on PATH:

```powershell
$retail = 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'
python tools/frame_budget_evidence.py $retail out/sample-default run -- ./build/nfsu_race_run_tests.exe tests/recordings/event1.txt $retail --sample-draws
python tools/frame_budget_evidence.py $retail out/sample-release run -- ./build-release/nfsu_race_run_tests.exe tests/recordings/event1.txt $retail --sample-draws
python tools/frame_budget_evidence.py $retail out/full-release run -- ./build-release/nfsu_race_run_tests.exe tests/recordings/event1.txt $retail --full-draws
python tools/frame_budget_evidence.py $retail out/profile run -- ./build-release/nfsu_frame_budget_probe.exe $retail tests/recordings/event1.txt
```

Run `cmp` on every output directory's `before.json` and `after.json` in Bash.
The sampled and full-draws runs are also CTest's `nfsu_race_run_retail` and
`nfsu_race_run_retail_full_draws` (labels `retail` and `retail-full`) when the
build is configured with `-DNFSU_RETAIL_ROOT=<installation>`; the commands
above remain for their recorded evidence. Never launch retail or write under
its installation or `E:/Games`.

The probe prints load time, the load-time render-preparation interval,
per-stage means, per-frame allocation counts and index visits. It uses the
same one-warmup/repeated-fixed-pose method for both paths; compare the full
FRAME_MS stage sum with its counterpart, not an isolated substage with a
parent interval. Isolated cull/list experiments remain separate data.
The optional index audit is per-thread and disabled in the race app.
Allocation interception requires a static GNU C++ runtime on Windows,
as do the repository's existing allocation-counting tests.

For an independent baseline, use a separate detached worktree at `7898d77`
with only the same probe, static-runtime link option, index audit and
load-observer checkpoints applied. Keep that measurement patch and raw
outputs with the handoff. It must contain no prepared-culler or frame-view
implementation. Run baseline and optimized probes sequentially, without
this worker building or running the full differential replay concurrently.
Report the observed spread and load-preparation delta; desktop timings
are not a claimed console saving.

The coordinator builds the reviewed head integrated with #211's direct
present and persistent results screen, reports the exact integrated commit
and NRO hash, and photographs the results. No nxlink stdout is needed.
Compare CULL/RECORD, UPDATE, frame distribution/maxima, load time and memory
against the same-mode baseline; require EQUAL and both replay hashes.
The branch changes no replay pins or frame-rate target and does not close #55.
