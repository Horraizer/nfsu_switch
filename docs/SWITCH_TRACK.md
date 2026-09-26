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
   The output is `build-ascii-switch/switch/nfsu_switch_track.nro`.
2. Copy that NRO to `sdmc:/switch/nfsu_switch_track.nro`. Keep the already
   installed first-lap files under `sdmc:/nfsu`, with their subdirectories.
   [SWITCH_ASSETS.md](SWITCH_ASSETS.md) lists the required files. Use the
   full-memory homebrew launch mode for this diagnostic.
3. Launch it. Missing, partial or wrong-version assets stop loading with
   the failing filenames on screen. The check can be cancelled with +.
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
for matching the owner's photographs to the tested package.

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
including 14096320 texture bytes; texture-preparation overlap is 41867030,
below the 48 MiB streaming plan. Per-section parser copies and non-texture byte ranges are released before
texture decoding. Texture pack bytes are released after decoding, before
scene preparation. The remaining raw buffers, temporary texture sets and
merged mesh copies are destroyed before collision loading and GPU creation.
Scenery records remain for culling. Collision covers the whole TRACKB1003
file and retains 1433448 bytes; a bounded vertical query runs at every
sample, with rise 2 and drop 20. `clear` and `outside` are displayed as such,
not as a fabricated ground height. A refused query stops with an error.

These payload figures exclude allocator overhead, strings, vector capacity,
scenery metadata and loading scratch. The panel separately reads the
console's process-used and process-available memory from `svcGetInfo`, and
shows the renderer's allocated GPU block bytes. Do not add process memory
and GPU block bytes: they may describe overlapping process allocations.
This is a current process-memory reading, not a measured loader peak.

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
camera/mapping/refusal checks and requires no game files. An exhaustive run also covers all 6824 samples:
1849 maximum draws, 6512 hits and 312 clear columns, with no outside/refused
query and no empty frame. Draw-update guards
also test a changed index pointer that must be refused before dereference.

**Hardware validation is pending.** A successful build, package check and
desktop trace do not prove console frame time, real memory occupancy, native
DXT appearance or camera movement. The owner's run and photographs complete
that acceptance step. Issue #196's race and #192's driving work are separate.
