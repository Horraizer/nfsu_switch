# Original loading presentation

The normal Switch frontend startup, career race preparation (including
intro0, drift and drag), free driving, restarts and tournament round changes
use RaceLoadingArt. This is the existing generic PC_Loading.fng branch,
read from authenticated PC1.4 USA GLOBALB/GLOBALA, rather than a fabricated
completion percentage. PC_LOAD and the two authored loading-bar sprites
retain their original transforms and Move animation timing.

Previously only intro0 enabled the art; other races displayed technical
heap/checkpoint rows. Screen now owns the transient artwork and its cached
software pixels. LoadingScreen begins presentation before career parts/car
preparation, reuses it inside run(), and releases it before race GPU upload.
The same release occurs on cancellation and exceptions. Initial frontend
loading releases its cache before open_frontend. No menu or race GPU
renderer is live while the software loading framebuffer is presenting.

Normal loading text passed by Progress is covered by the original artwork.
Phase, heap and error evidence remains in the network/SD logs; explicit
profile-frame, panel and replay diagnostics retain their prior text path.
No new game asset, percentage, tip, song restart or GPU budget is introduced.
The artwork may hold its current frame during a synchronous asset operation;
an independent asynchronous loader/continuous animation worker is not added.

## Optional higher-resolution artwork

Copy the generated `ArtworkHD` folder into the game data root near
`Movies` and `Movies720`. The Switch uses enhanced copies of the generic
loading background, the loading tip illustrations, and the Press A
startup background. The authored positions, alpha blend modes, text,
loading indicator and prompt timing are shared with the retail artwork.

The tool rescales the decoded textures with Lanczos and applies light
sharpening to opaque artwork. The loading background and splash fit a
1536-pixel maximum edge. Loading illustrations scale to twice the
original dimensions. Each texture is compressed with LZ4 and decoded only
while that screen is active, outside the shared frontend atlas. Missing,
incompatible or unreadable files fall back to the retail graphics.

Build `nfsu_frontend_art_sources` on PC. Use Python 3.9 or newer with
Pillow and liblz4 installed.

```text
python tools/prepare_artwork_hd.py <retail-root> out/ArtworkHD --exporter <nfsu_frontend_art_sources>
```

On Windows pass `--lz4 <path>` if liblz4 is outside the loader search
path. Retail files are read-only. The generated artwork stays local.

## Evidence

nfsu_intro_grid_viewer supports the loading mode. It writes four activity
frames and compares cached pixels with the original full raster at several
strides. Required host/SDK validation commands and the source/NRO identity
are recorded in build/loading-original-r38/artifacts/BUILD_RECEIPT.json.
Console startup, cancellation and an event6 inter-round transition still
need owner hardware acceptance. The unsent r37 camera/results fixes are
included. No independent review or Git publication is claimed.
