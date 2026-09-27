# Recorded commands on deko3d (issue #146)

`nfsu_switch_deko3d` prepares the same stock 240SX as the CPU application,
records it with `record_car_commands`, uploads that table, and executes
the recorded draws on a deko3d graphics queue. It never invokes the CPU
rasterizer. The painted frame is the default; `--unpainted` selects issue
#141's frame. Both read the same six inputs under `sdmc:/nfsu`, listed in
[SWITCH_BUILD.md](SWITCH_BUILD.md), and write no files.

Build with the commands in that document. The coordinator deploys the NRO:

```text
nxlink -a <ip> build-switch/switch/nfsu_switch_deko3d.nro
```

Use the console IP for `<ip>` and read its screen; the `-s` return channel
has not connected on this machine. The owner reports the screen. This
change has not run on a console or emulator. The AArch64 build and NRO/RomFS checks are file evidence, not
evidence of a displayed GPU frame.

The viewport is 1280x720. + exits; `appletMainLoop` handles a system close
request. There is no orbit in this first GPU application. Optional arguments
are `--unpainted`, `--material-state=retail`, and one replacement input root.
Unknown arguments are errors. Retail state is opt-in, as in the desktop
stock command, and selects the six recorded car variants.

Missing inputs are named by path. Input issues, preparation, recording,
arena refusals, shader loading and deko3d diagnostics stop rendering and
show the reason on screen and stdout, with + still available. The diagnostic
framebuffer closes before the GPU swapchain takes the window; after a
failure the renderer dies before the framebuffer reopens. Screen text
uses glyphs without heap allocation, so out-of-memory exceptions can still
be reported. If that framebuffer itself cannot be created, the failure can
only be printed to stdout. nxlink redirects stdout but not this screen text.

## Ownership and synchronization

`switch/deko_renderer.cpp` and its header are Switch application sources,
not neutral library sources. `nfsu_draw_commands`, `nfsu_gpu_upload` and
`nfsu_memory` retain their neutral classification and SDK-free desktop
build. No new library is introduced. The one-shot owner borrows the commands
and scene until close. Resource positions stay identical to the original
upload table and the handles returned by `UploadLedger::upload_table`.

Before any GPU call, `plan_draw_execution` checks the executor limits and
calls `validate_recorded_commands`, which checks manifest identity,
supported interfaces, payload shapes, u16 index ranges and values, texture
bindings, and pipeline enum bounds. Refusals name the upload or draw. The
payloads remain borrowed; validation cannot make a freed scene safe.

The image query uses `dkImageLayoutGetSize` and `dkImageLayoutGetAlignment`;
`upload_table_requirement` sizes the arenas. Buffers and staging use
`CpuUncached | GpuCached`; images use `GpuCached | Image`. Blocks are rounded
to 0x1000 and checked against `kMaxUploadResourceBytes`. The total of the
application's blocks is capped by the 256 MiB `kGpuBudgetPlaceholder`,
which remains a placeholder and excludes the driver's queue/swapchain
allocations. Uniforms also obey the 1 MiB transient placeholder. More than
4096 resources or draws is refused. Arena alignment/allocation caps remain
enforced by the existing allocator.

Mesh bytes go directly into their ledger placements. RGBA8 texture rows
go through staging to block-linear images. Rows whose byte length is not
a multiple of 32 use separately tracked padded staging; the copy's row
pitch is in bytes and includes padding; the layer stride is zero for
this depth-1 copy. `plan_texture_copy` and `plan_draw_execution` in the
neutral library calculate the copy fields and allocation sizes. Desktop
tests pin 512x512, 128x64 and narrow padded rows, opaque clear, viewport,
resource/draw limits and transient-budget boundaries. Ledger staging and
padded copies stay live through the upload fence. A full barrier invalidates the image cache, and the
upload fence is waited before drawing. No CPU-cached block is used.

Uniforms use the recorder's 256-byte slots and 112/64/80-byte Frame/Instance/
Material payloads. Descriptors bind RGBA8 images and a nearest/repeat sampler.
`placed.vert.dksh`, `textured.frag.dksh` and `fallback.frag.dksh` load from
mounted NRO RomFS. The build copies them only after manifest/blob checks.
Runtime checks the DKSH header, size and stage, copies code into Code
memory and initializes the shader. There is no runtime compiler.

Each draw binds its shaders, uniforms, manifest vertex layout, whole vertex
buffer, u16 index buffer and recorded triangle range. It also binds culling,
alpha comparison/reference, blend factors/operation, depth comparison/write
and color mask. The device uses zero-to-one depth and upper-left origin;
clear depth is 1. Depth is ZF32; two RGBA8 presentation images are 1280x720.

One queue owns every submission. Each frame signals and waits for its fence
before command memory reuse, then presents the acquired image. This first
implementation is serial. At exit `waitIdle` completes every last use;
the ledger releases/retire reclaims resources, uniforms and padded staging
are released, and resident/releasing/staged/lost-placement counters are
checked before the swapchain, blocks, queue and device are destroyed.

## Evidence and limits

The existing synthetic car/track comparisons now run the validator too.
`nfsu_draw_execution_tests` checks named refusals for malformed payloads,
programs, references, vertex/index ranges, texture bindings and pipeline
values, including the first and last index in the actual borrowed bytes.
Malformed buffer sizes are checked with bounded products, without division.
It also exercises the executor plans before any SDK call; these checks do
not execute a GPU copy or prove GPU pixels.
The optional retail check now covers painted and unpainted stock frames,
both material modes, the SDK-free execution plan, the full CPU draw trace
and placements, and both current off-state pins. Run it with full before/after retail manifests:

```text
build/nfsu_draw_commands_retail.exe <retail-root>
```

| Frame | TGA bytes | SHA-256 |
| --- | ---: | --- |
| unpainted | 3686418 | `3DEB2147CDA1B1D66DC47FC599B8A1FD88FCB273F0B38BFBE656666808199D25` |
| painted | 3686418 | `8A5932A302A22AA3C4C01776009B5348629D8D37B3B61E6E0AA303235B1CF48F` |

The #145 notes carried into #146 are addressed explicitly:

- N1: the observer's depth/write fields remain literals describing CPU
  state. The independently observed sequence does not make those literals
  independent evidence of raster state or GPU equivalence.
- N2: CPU clear preserves background alpha but CPU draws write 255.
  RGB-only GPU writes preserve clear alpha, so this executor refuses clear
  alpha below 255 by name. Stock frames use 255. A synthetic test checks the
  recorder against the actual CPU clear with background alpha 19.
- N3: a parsed invalid-slot material with a nonempty range now pins the
  non-drawable skip against the CPU loop. Uniform layout is pinned, but
  this is not an independent exact-value pin of every numerical Frame lane.

The Switch CTest suite checks the new AArch64 ELF, NRO header/icon/title,
bounded RomFS section and exact bytes of all three checked shader blobs
inside it. It runs on the build host. Issue #147 adds offscreen readback
and a separate on-screen comparison application, documented in
[FRAME_COMPARISON.md](FRAME_COMPARISON.md); it has not run on hardware.
Track residency and reclaiming CPU decoded pixels belong to #171 and are
not implemented here. Cache behavior, image byte order, presentation,
error screens and + exit await the coordinator/owner's hardware run.
