# Event-1 track slice on Switch (#54)

`nfsu_switch_track.nro` draws event 1's track 1003, STREAML1RA, through
deko3d. It checks the #47 **first-lap** manifest before any track parsing,
using the same read-only size and SHA-256 checks as `nfsu_switch_assets`.
It defaults to `sdmc:/nfsu`; an optional first argument replaces that root.
No game data is bundled in the NRO or written to the card. RomFS contains
only the three checked, offline-compiled shaders, including #194's fix.
The native DXT upload path depends on #171 / PR #197.

## Run and photograph

1. Build the Release Switch tree as in [SWITCH_BUILD.md](SWITCH_BUILD.md).
   The output is `build-switch/switch/nfsu_switch_track.nro`.
2. Copy that NRO to `sdmc:/switch/nfsu_switch_track.nro`. Keep the already
   installed first-lap files under `sdmc:/nfsu`, with their subdirectories.
   [SWITCH_ASSETS.md](SWITCH_ASSETS.md) lists the required files. Use the
   full-memory homebrew launch mode for this diagnostic.
3. Launch it. Missing, partial or wrong-version assets stop loading with
   the failing filenames on screen. The check and parsing can be cancelled with +; parsing checkpoints also
   service HOME and applet exit requests.
4. The first track view starts paused. Photograph the track and its panel.
   Press A to move along the hand path. Left/right jump backward/forward
   ten path seconds and pause. The left stick looks around, B resets to
   the first view, and + exits. At the path end playback pauses.
5. Photograph at least the start, a later position after moving, and a
   paused collision result. Keep the build tag visible. Report whether
   geometry/textures remain visible, whether movement and culling work,
   and any asset, memory, shader or GPU error shown.

For nxlink, pass `sdmc:/nfsu` as the application argument if needed. The
same panel is printed to nxlink periodically, but the screen does not rely
on stdout. `-DNFSU_TRACK_BUILD_TAG=<ASCII-commit-id>` embeds an identifier
for matching the owner's photographs to the tested package. The screen shows
the first 12 tag characters to keep the header within one row.

## What is bounded

The recovered permanent set has 78 sections and covers every section the
1003 route-zone lists request. #157's retail residency trace keeps that set
for the whole hand-given lap, so the app reads those table-selected byte
ranges once, not the entire STREAM file, and retains their GPU uploads.
It does not invent zone-only eviction or simulate a race. The diagnostic
path is #157's hand polyline, 6824 samples at 30 world units/s and 60 samples/s;
it is not #61's recovered race route.

Each view uses the same camera basis for GPU matrices and the retail tree
culler. The default far plane is 250 world units. If the complete culled
list exceeds 2047 draws, it tries 125, 62.5 and 31.25; it never silently cuts
a list. The chosen distance, visible and unplaced instances, draws,
fallback draws and triangle count are on screen. A degraded culling policy
or a list that still does not fit is an error.

The resident set has 5036 upload resources, so the shared executor accepts
up to 8192 resources. It reserves 2047 uniform slots once (1048320 bytes)
for this app. Each update must keep the original resource count, pointers,
formats and sizes; only frame/draw uniforms and draw selection change.
Pointer identity is checked before validation can read changed index data.
Every preceding GPU frame has completed before constants are overwritten.
The backend checks actual image layouts and its aggregate 256 MiB block
budget before allocating. Staging and GPU image retirement keep the existing
upload/last-use fence rules.

The loader limits raw selected section input to 64 MiB and native texture
payload to 16 MiB. The retained streaming payload is 27770710 bytes,
including 14096320 texture bytes. The computed texture-preparation payload
subtotal is 41867030 bytes; it excludes loading scratch and allocator
overhead and does **not** establish compliance with the 48 MiB plan.
Per-section parser copies and non-texture byte ranges are released before
texture decoding. Texture pack bytes are released after decoding, before
scene preparation. The remaining raw buffers, temporary texture sets and
merged mesh copies are destroyed before collision loading and GPU creation.
Scenery records remain for culling. Collision covers the whole TRACKB1003
file and retains 1433448 bytes; a bounded vertical query runs at every
sample, with rise 2 and drop 20. `clear` and `outside` are displayed as such,
not as a fabricated ground height. A refused query stops with an error.

These payload figures exclude allocator overhead, strings, vector capacity,
scenery metadata and loading scratch. They are labelled **CPU payload EST**
on the panel. The renderer's **GPU blocks** is an allocation measurement.

The old PROCESS MEMORY pair was `InfoType_UsedMemorySize` /
`InfoType_TotalMemorySize`, not process-available bytes. libnx maps almost
all process memory as its heap at startup, so mapped memory is counted as
used before the loader allocates from it. The owner's old photo reports
3339833344 / 3343908864, a difference of 4075520 bytes; that remainder is
not loader headroom. The pair is removed from the panel.

The new panel reads newlib `mallinfo().uordblks` (allocated in-use bytes)
and `mallinfo().arena` (allocator extent, including free chunks), before
loading, at parsing checkpoints, after loading and every frame. It keeps
before-load and after-load in-use values on screen and distinguishes:

- **HEAP LOAD OBS HWM**: the highest observed in-use value during loading;
  its **delta** subtracts the before-load baseline and compares with
  50331648 bytes (48 MiB). **LOAD HEAP OVER** is the positive excess.
- **HEAP RUN OBS HWM**: the highest observed in-use value since the baseline,
  including later renderer/readback/loop allocations.
- **arena now / arena OBS HWM**: current and highest observed allocator
  extent. Neither extent nor in-use bytes is remaining process headroom.

OBS means sampled: these figures are actual allocator snapshots, not
container-size arithmetic, but transient allocations between checkpoints
can exceed the observed high-water. Parsing polls before/after section
reads and parses, between the geometry/scenery/texture parsers, during
section merging, before each retained texture allocation, and at the
remaining load-stage boundaries. Every checkpoint services HOME/exit and
+ cancellation and samples the allocator. A single synchronous file read
or parser call finishes before the next checkpoint; no hard latency bound
is claimed. The panel has 12 rows and a 232-pixel filled band so the heap
figures and controls remain visible.

### Measured loader overrun and plan

The loader is **not established to fit the 48 MiB streaming arena**. Review
4382 measured 66265088-66334720 desktop private bytes above baseline,
about 132% of 50331648. A separate worker measurement of this round's
loader (MinGW GCC 13.2, `GetProcessMemoryInfo().PrivateUsage`, a 1 ms
sampler plus the parsing checkpoints, three fresh processes) reproduces
the overrun:

| Run | Before | After load | Observed peak | Peak above baseline | Retained above baseline | Excess above 48 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 950272 | 41295872 | 67473408 | 66523136 | 40345600 | 16191488 |
| 2 | 913408 | 41914368 | 67448832 | 66535424 | 41000960 | 16203776 |
| 3 | 925696 | 41558016 | 67342336 | 66416640 | 40632320 | 16084992 |

These are Windows process-private bytes, **not Switch heap bytes**; the
new console panel supplies the latter. They show a real loading overrun,
not a passed budget. Every run used a full 476-file before/after manifest
(paths, sizes, mtimes, SHA-256), unchanged. Sampling is a lower bound on
peak occupancy on either platform. A zero observed excess cannot prove
that a missed transient stayed within budget.

The reduction plan is to merge and release each non-texture section's raw
bytes immediately instead of keeping all selected ranges to the end,
read only required texture ranges and retain native blocks once rather
than overlap raw packs and a duplicate preparation set, then reserve the
retained containers from the selected counts. Re-measure the allocator
through the same load phases before claiming a 48 MiB cap. This diagnostic
reports the overrun; it does not implement that peak reduction or enlarge
the budget to hide it. GPU block budgeting remains a separate 256 MiB
check, and process/heap/GPU measurements must not be added as if disjoint.

## Timing and evidence

The track is rendered offscreen by deko3d. Its RGBA result is copied to a
libnx framebuffer so the diagnostic panel can be photographed without
adding a new GPU font renderer. No CPU rasterizer draws the track.
The panel distinguishes GPU command recording/submission/fence wait,
GPU-to-CPU readback, last culling/command update, and the previous complete
frame including text, framebuffer copy and vsync. The readback overhead is
part of this diagnostic, not a claim about the future race renderer's FPS.
Asset-check and preparation time are shown separately as load time.

The desktop acceptance executable exercises the same loader, culler,
recorder and contact queries:

```text
nfsu_track_slice_tests X:/Games/NFSU
nfsu_track_slice_tests X:/Games/NFSU --full
```

The sampled run covers 57 nonempty frames, 543-1769 draws, 55 collision hits
and two clear columns (samples 1080 and 2640). Clear is a valid query answer
along this hand-given path. The default CTest invocation uses synthetic
camera/mapping/refusal checks, allocator high-water accounting, and a
missing/partial installation rejected before parsing; it requires no game
files. The stream tests also check polling throughout parsing/merging and
cancellation propagation. An exhaustive run also covers all 6824 samples:
1849 maximum draws, 6512 hits and 312 clear columns, with no outside/refused
query and no empty frame. Draw-update guards
also test a changed index pointer that must be refused before dereference.

The owner ran head `1429830` on the Switch on 2026-09-26; the coordinator's
[photo transcription (comment 4378)](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/198#issuecomment-4378)
records intact native DXT textures and geometry, assets PASS, 78 resident
sections, sample 5220/6823, 1109 draws (13 fallback), 58972 triangles,
5036 resources, and a collision hit at polygon 5139, height 49.06. Loading
was 5837 ms, GPU record/submit/wait 9.74 ms, readback 1.110 ms and the
previous full frame 26.66 ms; GPU blocks were 83058688 / 268435456 bytes.
That photo validates the native BC upload/render path. It exceeds the
16.7 ms full-frame target; performance belongs to #55 / #196.

That run predates the new heap panel. Its 27770710-byte CPU payload was an
estimate and its PROCESS MEMORY pair did not establish occupancy or peak
headroom. A photograph from this round's NRO is needed to record console
before/after/current heap and observed high-water values. The existing
hardware result is retained; the new heap numbers are not yet a console
measurement reported by this worker. Issue #196's race and #192's driving
work are separate.
