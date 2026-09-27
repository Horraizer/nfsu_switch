# Track texture state and missing event-1 textures (#200)

This change enables alpha-tested and blended track materials on the CPU and
deko3d command paths for event 1, track 1003. The retail installation remains
an input only. The implementation adds no game asset to the repository.

## Evidence and scope

The PC `speed.exe` image identified in [`MATERIALS.md`](MATERIALS.md) has one
mesh-material queue path: 0x40C9D0..0x40CA54 appends groups, including track
meshes, to the same queue. At 0x40F225..0x40F258, texture-info bytes +81
(signed) and +84 choose a row and effect column. The queue walker at
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

The event-1 permanent request loads 78 sections and asks for 1008 distinct
track texture hashes. With its original sources, 960 decode and 48 are
unresolved. Of the 48, 46 have texture-info records in 19 nonresident `Y`
sections of `Tracks/STREAML1RA.BUN`; those sections total 3512968 input
bytes. The other two have no matching texture-info record in the surveyed
retail installation. Only two of the original 48 are in placed draw-list
entries: `0x2FD2B696` in `Y8`, and `0x60CD8020` in `Y4`/`Y5`. The former
belongs to `ARC_BKBUILDINGG_01`; the latter belongs to `SGN_LI_B03_NEO` and
uses additive blending. [`TRACK_WHOLE.md`](TRACK_WHOLE.md) records the
permanent-set baseline and why its diagnostic still reports 48 when no
spillover source is requested.

The event-1 preparation now reads the missing `Y` sections from the same
bounded stream table and verifies they contain only texture packs. It does
not add them to geometry residency or to the race state hash. Track slices
retain native DXT blocks, and the desktop renderer decodes only textures
requested by resident geometry. A sampled track-slice run read the 19 extra
sections, prepared 57 frames, reached 1769 draws at most, and left all 476
retail files unchanged by name, size, modification time and SHA-256.

## Verification

The shader manifest declares all six alpha-test/blend pairs for resolved
track textures. Synthetic tests compare the CPU draw trace to recorded GPU
commands for each pair, including the disabled depth write on blends and
the neutral alpha coefficient. A separate stream-table test keeps the
supplemental selector confined to nonresident numeric `Y` records in the
stream file. The full desktop and Switch gates and the replay comparison
are recorded in the issue's pull request.
