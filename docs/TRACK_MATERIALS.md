# Track texture state and missing event-1 textures (#200)

This change enables alpha-tested and blended track materials on the CPU and
deko3d command paths for event 1, track 1003. The retail installation remains
an input only. The implementation adds no game asset to the repository.

## Evidence and scope

The PC `speed.exe` image identified in [`MATERIALS.md`](MATERIALS.md) has one
mesh-material queue path: 0x40C9D0..0x40CA54 appends groups, including track
meshes, to the same queue. At 0x40F225..0x40F258, texture-info bytes +81
(signed) and +84 choose a row; the effect index at [ebp] chooses its column.
The queue walker at
0x40ED50..0x40F0CF visits the 16 rows and their columns; within a bucket,
the texture group keeps insertion order. This is the basis for
`TrackDrawOrder::retail_bucket`.

The common material-state path checks texture-info +85 at 0x40F436 and calls
0x410650 to enable an alpha test with reference 0xAA. Global setup at
0x408B65 sets `D3DCMP_GREATER`. At 0x40F451..0x40F4CC, +86 selects no blend,
source-over or additive blend, as decoded in [`MATERIALS.md`](MATERIALS.md).
The world branch at 0x40F4D4..0x40F543 disables depth writes for blended
draws; car blended draws keep depth writes. The track fallback remains opaque.

The effect column and world-effect diffuse alpha coefficient have not been
decoded. The implementation groups by first texture use within each decoded
row, then preserves FIFO within that group, and uses a neutral alpha
coefficient of 1. These are explicit approximations; the alpha test, blend
factors and world depth-write toggle above are traced states. The default
two-sided culling and the CPU race renderer's separate car/track depth
composition remain other limits to exact retail frames.

## Texture provenance

The event-1 permanent request's texture baseline is in
[`TRACK_WHOLE.md`](TRACK_WHOLE.md). Supplemental texture-info records live in
nonresident numeric `Y` sections of `Tracks/STREAML1RA.BUN`. Two hashes
used by placed draws are `0x2FD2B696` in `Y8` (`ARC_BKBUILDINGG_01`) and
`0x60CD8020` in `Y4`/`Y5` (`SGN_LI_B03_NEO`, additive blend).
The retail event-1 zone requests do not bind those sections; using them is a
labelled visual approximation, without adding geometry residency or changing
the race state hash. The selector and its counts are pinned by
`nfsu_track_stream_tests` and the retail `nfsu_track_slice_tests` command.

Both race paths resolve texture sources in this order: resident STREAM
sections, supplemental `Y` sections, `Tracks/TEX1003TRACK.BIN`,
`Global/INGAMEB.BUN`, then `Global/GLOBALB.BUN`. Track slices retain native
DXT blocks; desktop rendering decodes the textures requested by resident
geometry. The two remaining hashes without a matching texture-info record
are `0x2D1A7A07` and `0x74CFB820`. The further circuit tracks of the
2026-10-10 all-circuits task keep the same rule: every absent placed hash
their permanent sets request is substituted with the GLOBALB DEFAULTALPHA
record exactly as Atlantic's power tower is (track 1005 `0x828DCF0B`,
1007 `0x2F93D9E1` and `0x828DCF0B`, 1008 `0x6D1AFA74` and `0x828DCF0B`),
except `0x74CFB820`, which tracks 1007 and 1008 place and leave unresolved
in exactly the way event 1 ships it on track 1003. The drift tracks
(owner tasks 2026-10-12 and 2026-10-14) apply the same rule in bulk: the
L1RB tracks 1301-1304 place the same nineteen absent hashes and the L1RC
tracks 1305-1308 the same fifty-six, none of which any source of their sets
provides (the L1RC stream holds two of the L1RB hashes in a texture-only
section those tracks never load, which changes nothing), and all are
substituted with the DEFAULTALPHA record (`absent_placed_hashes` in
`src/track_slice.cpp` is the list), leaving nothing unresolved. The
aliases live in `RenderTexture::default_alpha_aliases`, which the fixed
natural-claims array was never sized for. The drag tracks (owner task
2026-10-15) substitute their own small per-image sets - four hashes on
1201/1210, two on 1202, five on 1206/1214 and four on 1207 - while 1207
also places `0x74CFB820`, which stays unresolved exactly as tracks 1007
and 1008 ship it. The sprint tracks (owner task
2026-10-17) place the same L1RA family hashes their neighbours do - 1104
and 1105 `0x828DCF0B`, 1106-1109 also `0x2F93D9E1` - all
substituted, while 1106, 1107 and 1108 place `0x74CFB820` and ship it
unresolved as ever.

`Global/GLOBALB.BUN` also contains raw P8 textures. Its `info +52` points to
a separate palette in the pack's data range. The retail loader points the
texture record at those bytes (0x00545A59..0x00545A61), copies each entry's
dword into a pixel (0x00407E70..0x00407E82), and selects A8R8G8B8 at
0x00407EA8. Each stored entry is therefore B, G, R, A; the decoder supplies
its index chain and converts that palette to RGBA. The retail slice test
requires `HEADLIGHTGLOW` (`0x3394FE62`)
and `WINDOW_TRAFFIC` (`0x008A6835`) to decode and bind as textures instead of
fallback draws. The pack layout and payload validation are documented in
[`FORMATS.md`](FORMATS.md); `nfsu_texture_tests` checks palette indexing.

## Verification

The shader manifest declares all six alpha-test/blend pairs for resolved
track textures. Synthetic tests compare the CPU draw trace to recorded GPU
commands for each pair, including the disabled depth write on blends and
the neutral alpha coefficient. A separate stream-table test keeps the
supplemental selector confined to nonresident numeric `Y` records in the
stream file. The full desktop and Switch gates and the replay comparison
are recorded in the issue's pull request.

The bounded loader drops resident and supplemental raw section bytes before
re-reading one texture source at a time. The retail
`nfsu_track_loader_peak_tests` command measures its allocation peak. The
retail `nfsu_race_run_tests --full-draws` command compares cached commands
with direct recording, including upload resources, state, order and placement.
The command-cache estimate is pinned in `nfsu_race_run_tests`; its increase
over the pre-spillover baseline comes from additional texture upload
resources, including the decoded P8 textures, not from material state.
Before/after retail manifests are recorded in the PR handoff.
