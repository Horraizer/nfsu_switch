# CPU/deko3d frame comparison on the console (issue #147)

`nfsu_switch_compare.nro` renders the same prepared stock 240SX twice in
one process: `render_stock_frame` supplies the CPU reference, and #146's
recorded commands supply the deko3d frame. It reads the same six files under
`sdmc:/nfsu` as [SWITCH_BUILD.md](SWITCH_BUILD.md). It writes no files and
packages no game asset. The painted stock frame is the default; `--unpainted`
selects the unpainted #141 frame. One positional argument can replace the
input root. Unknown options, including retail material mode, are refused:
the comparison uses the pinned default camera and material-state=off.

Build using [SWITCH_BUILD.md](SWITCH_BUILD.md), then deploy:

```text
nxlink -a <ip> build-switch/switch/nfsu_switch_compare.nro
nxlink -a <ip> build-switch/switch/nfsu_switch_compare.nro --unpainted
```

Replace `<ip>` with the console IP. Do not use `-s`: its return channel does
not work on this machine. No socket or stdout redirection is needed for this
application. The report, input errors and deko3d failures appear on screen.
The owner photographs the report; the coordinator records its numbers on
issue #147. Keep the NRO SHA-256 from the handoff with the photo.

## Reading and controlling the report

The first completed view shows the captured GPU frame with an opaque text
report over it. It reports:

- the exact RGBA pixel count and percentage;
- the pixel count and percentage whose four channels are each within +/-2;
- the maximum absolute difference in any R, G, B or A channel;
- RGB PSNR over every pixel, in dB;
- CPU render time, GPU render/submission/fence-wait time, and a separately
  measured GPU readback/submission/fence-wait time;
- CPU TGA SHA-256 and whether it matches the desktop pin, and raw GPU RGBA
  SHA-256. These two hashes encode different byte sequences; they are not
  an equality test against each other.

A shows the captured GPU frame, B the CPU reference, and X the amplified
difference. Y hides/shows the report, so its text cannot hide a region of
the difference image. + exits; the system close request is honored. The
images are static captures, not newly rendered controller frames.

PASS requires at least 99.0 percent of pixels within +/-2 AND PSNR at least
35 dB, exactly the coordinator's fixed issue thresholds. The application's
PASS also requires the CPU TGA to match its selected desktop pin:

| CPU frame | TGA bytes | SHA-256 |
| --- | ---: | --- |
| unpainted | 3686418 | `3DEB2147CDA1B1D66DC47FC599B8A1FD88FCB273F0B38BFBE656666808199D25` |
| painted | 3686418 | `8A5932A302A22AA3C4C01776009B5348629D8D37B3B61E6E0AA303235B1CF48F` |

A CPU pin mismatch displays overall FAIL even if the numerical GPU/CPU
comparison passes. No threshold is adjusted to fit a result. Percentages
are displayed to six decimal places, but the within-two decision uses exact
integer counts, including the accepting 99-percent boundary.

## Metric and difference convention

The coordinator recorded the owner's RGB PSNR decision on 2026-09-26 in
[issue #147, comment 4164](https://git.accad.pro/murzintsevav/nfsu-switch/issues/147#issuecomment-4164).
This answers PR #191 review note N1; the earlier claim that confirmation
preceded implementation was unsupported. Its formula is
`10*log10(255^2 * 3*N / RGB_squared_error)`, with N every image pixel,
including background. Perfect RGB has infinite PSNR. Alpha is excluded from
PSNR so a constant alpha channel cannot inflate the score; alpha remains
included in both pixel criteria and the maximum. No alignment search,
cropping, gamma conversion, channel swizzle, flip or background mask is
applied. RGBA8 is top row first in both inputs; stride padding is ignored.

The difference image is per-channel `min(255, abs(cpu-gpu)*16)`, with opaque
alpha. Red and blue also include the alpha difference, making alpha-only
errors magenta. Display text is drawn only into the libnx framebuffer after
metrics and hashes have been computed. The CPU, GPU and difference buffers
remain unchanged when changing views.

## Readback and ownership

The existing deko3d renderer has an offscreen mode with no native window or
swapchain. Its render target uses RGBA8_Unorm and the same `record_frame`
path as #146's presentation mode. Both paths bind the same clear, viewport,
uniforms, shaders, resources, pipeline states and indexed draw ranges.

Capture completes rendering with a flush-enabled fence. Then
`copyImageToBuffer` copies the whole image to a separate CPU-uncached,
GPU-uncached block. `DkCopyBuf.rowLength` is width*4 BYTES (5120 for 1280),
imageHeight is zero for depth 1. Full barriers with L2 flush/invalidation
surround the copy; another flush-enabled fence completes it before the CPU
reads any pixels. The readback starts at aligned offset 32, with 32-byte
poisoned guard regions on each side checked after the fence. No copy flag
flips the image. The renderer closes and destroys every GPU resource before
the captured bytes are shown through libnx. The offscreen renderer never
claims the diagnostic framebuffer's native window.

The 256 MiB GPU placeholder also counts the readback allocation. Reported
GPU render time includes CPU command encoding and the fence wait, excludes
uploads/allocation and excludes the separately timed readback. CPU time
includes only render_stock_frame, excluding preparation and TGA encoding.
These are wall-clock measurements, not hardware timestamp-query timings.

`nfsu_frame_compare` is a classified neutral library: standard-library
metrics, bounded RGBA8 views and difference generation, with no SDK, clock,
file access or renderer dependency. Desktop tests cover metric formulas,
99/98-percent and above/below-35-dB cases, alpha-only errors, stride padding,
truncation/overflow/empty-input refusals and amplification/clamping. The
Switch host CTest checks the comparison ELF/NRO/NACP and the exact checked
offline shader blobs in its RomFS. Neither test runs GPU code on the host.

## Hardware evidence status

No console or emulator result has been measured by this worker. The NRO is
ready for the owner to run; its four numbers, frame times, pin status,
difference view and + exit still need that run and photo. A passing host
suite or a self-comparison test is not evidence that the real GPU meets
99 percent / 35 dB. Track residency (#171) is outside this change.

## Lighting follow-up

[SHADER_DERIVATIVES.md](SHADER_DERIVATIVES.md) records the uam derivative
constant defect behind the first console FAIL and the offline build fix.
