# Car detail investigation (issue #232)

All source files were read from the stock retail installation through
`tools/frame_budget_evidence.py`. Its before and after manifests covered 476
files and were equal for every run. Geometry came from
`Cars/240SX/GEOMETRY.BIN`, textures from `Cars/240SX/TEXTURES.BIN`,
`Cars/TEXTURES.BIN`, and `Global/GLOBALB.BUN`; no modified global pack was
used. The stock part assembly used the same brake and plate geometry as
`nfsu_draw_commands_retail`.

## Pixel and source trace

Coordinates below are in the 1280x720 painted stock frame. The draw and
triangle IDs are from the reference renderer's raster ID pass. Mesh indices
are the source 240SX geometry indices, not indices in the assembled scene.

| Detail and camera | Pixel, draw, triangle | Mesh and material | Texture/state | Finding |
| --- | --- | --- | --- | --- |
| Nissan badge, yaw 35 | (884,443), draw 6 | `240SX_KIT00_FRONT_BUMPER_A`, mesh 40, group 0 | `240SX_BADGING` `0xB3DC27AB`, car texture record 76, DXT3 128x64 one mip, alpha test off, source-over blend | The decoded atlas contains a readable NISSAN wordmark. At this camera the badge is about 14 pixels wide. A nose closeup at yaw 0, pitch 0, distance 2, target (1.7,0.6,0) reads clearly after linear sampling. |
| Black strip ahead of rear wheel, yaw 145 | (583,507), draw 60, triangle 2801 | `240SX_KIT00_REAR_BUMPER_A`, mesh 295, group 1, part slot 2 copy 0 | generated `240SX_SKIN1` `0x77A20245`, A8R8G8B8 512x512 one mip, alpha test and blend off | Triangle 458 has constant UV (0.597656,0.998047); it selects the opaque black bottom edge of the stock skin target. This strip is source art, not a missing texture. |
| Black strip ahead of front wheel, yaw -45 | (635,480), draw 49, triangle 1239 | `240SX_KIT00_SKIRT_A`, mesh 228, group 1, part slot 10 copy 0 | same generated `240SX_SKIN1`, alpha test and blend off | Triangle 142 has the same constant UV. The target texel (306,511) is (0,0,0,255). This strip is source art. |
| Dark front arch, yaw -45 | (600,420), draw 55, triangle 2118 | `240SX_KIT00_BODY_A`, mesh 259, group 2, part slot 5 copy 0 | same generated `240SX_SKIN1`, alpha test and blend off | Triangle 727 has constant UV (0.597656,1). Nearest sampling toggled between the blue top row and black bottom row at the repeat seam, making black speckles. Retail linear/repeat sampling smooths this seam. The dark arch remains part of the source body art. |

The generated skin target is `DUMMY_SKIN1`, `Cars/TEXTURES.BIN` pack 0 record
120, A8R8G8B8 512x512 with one mip. The painted composition binds it as
`240SX_SKIN1`. The black texel was checked in the decoded target, and both
strips remained black with shading disabled. The material flags are decoded
from texture-info bytes +85 and +86; the retail state and effect-5 cull
branches are documented in `docs/MATERIALS.md`. Opaque car materials select
`D3DCULL_NONE`; the blended badge selects `D3DCULL_CW`.

The installed retail `fx/car.efx` decodes from XOR 0xAA and declares the
base sampler with `AddressU/V = WRAP` and `MINFILTER`, `MAGFILTER`, and
`MIPFILTER = LINEAR`. This is the primary source for the sampling change.
The CPU reference now uses texel-centered bilinear repeat for car draws.
The deko3d path selects a linear/repeat sampler for `car_textured`; track
draws retain their nearest/repeat sampler. The extra descriptor is 32 bytes.
No extra image or staging resource is allocated. Desktop race car draws now
use retail material state, matching the existing Switch race path.

## Tire backing and console limit

The inner wheel backing is in `240SX_KIT00_WHEEL_A`, mesh 304, material
group 1, `240SX_TIRE` `0x03A09493` (car texture record 4, DXT1 128x128,
one mip). The inboard two-sided quad is triangles 180 and 181. Its corner
texels have alpha 0 and its dark center has alpha 255. The retail texture
mode 1 enables `D3DCMP_GREATER` at reference 0xAA, with blending disabled.
An isolated inward view shows a cutout disc in retail mode and a dark
square in off mode. The stock viewer now defaults to retail mode and keeps
an explicit `--material-state off` diagnostic. CPU and recorded GPU draws
agree on this material state and texture binding.

The owner also reported a square in an earlier Switch `--replay` capture.
That observation cannot be disproved by a desktop frame or by successful
cross compilation. This branch needs the requested console capture to
verify the new NRO's actual alpha cutout; if the square remains, the GPU
alpha path or selected rim texture needs a follow-up using that capture.
The stock rim pointer selected from `CarRenderInfo + 0x4D8` is still
unproven, as recorded in `docs/CAR_TEXTURES.md`.

## Fixed frame pins

`nfsu_draw_commands_retail` verifies the CPU frame, draw-command parity,
zero painted fallback pixels, and the three detail views. Its invocation
must be wrapped by `tools/frame_budget_evidence.py ROOT OUT run -- ...`.

| Painted retail camera | TGA SHA-256 |
| --- | --- |
| yaw 35, default | `FE7506F173A871A208F94451DFC71A3F3AEBF1EBE677C28AF2B73AD9CEA3AEE8` |
| yaw -45 | `370A134DC58F2F3FD6FCC50F191AD800F32DA125CB472C5CAA894021D46B9403` |
| yaw 145 | `F136A9150C99EE367DA801228D642F7FF4E3DC801C79EC6B2B3DA6A002A2907F` |
| nose closeup | `A98E6D51AE8B5E22B642ABE958B505CDC50B956759F21CE56E33B1B94DFB19EF` |

The deterministic race replay still finishes event 1 in 48336 steps,
13:26.90, with state SHA-256
`79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9`.
The frame output hashes changed as expected with the corrected sampler and
desktop race material state. The retail manifest remained equal after it.
