# Track and car texture filtering (#262)

`--texture-filter legacy|linear` selects a render policy on `nfsu_race`,
`nfsu_viewer` and `nfsu_switch_race`. Legacy is the default pending console
visual and performance acceptance by the coordinator. It preserves each
material's previous sampler; it does not force all materials to nearest.
The policy is fixed before `RaceGpuScene::load`, so cached templates and
rebuilt commands use the same choice throughout a replay.

| Material family | Legacy CPU | Legacy deko3d | Linear CPU/deko3d |
| --- | --- | --- | --- |
| Resolved world diffuse | nearest/repeat | nearest/repeat | linear/repeat |
| Ordinary car diffuse | linear/repeat | linear/repeat | linear/repeat |
| Shaded car diffuse | linear/repeat | nearest/repeat | linear/repeat |

The existing legacy shaded-car discrepancy is preserved deliberately; only
the candidate fixes that sampler discrepancy. This does not assert that
other CPU/GPU shading differences are solved. Sky, fallback materials, HUD,
reflection producers and targets, shadow, particles, glows and wet-road
lamps retain their previous selection. Addressing, strict alpha-test
threshold, blending, draw order and depth policy are unchanged. Reflection
production explicitly retains legacy sampling even when the main scene is
linear. New manifests reuse the existing shaders and sampler descriptors;
there are no additional images, mip levels or runtime shader uploads.

## Recovered source and approximation

The read-only retail root used was
`E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`.
The provenance identities are:

| File | Bytes | SHA-256 |
| --- | --- | --- |
| speed.exe | 3178496 | C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7 |
| Global/GLOBALB.BUN | 2361176 | 39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9 |
| fx/car.efx | 7200 | 4055E8F0FFA65752AD4D9F9CF36AFBDF67A07DD9F0C15B8BA31A57A1F726FEA7 |
| fx/world.efx | 2404 | D1CDBD788D564C4CD408A060D0EDA4E99245F0EB54B18A4FCEE848D2966E6C98 |

RECOVERED: XOR each effect byte with `0xAA` to read its declaration.
In car.efx, lines 32-39 declare the diffuse sampler, WRAP U/V and LINEAR
MIPFILTER/MINFILTER/MAGFILTER. The fixed environment declarations at
lines 204-207 and 253-255 also specify linear filters. In world.efx,
lines 18-25 declare the diffuse sampler with the same WRAP and LINEAR
settings; fixed technique lines 99-100 and 105-107 repeat them. These are
source declarations for the selected diffuse families, not a capture of
every executed runtime state or proof of settings for unrelated effects.

APPROXIMATION: this implementation filters only the existing top level.
Retail declares linear mip filtering too, but the uploader has no mip
chain, and #262 does not add one. Bilinear improves near and medium-range
sampling; it cannot remove distant oblique-road shimmer or recover detail
lost through minification. It can soften signs and thin alpha silhouettes.

Original game captures in the owner's sibling `reference` directory were
read without modification. They support an appearance comparison, not an
internal sampler inference. Their different lighting, detail and cameras
are not claimed to be reproduced by this change.

## Reproduction and evidence

From the actual workspace, with the real GCC runtime on PATH:

```text
python tools/texture_filter_evidence.py "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" out/filter-pair --race build-release/nfsu_race.exe --frames 180,600,6000 --motion --expected tests/texture_filter_expected.txt
```

Use a fresh output directory. The tool refuses a retail output location,
requires fresh frames and clean command completion, and brackets all four
runs with SHA-256 file manifests. All four state/trajectory artifacts and
race-result lines must agree with the existing replay pins. At fixed ticks
180, 600 and 6000, road/buildings/signs/foliage and the car are rendered with
shading off/on. The additional 6000..6120 sequence samples motion every four
60 Hz simulation ticks. It is a two-second 15 fps inspection sequence, not
a measurement of production rendering speed. Candidate replay hashes live in
`tests/texture_filter_expected.txt`; existing legacy hashes stay in
`tools/race_replay_check.py`. `nfsu_texture_filter_retail` checks paired
images, alpha preservation and simulation identity using fresh outputs. It
also re-renders twelve native diagnostic images whose pins live in
`include/nfsu/texture_filter_reference.h`. The versioned diagnostic input, pose checks and native image references
are separate from the complete gameplay replay; see the integration policy
below. Production legacy replay pins stay in `tools/race_replay_check.py`.

Local evidence is under `out/262-gpt6-pair-v2` in the worker worktree, with
`images.json`, command logs, state/trajectory files and equal before/after
retail manifests. The preview and motion GIF are inspection aids; the TGA
files and the reproducible tool are the evidence. No retail-derived image
is committed. Road grain decreases with linear filtering, building edges
soften, and foliage cutouts remain; distant motion still shimmers. The
ordinary CPU car is identical because it already used linear sampling.
Console appearance has not been accepted.

Focused tests cover weighted interpolation, negative and integral repeat,
one-texel and narrow textures, interpolated alpha on both sides of the
strict cutout threshold, source-over alpha, unchanged coverage/depth, and
policy selection in cached/rebuilt commands. Full-route linear runs with
shading off/on retain the existing simulation and memory assertions.

## Console comparison and remaining acceptance

The owner observed a hardware parity failure at `821457c` in handheld
step 180, linear, shading off; the board record is
[coordinator comment 6589](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/264#issuecomment-6589).
CPU pin agreement did not establish GPU parity. A local draw trace identified
the left opaque banner as DXT1 `0x6E7C9B86`, 128x32 (eight block rows).
The upload layout was inherited unchanged from the base, rather than
introduced by the linear policy.

In the installed deko3d 0.5.0, automatic layout chooses two GOBs for six to
eight physical rows. Its level allocation reduces that height to one GOB,
but the base-level transfer retains two: successive 64-byte columns can
skip memory and write beyond the image allocation. The primary source is
[dk_image.cpp at v0.5.0](https://github.com/devkitPro/deko3d/blob/5cc144dcf5606fbcd78430f6c62923546e5f9cf4/source/dk_image.cpp),
`calcLevelOffset`, `ImageInfo::fromImageView` and `dkImageLayoutInitialize`.
[Upstream issue 10](https://github.com/devkitPro/deko3d/issues/10) reproduces
the corresponding eight-row RGBA8 sampling problem. The app now requests a
custom one-GOB layout only for those physical heights, including compressed
edge blocks. Other heights retain automatic layout. Payloads, byte pitch,
native compression, sampler/depth/blend state, CPU pins and comparison
tolerances stay unchanged. `nfsu_draw_execution_tests` models the transfer
and level addresses, reproduces the old skipped-column/out-of-image failure,
and checks corrected round trips for RGBA8, BC1 and BC2. A new console run
is still required to establish the repair's actual GPU result.

The separate CPU car/track composition also differs from a common target
for glass blending. At `821457c`, a local step180 linear/off shared-target
experiment changed only car-region pixels and passed both comparison
tolerances against the existing reference. This is a measured reference
limitation, not proof that all hardware glass differences are explained.
The original f61380672d48 diagnostic and its evidence retain their identity.
Any residual hardware failure needs its own evidence; this integration does
not change comparison tolerances or establish hardware parity.

`nfsu_switch_texture_compare.nro` captures the existing cached GPU scene and
rasterizes its fixed replay pose on the CPU at 1280x720. It checks selected
track identities/order against a rebuilt selector before drawing. Its CPU
reference decodes the existing native top levels within the existing CPU
texture budget; that diagnostic storage is separate from the production
race's native-block loader and is not a production FPS measurement. Sky is
on; particles, glows, wet-road reflections and HUD are omitted from this
isolated comparison. Shadow and the stock car remain. Desktop
`nfsu_texture_filter_compare` writes the corresponding CPU TGA to a fresh
`--out` path. A selects GPU, B CPU, X the amplified difference and Y hides
or shows the report; + exits. PASS also requires its CPU TGA to match the selected desktop pin. Pixel
comparison uses the existing #147 thresholds:
99 percent of all RGBA pixels within +/-2 AND RGB PSNR at least 35 dB.
Do not increase these tolerances to accept a discrepancy.

```text
nxlink -a <ip> <artifact-dir>/nfsu_switch_texture_compare.nro -- --step 180 --texture-filter legacy --car-shading off
nxlink -a <ip> <artifact-dir>/nfsu_switch_texture_compare.nro -- --step 180 --texture-filter linear --car-shading off
```

The `--` ends nxlink options; the following flags belong to the NRO.

Repeat both at steps 600 and 6000 and with shading on. The legacy shaded
sampler difference is a known diagnostic baseline, not an expected parity
pass. Photograph the report and each visible difference region; preserve
the source SHA and NRO SHA-256 from the handoff with the photographs.

For actual driving and performance, run this exact pair with unchanged
settings in both handheld and docked mode:

```text
nxlink -a <ip> <artifact-dir>/nfsu_switch_race.nro -- --replay --profile-frame --sky on --particles=on --car-shading on --texture-filter legacy
nxlink -a <ip> <artifact-dir>/nfsu_switch_race.nro -- --replay --profile-frame --sky on --particles=on --car-shading on --texture-filter linear
```

Do not add a height override, boost override, tuned car or panel mode.
Repeat shading off for visual isolation. Capture the same replay road,
building/sign, foliage and car sections, then every final profile page.
Record CPU/GPU/frame-time distribution, over-budget frames, memory counters,
replay result and actual clocks/scene resolution. Present time includes
vsync; GPU render timing and full-frame timing answer different questions.
Compare matching modes and settings, with the 60 fps target unchanged.
The coordinator records hardware results on #262/#55 and decides rollout.
No console parity, speed gain, final visual acceptance or default change is
claimed by desktop images or the Switch host packaging checks.

## Integration fixture policy (#262, #253, #275, #222)

`nfsu_texture_filter_compare` now uses `texture_filter_v2.txt`, a bounded
prefix of the reviewed #275 gameplay input as integrated at 8090cb0. It is
an authored controller recording, not a retail asset. The file ends after
the last diagnostic pose. Its LF bytes are protected by `.gitattributes`.
The application checks its SHA-256 before replaying it, then checks both
`RaceRun::sequence_hash()` and `RaceRun::steps_hash()` before selecting or
rendering each pose. References live only in
`include/nfsu/texture_filter_reference.h`. The Switch comparison packages
its own eight-shader RomFS and this fixture; the race NRO retains its
original gameplay recordings and packaging checks. An explicit `--input`
must have the same v2 bytes. The shared gameplay recording is refused even
at an early pose that happens to produce the same image.

This fixture isolates input maintenance, not simulation code. It uses the
same production physics on CPU and Switch. A future physics change that
moves a diagnostic pose fails the state assertion and requires attribution
before any image-reference update. It must not retain obsolete physics,
ignore a state mismatch, repin from a failed GPU capture, relax comparison
tolerances, or inherit a previous NRO's hardware result. A future deliberate
fixture change gets a new version and separately measured input, state and
image references. Both filter modes must still match their CPU references
within the existing pixel thresholds.

At 8090cb0, measured with standalone GCC 13.2.0 Release and fresh outputs in
`out/262-s1-attribution-v4`, controlled source/archive substitutions showed:

- Removing only the #275 low-support torque, using the old recording and
  old HUD, recovered all earlier gameplay linear pins and the native
  linear/shaded diagnostic references. Present #222 contact observers were
  retained throughout that measurement and did not change those results.
- With the same old recording, production physics first changed the state
  stream at step 1052. The old complete recording no longer finished the
  race. Its native step-6000 image differed, so retaining an isolated old
  recording alone cannot preserve the previous diagnostic reference.
- The replacement input first differed at zero-based tick 1063; its effect
  was measured separately with physics and HUD fixed. Both state and the
  step-6000 image changed again. The early diagnostic poses remained equal.
- Switching only to #253 HUD preserved byte-identical state and trajectory
  streams. At steps 180, 600 and 6000 in both linear shading modes, every
  changed gameplay pixel remained inside x483..620/y242..347 at 640x360.
  The native comparison omits HUD and is unaffected by that source change.
- The bounded v2 input reproduced all eight native references at steps 180
  and 600. Only the four step-6000 native references were replaced after
  these independent physics/input measurements. The four gameplay linear
  references were reconciled with the measured HUD and physics/input
  changes; existing legacy gameplay references remain unchanged by this fix.

The measurement binaries accepted arbitrary authored input, reported their
hashes, and stopped explicitly at tick 6000. They are attribution tools,
not required gate results. The production executable has no such bypass.
Fresh full native retail and Switch gates verify the production references,
input refusal, output refusal, packaging and unchanged retail manifests.
The [ASCII reproduction scripts and measurements](https://git.accad.pro/attachments/e7a33696-f199-48d7-bc49-1fc88b90172e) contain the source substitutions, first-divergence evidence and fresh image identities; no retail-derived image is attached. The durable finding and policy checkpoint is
[PR281 comment7207](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/281#issuecomment-7207).

The earlier console artifact remains separately identified as source
`f61380672d480fee2df99a3e68f7006708f00689`, diagnostic NRO SHA-256
`6E8A082901D014A606A8F8B94D83324B927155049DE60F9719D31195AC9A1196`.
Its recorded hardware passes apply only to that original artifact, as
preserved in
[PR281 checkpoint7193](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/281#issuecomment-7193)
and its evidence attachment. The combined v2 diagnostic has no inherited
hardware acceptance, including at unchanged early poses. This fix does not
change gameplay physics, HUD, audio or the production filtering default.
Bright-frame, handling, glass and hardware/performance acceptance remain
open; independent review of the whole integration is still required.
