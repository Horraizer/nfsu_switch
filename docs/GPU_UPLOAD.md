# GPU upload bookkeeping

Issue #50 asks for deko3d uploads of prepared geometry, palettes, compressed
textures and generated body skins with bounded lifetime management: only
proven formats first, a clear failure for the rest, the alignment, cache and
synchronization requirements respected, and allocator and upload bookkeeping
tests that need no hardware where that is possible.

This note covers exactly that last clause. It builds the CPU half of the
upload path - what a scene uploads, which formats may be uploaded, and when
each placement may be used again - and it tests all of it without a Switch.
It calls no deko3d function, copies no byte to a GPU and has never run on
hardware. What a backend must still write, and what is unverified until it
does, is listed at the end.

Header: [`include/nfsu/gpu_upload.h`](../include/nfsu/gpu_upload.h).
Implementation: [`src/gpu_upload.cpp`](../src/gpu_upload.cpp), library
`nfsu_gpu_upload`, which also holds the budgets of
[`MEMORY_BUDGETS.md`](MEMORY_BUDGETS.md).
Tests: [`tests/gpu_upload_tests.cpp`](../tests/gpu_upload_tests.cpp).

## Where this layer sits

It reads a `PreparedCarScene` or a `PreparedTrackScene` and nothing below
them. It links `nfsu_track_draw_list`, the library a backend links, and never
reaches past it to the parsers, the texture decoder or the culler; the format
gate below is derived from the decoder, and a test holds the two together, but
the gate does not call it. It places memory in the arenas of
[`MEMORY_BUDGETS.md`](MEMORY_BUDGETS.md), which manage offsets and own nothing,
so the same code runs on a desktop with nothing behind the arenas at all.

Three things come out of it:

1. **An upload table** per scene: every resource a backend uploads, with its
   bytes, alignment, memory kind and lifetime.
2. **A format gate** that accepts exactly the texture format the decode path
   produces and refuses every other one by name.
3. **An upload ledger** that places each resource, and gives its memory back
   only once the GPU has completed the fences that read it.

## The resource model

| Kind | Source | Bytes | Element | Alignment | Memory |
| --- | --- | --- | --- | ---: | --- |
| vertex buffer, car | `PreparedMesh::vertices` | vertices x 36 | `MeshVertex` | 4 | buffer |
| vertex buffer, track | `PreparedTrackMesh::vertices` | vertices x 24 | `TrackVertex` | 4 | buffer |
| index buffer | `PreparedMesh::triangles`, `PreparedTrackMesh::triangles` | triangles x 6 | one `u16` index | 2 | buffer |
| texture | `PreparedCarScene::textures`, `PreparedTrackRegion::textures` | width x height x 4 | one RGBA8 texel | 32, staging | image |

The table lists, in this order, each prepared mesh's vertex buffer and then
its index buffer, in mesh order, and then the textures in scene order. Each
resource carries a pointer to its bytes inside the scene, so a table borrows
the scene and must not outlive it, and a backend's copy is one `memcpy` per
resource.

- **Vertices are uploaded as the structs they are in memory.**
  `MeshVertex` holds `px, py, pz, nx, ny, nz, color, u, v`; the car file
  stores `px, pz, py, nx, nz, ny, color, u, v`. The two are the same 36 bytes
  in a different order, so the vertex attribute layout a backend declares must
  come from `offsetof` on the struct, never from the file layout in
  [`FORMATS.md`](FORMATS.md). `static_assert`s in `src/gpu_upload.cpp` pin
  both vertex sizes, so a field added to either struct stops the build until
  the upload is reconsidered.
- **A triangle list is already the index stream.** `MeshTriangle` is three
  `u16` with the alignment of one, so `triangles.data()` is the stream
  `DkIdxFormat_Uint16` reads, and the index count is three per triangle.
- **A texture is its decoded top level**, RGBA8 as the format gate below
  accepts it: `rgba.data()`, `width * 4` bytes per row.
- **Names** are ASCII and unique within a table, and carry the prepared
  position so that two meshes with one stored name stay apart:
  `car mesh 0 BODY vertex buffer`, `track texture 1 hash 0xAAAA0002`,
  `car generated texture 0 hash 0x5C1A0001`. A leak report prints them. A
  stored mesh name keeps every byte from 0x20 to 0x7E, the space and the
  tilde included, and every other byte becomes `?`; an empty name is left out
  rather than leaving a double space. A texture's hash is
  `RenderTexture::hash`, which always counts as claimed, not its first claim,
  which a hand-built texture may leave empty.
- **An empty stream is counted, not listed.** A mesh prepared with vertices
  and no triangles draws nothing and has nothing to place, and an arena
  refuses a zero-byte request, so `UploadTotals::empty_buffers` counts it and
  the table leaves it out.

### Lifetimes

| Lifetime | What has it | Released |
| --- | --- | --- |
| `scene` | every buffer, and every texture decoded from a pack | with the scene it came from |
| `generated` | the generated body skin of [`RUNTIME_SKIN.md`](RUNTIME_SKIN.md) | on its own, whenever the customization changes; never after its scene |

The generated skin is the one resource no file holds. The garage composes it
again every time a vinyl changes, so it must be replaceable while the car's
buffers and pack textures stay where they are, and a table that gave it the
`scene` lifetime would make every vinyl change re-upload the whole car.

It is recognised by provenance, never by hash or position.
`bind_runtime_skin` stamps the generated texture with a caller-chosen source
index past the real texture sources and reports it in
`RuntimeSkinBinding::source`; `build_car_upload_table` takes that index and
gives `generated` to every texture carrying it. `kNoHandle` marks nothing
generated, even a texture whose own source index is `kNoHandle`. A test builds
the scene through `bind_runtime_skin` and `prepare_car_scene`, exactly as the
viewer does, and reads it with the binding's source, with none, and with a
source nothing carries. Naming a real pack source marks that source's
textures, which is the documented reason `bind_runtime_skin` asks for an index
past them.

### One upload, one accounting

A track region already counts its upload in `TrackDrawCost`:
`vertex_buffer_bytes`, `index_buffer_bytes` and `texture_bytes`. The track
table is that upload broken down, not a second accounting of it, and its
totals equal those three fields exactly because both multiply the same vectors
by the same element sizes. `validate_prepared_track_scene` recomputes the
cost, so the two cannot disagree for a scene it accepts; a test pins the
equality so that a change to either side has to change both.

A car scene has no cost structure. Its texture total equals the
`RenderTextureSet::decoded_bytes` of the set it was prepared from when every
texture of the set is used, which the test fixture arranges and checks.

### Not in the table

- **Palettes.** No prepared scene holds one. `decode_level` refuses a P8
  level, so no P8 texture reaches a prepared scene, and the format gate
  refuses P8 by name. The retail upload at 0x00407DB0 expands a P8 record
  through its own palette into 32-bit pixels before anything samples it
  ([`BODY_SKIN.md`](BODY_SKIN.md)); this project does not implement that
  expansion, so there is no palette to upload.
- **Compressed blocks.** The decode path turns DXT1, DXT3 and A8R8G8B8 into
  RGBA8, and no prepared scene keeps the stored payload. deko3d has formats
  that would sample DXT1 and DXT3 directly (`DkImageFormat_RGBA_BC1` and
  `DkImageFormat_RGBA_BC2`, deko3d.h lines 447 and 448), which would cut the
  texture memory of those formats to an eighth and a quarter. That is the
  obvious next step and the gate refuses it until someone proves it.
- **Instance transforms, draw lists and per-frame constants.** They are
  per-draw data. How a backend passes them - uniform buffers at
  `DK_UNIFORM_BUF_ALIGNMENT`, 256 bytes (deko3d.h line 150), or anything else -
  is a backend decision, and a frame's worth lives in the transient budget.
- **Shaders, command memory and descriptors.** The backend owns them, they do
  not depend on the scene, and deko3d gives them their own alignments.

## The format gate

`check_texture_upload_format` accepts one format and refuses the rest. The
list is not invented; it is `PixelFormat` of
[`include/nfsu/texture.h`](../include/nfsu/texture.h), one entry per stored
payload, plus the one format the decode path produces from any of them:

| Format | What produces it | `decode_level` | In a prepared scene | Gate |
| --- | --- | --- | --- | --- |
| RGBA8 | `decode_level`, and `bind_runtime_skin` for the generated skin | its output | every texture | **accepted** |
| DXT1 | a pack record the parser classifies as DXT1 | decodes to RGBA8 | as RGBA8 only | refused |
| DXT3 | a pack record the parser classifies as DXT3 | decodes to RGBA8 | as RGBA8 only | refused |
| A8R8G8B8 | a pack record the parser classifies as A8R8G8B8 | reorders to RGBA8 | as RGBA8 only | refused |
| P8 | a pack record the parser classifies as P8 | refuses: "P8 level requires a 256-entry palette" | never | refused |
| unsupported | nothing: the pack parser rejects the record | never tried | never | refused |

Where each column comes from: `classify_format` in `src/texture.cpp` for the
parser, `decode_level` in the same file for the decode, `decode_sources` in
`src/texture_decode.cpp` for what a `RenderTexture` keeps - a record only when
its top level decodes to exactly `width * height * 4` bytes - and
`bind_runtime_skin` in `src/runtime_skin.cpp`, which refuses a generated skin
of any other size.

RGBA8 is accepted when both dimensions are nonzero, the byte count is exactly
`width * height * 4` computed without overflow, and that is at most
`kMaxUploadResourceBytes`, the largest placement one deko3d memory block can
hold. The product is checked rather than trusted: `(2^32 - 1)^2 * 4` wraps
64 bits to `2^64 - 2^35 + 4`, and a gate that multiplied without checking
would accept exactly that byte count.

Every refusal names the format and says why it is not produced:

```text
texture format DXT1 is not uploadable: the decode path turns a DXT1 level into RGBA8 and no prepared scene holds the stored payload; upload the decoded RGBA8
texture format P8 is not uploadable: decode_level refuses a P8 level because it does not carry its 256-entry palette, and nothing in this project expands one through its palette, so no P8 texture reaches a prepared scene
texture format unsupported is not uploadable: the pack parser rejects a record of this format before any decode is tried
```

A8R8G8B8 is the case a careless gate gets wrong. It has four bytes per texel
exactly like RGBA8, so a gate that decided by byte count would accept it, and
a backend would upload B, G, R, A as R, G, B, A. The test offers every stored
format with a byte count that would be right for RGBA8, and requires each to
be refused by name.

Two tests hold the gate to its sources. One runs `decode_level` itself on a
well-formed level of every pack format, 5x3 so the DXT edge blocks are
cropped, and requires what it decodes to be exactly an RGBA8 image the gate
accepts and every stored payload, decodable or not, to be refused; if
`decode_level` ever learns P8, that test is where the gate has to follow. The
other requires the gate to spell every pack format exactly as
`pixel_format_name` does.

For a validated scene the gate cannot refuse: both scene validators already
require every texture to be `usable()`, which is the same size rule, and under
the default `SceneLimits` and `TrackSceneLimits` a texture past
`kMaxUploadResourceBytes` is refused at preparation, because
`max_texture_bytes` is 128 MiB. The tables still call the gate, because the
shape they record is the gate's, and the gate is tested directly; the refusal
branch inside the tables is defence in depth that no prepared scene reaches.

## The ledger

`UploadLedger` places each resource and tracks it until its memory is safe to
use again. A resource is **resident** from `upload()`, **releasing** from
`release()` until the GPU is done with it, and **reclaimed** after that, when
its placements go back to their arenas and its handle goes stale. A texture
also holds a **staging** placement of exactly its texels from `upload()` until
the copy that reads it has completed.

A fence is a number the backend assigns in submission order and the GPU
completes in that order, which is what a deko3d fence signalled on one queue
gives. `retire(n)` tells the ledger every fence up to `n` has completed. The
rules are the reason the ledger exists:

- **A staging copy is reclaimed only once its upload fence has completed.**
  Earlier, the next upload could overwrite bytes the copy has not read.
- **A released resource keeps its memory until both its last-use fence and
  its upload fence have completed.** The first guards a draw in flight; the
  second guards a copy in flight, for a texture released before its copy
  finished.
- **Nothing is reclaimed anywhere but `retire()`,** so a backend that never
  learns a fence completed can never reuse memory the GPU still reads.
  Fences only move forward: a smaller value changes nothing.

The rest of the bookkeeping:

- **Double release** is refused by name and changes nothing. Before the
  reclaim, the message keeps the first release's fence -
  `double release of car texture 1 hash 0x5C1A0002: already released at fence
  8 and not yet reclaimed` - because accepting the second would move the fence
  and could reclaim memory a draw still reads. After the reclaim the name is
  still reported, until the slot is reused; a release through a handle whose
  slot a newer upload holds is refused and leaves the newer upload alone.
- **The leak report,** `unreleased()`, names every resource uploaded and never
  released, in slot order. A released resource the GPU has not finished with
  is not a leak and is listed by `releasing()` instead.
- **A table uploads all or nothing.** When an arena refuses a resource, every
  placement the call made goes back at once - nothing was submitted - and the
  error names the resource and the arena's reason: `cannot upload car mesh 1
  WHEEL vertex buffer: the gpu arena for buffer memory refused 108 bytes at
  alignment 4: over budget`. The arenas' high-water marks keep the moment the
  partial placement existed, because it did. The slots go back too, newest
  first, which leaves the ledger's free slots exactly as the call found them:
  a retry is given the same slots in the same order, so its reports stay in
  table order.
- **A re-created ledger starts over.** `create()` resets every figure, the
  completed fence included, so a backend may number its fences from 1 again;
  a ledger that kept the old completed fence would reclaim new staging before
  the copies that read it had run. Handles issued before `create()` stay
  refused, because slot generations carry on across it.
- **An image is placed where its layout says.** Its size and alignment come
  from an `ImageLayoutQuery`, which on deko3d is `dkImageLayoutGetSize` and
  `dkImageLayoutGetAlignment` of the layout the backend builds. A layout
  smaller than the texels is refused. A desktop passes `linear_image_layout`,
  which answers the texels and the staging alignment for a texture and
  refuses anything else: a lower bound, never a measurement.
- **Lost placements are counted.** If an arena is reset, or a placement
  released, behind the ledger's back, the arena refuses the ledger's release
  and `stats().lost_placements` counts it. Nonzero means the arena and the
  ledger no longer describe the same memory, which nothing can repair from
  inside either.

Two tests run the lifetimes the way a game would.
`upload_release_upload_returns_to_a_known_state` uploads the car and the track,
releases both and retires, 500 times: after every cycle the arenas are exactly
empty and the ledger holds nothing, every placement of every cycle is where
the first cycle put it, and no handle names a slot past the 15 resources a
cycle holds. `generated_skin_is_replaced_on_its_own` recomposes the skin a
thousand times with two frames in flight: no other placement of the car moves,
at most three skins exist at once, and the image arena's peak is exactly the
two pack textures and three skins, 88 + 3 x 128 = 472 bytes, and never moves
from the tenth recomposition to the thousandth.

## What a deko3d backend must honour

Every requirement here comes from `include/deko3d.h` of devkitPro/deko3d at
commit `350f2b00a3e76ecd4f00191f8c5d6544ffbcb9db` (SHA-256 of the header
`623b3d7ec25bd82bc08492edaf86d74bd5b27361307af3469758f8ec5973ef6a`), or from
the deko3d sample framework in switchbrew/switch-examples at commit
`669786898205b7beb25ff1731e72982e6d0397d3`, under
`graphics/deko3d/deko_examples/source/`. The header is an API and a sample is
a practice; the third column says which each row rests on. Neither has been
run on hardware for this project.

| Requirement | Honoured here by | Rests on |
| --- | --- | --- |
| A memory block's size is a multiple of `DK_MEMBLOCK_ALIGNMENT`, 0x1000, and is a `uint32_t` | `kMaxUploadResourceBytes` = 0xFFFFF000, the largest such size; the gate and the ledger refuse anything bigger | API: deko3d.h lines 142 and 190 |
| A block's CPU and GPU caching, and whether it may hold images, are fixed when it is made | `UploadMemory`: `buffer` and `image` placements go to separate arenas | API: the `DkMemBlockFlags` at deko3d.h lines 176 to 183 |
| Buffer memory with the default flags is `CpuUncached \| GpuCached` | buffers are written in place by the CPU, with no cache maintenance | API: deko3d.h line 199; practice: the `pool_data` of `Example04_TexturedCube.cpp` line 162 |
| Images need a block with the `Image` flag | the image arena | API: deko3d.h line 183; practice: `pool_images` is `GpuCached \| Image`, `Example04_TexturedCube.cpp` line 160 |
| A vertex buffer is aligned to its vertex type | `kCarVertexAlignment`, `kTrackVertexAlignment` = `alignof` | practice: `alignof(Vertex)`, `Example04_TexturedCube.cpp` line 185 and `Example07_MeshLighting.cpp` line 157 |
| An index buffer of `u16` is aligned to `u16` and bound as `DkIdxFormat_Uint16` | `kIndexAlignment` = `alignof(std::uint16_t)` | API: deko3d.h line 1145; practice: `Example07_MeshLighting.cpp` lines 158 and 312 |
| An image takes the size and alignment its layout reports | `ImageLayoutQuery` | API: `dkImageLayoutGetSize` and `dkImageLayoutGetAlignment`, deko3d.h lines 1349 and 1350; practice: `SampleFramework/CExternalImage.cpp` line 25 |
| A texture is copied from staging memory aligned to `DK_IMAGE_LINEAR_STRIDE_ALIGNMENT`, 32 | `kTextureStagingAlignment` | API: deko3d.h line 162; practice: `CExternalImage.cpp` line 10 |
| The copy is `dkCmdBufCopyBufferToImage`, and its staging memory is released only once the queue has finished it | staging reclaimed by `retire()` at the copy's fence | API: deko3d.h line 1325; practice: `CExternalImage.cpp` lines 30 to 35, which wait for the queue to go idle |
| Memory the GPU may still read is not released | reclaim at the last-use fence | practice: `Example07_MeshLighting.cpp` line 256 waits for the queue before destroying anything |
| A fence is signalled on a queue and waited on or polled | the fence numbers `retire()` takes | API: `dkQueueSignalFence` and `dkFenceWait`, deko3d.h lines 1335 and 1246 |
| A `CpuCached` block needs `dkMemBlockFlushCpuCache` over each range the CPU wrote | not modelled: this layer places buffers in `CpuUncached` memory | API: deko3d.h line 1244 |
| An image copied on the same queue it is sampled on needs a barrier and an image-cache invalidation before it is sampled | not modelled; the backend records it | API: `dkCmdBufBarrier` and `DkInvalidateFlags_Image`, deko3d.h lines 1266 and 346. Which barrier mode suffices is **unverified**: the sample avoids the question by waiting for the whole transfer queue |
| RGBA8 is uploaded as `DkImageFormat_RGBA8_Unorm` | the one accepted format | API: deko3d.h line 418. Byte order on hardware, and whether any retail texture is sRGB, are **unverified** |

The constants `src/gpu_upload.cpp` needs are restated in
`include/nfsu/gpu_upload.h` rather than included, so that the desktop build
needs no devkitPro toolchain. The deko3d backend should `static_assert` each
against the real header.

## The thin hardware layer

What someone with a Switch still writes, and all of what they write:

1. Make the memory blocks: one `CpuUncached | GpuCached` block for buffers and
   one for staging, or one for both, and one `GpuCached | Image` block for
   images, each a multiple of 0x1000 and together within the GPU budget of
   [`MEMORY_BUDGETS.md`](MEMORY_BUDGETS.md). Create a `MemoryArena` over each
   with base 0.
2. Implement `ImageLayoutQuery` with a `DkImageLayoutMaker` for
   `DkImageFormat_RGBA8_Unorm`, the texture's width and height and one mip
   level, answering `dkImageLayoutGetSize` and `dkImageLayoutGetAlignment`.
3. For each buffer the ledger placed: copy `resource.bytes` from
   `resource.data` to the block's CPU address plus the placement offset.
4. For each texture: copy the texels to the staging placement, initialise the
   image at the image placement, record `dkCmdBufCopyBufferToImage`, and the
   barrier and invalidation before the first draw that samples it.
5. Number every submission in order, signal a fence for it, and call
   `retire(n)` whenever fence `n` is known to have completed.
6. Check that `stats().lost_placements` stays 0 and that `unreleased()` is
   empty when a scene is unloaded.

## Tests

`tests/gpu_upload_tests.cpp` states in a comment above each test the defect
it would catch. The car scene is prepared by `prepare_car_scene` with its skin
bound by `bind_runtime_skin`; the track scene is parsed from byte images by
the real parsers and placed by `build_track_scene`. A table that is right for
a structure no preparation step produces would prove nothing.

Every table is asserted resource by resource - kind, lifetime, memory, name,
source, bytes, alignment, element size and count, hash - and the sizes are
chosen so that no byte count can be right by accident: an 8x4 skin, a 4x4 and
a 2x3 texture on the car, a 4x2 and a 3x5 texture on the track.

## Mutations

Each mutant is one edit to the library, rebuilt and run against both suites. A
run is classified by what it printed, as [`CLAUDE.md`](../CLAUDE.md) requires:
a kill needs a non-zero exit, at least one `FAIL:` line and the
`<n> check(s) failed` summary; a survival needs exit 0 and the pass line;
anything else - a crash, a timeout, a missing summary - is unknown and is not
counted as a kill.

Three sweeps have been run over this change, and the two its author did not
write both found what the author's own had missed. An author's mutants measure
the set as much as the suite: [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)
records a suite its author's own 50 mutants scored at 49 caught, and an
independent pass that then found 56 survivors among 124. The last two sweeps
below ran at `932cc01`, whose `src/` and `tests/` are the ones committed here.

### The review

The review of head `7bb355df64c51f2b25da575197b72f90f9b0bfac` ran 83 mutants:
74 caught, 9 survived, 0 unknown. Those figures are the coordinator's report of
the review; this branch did not run that sweep and has not seen its mutant
list. The review found no wrong result: by its account each of the nine
survivors is a coverage gap, a line the shipped code gets right that no test
looked at. The fix-round brief names four of them, one to each of its items B1
to B4, and each item closes the survivor it names. For those four this branch
confirms the review's account: the shipped line is right, and only the test
was missing.

| Review item | Survivor | The gap | Closed by |
| --- | --- | --- | --- |
| B1 | `src/memory_arena.cpp:96`: `create()` reserves `max_allocations` free blocks, not `max_allocations + 1` | no test reached the one state that needs the last block, a two-sided split at the allocation limit | `arena_calls_never_allocate`: that split, in an arena of three |
| B2 | `src/memory_budget.cpp:146`: `staging_largest_bytes` is the last texture's size, not the largest | the one table it was asserted on had its largest texture last | `scene_requirement_reads_the_whole_table`: the car table, largest texture first |
| B3 | `src/memory_budget.cpp:167-171`: the guard on the placement pass removed | no test gave a layout query that answers differently when a table is placed than when it was sized | `scene_requirement_reads_the_whole_table`: a query answering 64 bytes, then 4096 |
| B4 | `src/gpu_upload.cpp:532`: the lifetime an upload records not taken from its resource | no test read a record's lifetime back through `find()` | `generated_skin_is_replaced_on_its_own`: the skin's record reads back as generated, a pack texture's as scene |

The other five survivors are named in the review's comment on the board, which
this session could not reach, and not in the brief. They are not named here
rather than guessed. Every mutant this branch's own two sweeps left alive in
the same three files is listed below, with the test that now kills it or the
reason it is still open; if any of the five is among them, the same holds for
it, but without the review's list that cannot be confirmed.

Each of B1 to B4 was shown both ways at `932cc01`: the survivor applied and
only the named test run, then the file restored byte for byte and the same
test run again.

| Review item | With the survivor | Restored |
| --- | --- | --- |
| B1 | exit 1, `FAIL: heap allocations inside a two-sided split at the limit: got 1, want 0`, `1 check(s) failed` | exit 0, `all memory arena tests passed` |
| B2 | exit 1, `FAIL: the largest texture is the first one: got 24, want 128`, `1 check(s) failed` | exit 0, `all gpu upload tests passed` |
| B3 | exit 1, `FAIL: a layout query that answers differently the second time is refused`, `2 check(s) failed` | exit 0, `all gpu upload tests passed` |
| B4 | exit 1, `FAIL: the ledger records the skin as a generated image texture`, `1 check(s) failed` | exit 0, `all gpu upload tests passed` |

At `7bb355d` the B1 survivor passes the same test, exit 0,
`all memory arena tests passed`: the test there never reached the split.

### The independent pass

A subagent the worker dispatched, which did not write the change, wrote 136
mutants of its own over the three library files. It ran inside the worker's
session, so it is independent of the author's mutant list, not of the author.
It first ran them at `d1d31a6`, a state of this branch from before its first
push that was squashed into `7bb355d`; that state's `src/` is byte for byte
`7bb355d`'s, and its headers differ in one comment. There it scored 101 killed,
35 survived, 0 unknown. Re-run unchanged at `932cc01`: 133 killed, 3 survived,
0 unknown.

61 of the 136 are in this document's file, `src/gpu_upload.cpp`: 44 killed and
17 survived at first, 60 killed and 1 survived now, 0 unknown either time. Each
"closed by" below was checked on its own at `932cc01`: the survivor applied and
only the named test run, which printed a `FAIL:` line and the failure summary,
after every named test had passed on the unmutated build.

| Mutant | Change | At `932cc01` | Closed by |
| --- | --- | --- | --- |
| I-G02 | printable replaces '~' (0x7E) | killed | `names_hashes_and_sources` |
| I-G03 | printable replaces ' ' (0x20) | killed | `names_hashes_and_sources` |
| I-G04 | checked_multiply overflow guard off by one | killed | `gate_refuses_malformed_rgba8`: the largest product that fits |
| I-G16 | kNoHandle no longer excluded from the generated match | killed | `names_hashes_and_sources` |
| I-G24 | texture_hash taken from claims[0] | killed | `names_hashes_and_sources` |
| I-G26 | linear_image_layout accepts a non-texture | killed | `image_layout_query_is_honoured` |
| I-G27 | ledger create accepts a null image arena | killed | `ledger_creation_is_checked` |
| I-G28 | ledger create refuses max_resources == kMaxArenaAllocations | survived | still open: the inclusive upper bound of `max_resources`, for the same reason as I-A07: a ledger at exactly `kMaxArenaAllocations` reserves 720 MiB of 176-byte slots and 4-byte free-slot entries on this build; the suite pins only that one more is refused |
| I-G31 | ledger create keeps the completed fence | killed | `ledger_creation_is_checked` |
| I-G33 | upload refuses bytes == kMaxUploadResourceBytes | killed | `ledger_creation_is_checked` |
| I-G44 | rollback oldest first instead of newest first | killed | `upload_table_is_all_or_nothing`: a retry on the same ledger |
| I-G45 | ledger release accepts a never-issued handle on a free slot | killed | `handles_the_ledger_never_issued` |
| I-G46 | double-release names a record that is no longer the handle's | killed | `handles_the_ledger_never_issued` |
| I-G57 | find() accepts a dead slot at the handle's generation | killed | `handles_the_ledger_never_issued` |
| I-G58 | staged_bytes counts the image placement size | killed | `image_layout_query_is_honoured` |
| I-G60 | mesh name not passed through printable() | killed | `names_hashes_and_sources` |
| I-G61 | unnamed mesh label gets a trailing space | killed | `names_hashes_and_sources` |

### The author's sweep

The author's own mutants, at `932cc01`. Each suite was also run one test at a
time, since both test programs take a test name, so every kill is attributed
to the tests that made it and every test is shown able to fail on its own.

39 mutants, 38 killed, 1 unknown.

| Mutant | Change | Result |
| --- | --- | --- |
| G01 | row bytes without the texel size | killed |
| G02 | block limit refuses the largest texture | killed |
| G03 | A8R8G8B8 accepted by byte count | killed |
| G04 | P8 accepted | killed |
| G05 | a refusal leaves the shape as it was | killed |
| G06 | texel product not checked for overflow | killed |
| G07 | a texture with extra bytes accepted | killed |
| G08 | format name drifts from the parser | killed |
| G09 | car vertices at the track stride | killed |
| G10 | car index count is the triangle count | killed |
| G11 | texture staging alignment below the deko3d stride alignment | killed |
| G12 | generated decided by position | killed |
| G13 | generated never set | killed |
| G14 | car table skips the validator | killed |
| G15 | track table skips the validator | killed |
| G16 | track vertices at the car stride | killed |
| G17 | an empty stream is listed | killed |
| G18 | staging reclaimed before its copy completes | killed |
| G19 | reclaim ignores the upload fence | killed |
| G20 | reclaim ignores the last-use fence | killed |
| G21 | fences may move backwards | killed |
| G22 | reclaim does not return the placement | killed |
| G23 | ledger never reuses a slot | killed |
| G24 | leak report includes released resources | killed |
| G25 | a second release before the reclaim is accepted | killed |
| G26 | a stale upload handle is accepted | killed |
| G27 | a refused table keeps its placements | killed |
| G28 | a refused table keeps its counts | killed |
| G29 | a staging refusal keeps the image placement | killed |
| G30 | lost placements are not counted | killed |
| G31 | a ledger holding resources is re-created | killed |
| G32 | the layout size is ignored | killed |
| G33 | a layout smaller than the texels accepted | killed |
| G34 | a texture accepted in buffer memory | killed |
| G35 | a texture uploaded with no staging arena | unknown |
| G36 | resident bytes counted as resources | killed |
| G37 | track index count is the triangle count | killed |
| G38 | ledger create does not raise the generation floor | killed |
| G39 | a new ledger slot starts at generation 0 | killed |

Each test run alone, against the same mutants:

| Test | Kills | Mutants |
| --- | ---: | --- |
| `gate_accepts_rgba8` | 3 | G01 G02 G05 |
| `gate_refuses_every_pack_format` | 3 | G03 G04 G05 |
| `gate_refuses_malformed_rgba8` | 2 | G06 G07 |
| `gate_names_are_the_parser_names` | 1 | G08 |
| `gate_follows_decode_level` | 1 | G03 |
| `car_table_is_exact` | 5 | G01 G09 G10 G11 G13 |
| `generated_lifetime_follows_the_binding` | 2 | G12 G13 |
| `tables_refuse_an_invalid_scene` | 2 | G14 G15 |
| `track_table_is_the_region_cost` | 3 | G11 G16 G37 |
| `empty_streams_are_counted_not_listed` | 1 | G17 |
| `names_hashes_and_sources` | 2 | G12 G17 |
| `ledger_fences` | 8 | G09 G10 G18 G19 G20 G21 G22 G36 |
| `upload_release_upload_returns_to_a_known_state` | 2 | G22 G23 |
| `leaks_are_reported_by_name` | 3 | G22 G24 G37 |
| `double_release_is_refused_by_name` | 4 | G20 G23 G25 G26 |
| `handles_the_ledger_never_issued` | 3 | G09 G23 G26 |
| `upload_table_is_all_or_nothing` | 7 | G09 G11 G13 G22 G27 G28 G29 |
| `generated_skin_is_replaced_on_its_own` | 2 | G13 G22 |
| `lost_placements_are_counted` | 2 | G22 G30 |
| `ledger_creation_is_checked` | 4 | G18 G22 G31 G34 |
| `create_again_keeps_old_handles_stale` | 3 | G26 G38 G39 |
| `image_layout_query_is_honoured` | 3 | G32 G33 G36 |

G35 removes the check for a missing staging arena, and the upload then
dereferences a null pointer: the program dies with a segmentation fault and
prints no summary. By the rule above that is unknown, not a kill, and it is
reported that way. It has come out unknown in all five runs of this sweep on
this branch, the same crash each time, so re-running it changes nothing.

`ledger_creation_is_checked` gained one check at `932cc01`, after the review: a
ledger asked for `kMaxArenaAllocations + 1` resources must be refused, and the
refusal must name the cap. Neither of this branch's sweeps has a mutant that
drops the upper half of that bound - I-G28 moves it, it does not remove it - so
it was shown both ways as B1 to B4 were. With
`max_resources > kMaxArenaAllocations` taken out of the check: exit 1,
`FAIL: one resource more than the cap`, `2 check(s) failed`. Restored: exit 0,
`all gpu upload tests passed`. At `1204e1c` the same edit passes the same test,
exit 0, `all gpu upload tests passed`.

## What is not verified without hardware

- Every row of the deko3d table above marked as practice or unverified, and in
  particular the barrier a copy needs before sampling, the byte order and
  colour space of `DkImageFormat_RGBA8_Unorm`, and whether a memory block's GPU
  address is aligned beyond 0x1000.
- The size of any image: `dkImageLayoutGetSize` decides it, and every image
  figure in this repository is its texels, a lower bound.
- That a buffer written into `CpuUncached` memory is visible to the GPU at the
  next submission with no further step.
- That fences retire in submission order on the queue the backend uses.
- Everything about time: how long an upload takes, and whether streaming a
  region stalls a frame.

## Issue #50's acceptance

| Bullet | State |
| --- | --- |
| Support only proven formats first, and fail clearly for unsupported ones | done: RGBA8 only, every other format refused by name with the reason, the list derived from the decoder and pinned to it by a test |
| Respect alignment, cache and synchronization requirements | done on the CPU side: every placement aligned as its resource requires, buffer and image memory kept apart, staging and releases reclaimed only at their fences. **Open on hardware:** the barrier, the cache behaviour and the image layout are the backend's and are unverified |
| Allocator and upload bookkeeping tests independent of hardware where possible | done |
| Real deko3d uploads | **open.** No Switch and no GPU were available; nothing here calls deko3d |
| Release build and full CTest pass | The two new suites pass in Debug and in Release. The full suite was not run on this branch's platform: on Linux `src/render.cpp` does not compile, a pre-existing error outside this change now tracked as issue #132, so no target that links `nfsu_render` builds there. The coordinator reports the full suite green at head 7bb355df64c51f2b25da575197b72f90f9b0bfac on MinGW-W64 UCRT GCC 13.2.0, 30 of 30 in Debug and in Release, twice independently; that is the coordinator's measurement, not this branch's |
| No retail or derived assets, ASCII only | no fixture comes from game data; scanned, see the handoff |
