# Switch frame profiling (#55), first measured change

This work starts #203's performance step from PR #199 round-2 head
`164e67319d61`, which contains main `17e91ad`. It is the CPU recording pass,
not evidence that the console now reaches 60 FPS. No simulation, culling
policy, material order, distance limit, triangle or texture is reduced.
The loader peak reduction belongs to #204 after #55.

## Baseline retained before optimization

[Console drive 4401](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/199#issuecomment-4401)
at `e65960b` measured 97.20 ms per driving frame: SIM 8.46 ms (six catch-up
ticks), RES/HASH 0.82, CULL/RECORD 52.14, UPLOAD 4.48, DRAW 11.19,
TRANSFER/COPY 8.04 and previous HUD/PRESENT 11.73. That is about 10 FPS,
not 60. The 31.93 ms photo after replay finish has stopped simulation and
culling and cannot replace this baseline.

Before implementing the cache, stage timers were added to the unchanged
RaceGpuScene::frame path. Release GCC 13.2 on this Windows host, after one
warmup and over 120 identical starting-pose frames, measured these means:

| Stage | Original milliseconds |
| --- | ---: |
| Cull and camera | 0.249104 |
| Draw-list construction | 0.346605 |
| Car recording | 0.045333 |
| Track recording | 2.318870 |
| Combine and execution checks | 0.505441 |
| Sum | 3.465353 |

The track recorder rebuilds its upload table, validates the complete
resident scene, rebuilds resource maps and repeats invariant group work.
Its inputs do not change while this race scene remains loaded. That is the
measured bottleneck addressed here. PC times are not Switch estimates.

Three interleaved runs in the same allocation-counted binary retained this
comparison (milliseconds, 120-frame means per run):

| Run | Reference track | Cached track | Reference stage sum | Cached stage sum |
| --- | ---: | ---: | ---: | ---: |
| 1 | 2.302540 | 0.246333 | 3.457721 | 1.428656 |
| 2 | 2.441370 | 0.341494 | 3.684880 | 1.766957 |
| 3 | 2.535730 | 0.275634 | 3.825929 | 1.492895 |

The fixed-pose live C++ byte counter was stable at 43402887 on the
reference path and 43455111 on the cached path after warmup. Both hold the
new cache; the 52224-byte difference is per-frame vector capacity. This is
not an allocator-arena measurement. Timings vary with host load, which is
why correctness and resource limits, rather than these milliseconds, gate
regressions.

## Change and ownership

The race scene prepares one validated command template per placed track
group, using the original recorder and a representative instance per mesh.
A frame uses the original culler and checked draw list, selects templates
in the same order, and copies each selected instance's exact transform and
identity. Camera constants still come from the same frame options. Combined
command validation and backend update validation still run. Car recording,
resource uploads, shader state and GPU synchronization are unchanged.

The cache borrows immutable scene payloads. RaceGpuScene now privately owns
that scene, exposes only a const view, forbids copying and refuses reload
after success. A caller cannot replace its buffers while templates still
point to them. The `reference` frame option retains the full original
recorder for differential tests, not as a different quality setting.

The metadata/capacity estimate is 2220547 bytes on the desktop ABI, bounded
by 8 MiB. No vertex, index or texture payload is duplicated by the cache.
The console panel includes this estimate in persistent payload accounting
and reports it separately. The combined car/course/cache estimate is
5435093 bytes against 64 MiB. C++ allocator overhead remains distinct from
these estimates and from the allocator readings.

## Reproducible checks and regression thresholds

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
build-release/nfsu_race_run_tests tests/recordings/event1.txt X:/Games/NFSU --profile-reference
build-release/nfsu_race_run_tests tests/recordings/event1.txt X:/Games/NFSU --profile
build-release/nfsu_race_run_tests tests/recordings/event1.txt X:/Games/NFSU --sample-draws
```

Bracket every retail invocation with the full 476-file SHA-256, size and
mtime manifest. Inputs remain read-only; no derived game asset enters Git.
The optional profile modes perform one warmup and 120 measured frames.
Their clock is supplied through TimingService by the desktop test; the
neutral race library reads no machine clock itself.
The test counts live requested C++ allocation bytes, including aligned
new/delete; it does not measure C malloc, newlib overhead or GPU blocks.
MinGW links this test's libstdc++ statically so Windows DLL allocation calls
do not bypass the executable's replacement operators.

Stable thresholds are enforced as failures, not timing assertions:

- Zero additional retained C++ bytes across the 120 identical frames after
  warmup; both cached and reference paths satisfy this independently.
- Cache metadata estimate at most 8 MiB and combined persistent payload at
  most 64 MiB; the existing stream/GPU/uniform limits remain unchanged.
- Every sampled frame has exactly the same ordered commands, draw identity,
  upload resource fields and totals, resource references, ranges, program/state,
  placement, material, camera, viewport, clear color, visibility counts,
  triangle counts and far plane as the original recorder. No tolerance or
  visual approximation. The event 1 cache estimate is pinned at 2220547 bytes.
- All 48336 simulation/residency step hashes and the exact two-lap result
  remain the desktop pins. Existing limits remain 2047 draws / 8192
  resources. The stock course's sampled maximum remains 1824 draws.

The repeated fixed-pose sample tests retention, not a proof of no leak on
all paths. Frame timing is reported rather than made a machine-dependent
CTest threshold. Three interleaved reference/cached runs and before/after
retail inventories are retained in the delivery evidence archive.

## Heap growth and next console measurement

deko3d's GPU blocks come from the same newlib heap and are included in
MALLOC IN-USE NOW. The after-load sample is before renderer creation.
Photo 4401's increase is 96640944 bytes, of which 86659072 is GPU blocks;
9981872 remains for readback, commands and race state. The two console
samples in comments 4401/4470 do not establish a growing per-frame leak.
See SWITCH_RACE.md for exact sample definitions and the known loader
peak overrun. This change does not claim to fix that separate #204 peak.

Run the new tagged NRO through the same driving/replay views, photograph
CULL/RECORD, full frame, draw counts and heap values, and compare the replay
EQUAL result. The diagnostic renderer deliberately stays intact for this
first comparison. Once the CPU change is measured on hardware, the next
#55 passes are direct GPU presentation without full-frame readback or CPU
text, safe overlap with per-frame fenced storage inside the budgets, and
state/batch reductions that preserve transparency and equal-depth order.
A compact GPU HUD must preserve countdown/lap/result visibility when the
large diagnostic panel is disabled. None of those presentation changes is
represented as implemented here; 60 FPS remains the measured target.
