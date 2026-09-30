# Car detail investigation (issue #232)

The retail install was read through `tools/frame_budget_evidence.py` with
equal before/after SHA-256 manifests of all 476 files. The input packs were
`Cars/240SX/GEOMETRY.BIN`, `Cars/240SX/TEXTURES.BIN`,
`Cars/TEXTURES.BIN`, and `Global/GLOBALB.BUN`; brake and plate geometry
matched `nfsu_draw_commands_retail`. No modified global pack was used.

## Owner's three details

Coordinates are in the 1280x720 painted stock frame. Draw and triangle IDs
come from the reference renderer's raster ID pass; mesh IDs refer to source
240SX geometry. The review's tire-texture recolor isolated the two default
camera rectangles to the tire mesh, rather than to the skin strips visible
at other yaws.

| Detail | Source and state | Result and limit |
| --- | --- | --- |
| Two rectangles inside the wheels at the default camera | `240SX_KIT00_WHEEL_A`, mesh 304 group 1, `240SX_TIRE` hash `0x03A09493`, record 4, DXT1 128x128. The inboard backing quad is triangles 180/181. Texture corners have alpha 0 and the center alpha 255. | Retail mode 1 tests alpha with `GREATER 0xAA`, removing the opaque-looking rectangular corners. Off mode draws the backing quad as a dark square. The earlier Switch race path recorded the car with off state even though its track was retail; the cached and reference `RaceGpuScene` paths now both use retail. A console capture is still needed to verify deko3d output. |
| Nissan badge at yaw 35 | Pixel (884,443), draw 6: `240SX_KIT00_FRONT_BUMPER_A`, mesh 40 group 0, `240SX_BADGING` hash `0xB3DC27AB`, record 76, DXT3 128x64. | The decoded atlas contains a readable NISSAN wordmark. Its main missing state was source-over blending. Retail blend restores the transparency; linear sampling helps the approximately 14-pixel badge. A nose closeup at yaw 0, pitch 0, distance 2, target (1.7,0.6,0) reads clearly in the CPU reference. |
| Dark front arch at yaw -45 | Pixel (600,420), draw 55, triangle 2118: `240SX_KIT00_BODY_A`, mesh 259 group 2, generated `240SX_SKIN1`. Triangle 727 uses constant UV (0.597656,1). | The dark area is present in the source body art. Nearest repeat sampled opposite sides of the skin seam, producing black speckles; linear repeat smooths that seam. There is no independent retail trace proving the arch's precise appearance. |

The stock viewer now starts at retail material state. The desktop viewer's
stock default was already retail; explicit `--material-state off` remains a
diagnostic. `nfsu_race_run_retail` checks the actual cached and reference
race car commands: 74 car draws, 12 tire draws with alpha test and two badge
draws with source-over blend. The complete frame contains 16 alpha-tested
and 19 blended car draws. CPU command parity proves material selection, while
the Switch capture must confirm the hardware result.

## Separate skin-strip traces

Two genuine dark strips appear at other cameras. They do not explain the
owner's default-camera rectangles. At yaw 145, pixel (583,507) is draw 60,
triangle 2801, `240SX_KIT00_REAR_BUMPER_A` mesh 295 group 1; triangle 458
has constant UV (0.597656,0.998047). At yaw -45, pixel (635,480) is draw
49, triangle 1239, `240SX_KIT00_SKIRT_A` mesh 228 group 1; triangle 142
uses the same UV. Both sample the opaque black bottom edge of generated
`240SX_SKIN1` (target texel 306,511 is RGBA 0,0,0,255) and persist with
shading disabled. The target is `DUMMY_SKIN1`, `Cars/TEXTURES.BIN` pack 0
record 120, A8R8G8B8 512x512, one mip. These strips are source art.

## Sampling, pins, and resource cost

The installed `fx/car.efx`, decoded with XOR 0xAA, declares `AddressU/V =
WRAP` and `MINFILTER`, `MAGFILTER`, and `MIPFILTER = LINEAR`. The CPU
reference uses texel-centered bilinear repeat for car draws; the deko3d
car program selects a linear/repeat sampler. Track draws retain their
nearest/repeat sampler. The added descriptor is 32 bytes; no mip levels,
images or staging allocations were added. The deko3d sampler's actual
console behavior has not been verified off-console.

`nfsu_draw_commands_retail` and `include/nfsu/stock_frame.h` hold the
default stock and painted pins; `tests/tuned_frame_retail.cmake` holds the
three tuned camera pins. `nfsu_draw_commands_retail` also checks the yaw
-45, yaw 145 and nose detail views. `nfsu_stock_frame_retail` and
`nfsu_stock_frame_painted_retail` hold the Switch CPU default frames.
`nfsu_stock_frame_viewer_match` uses a synthetic alpha witness: the default
and explicit retail frames must have the same SHA-256, and both must differ
from explicit off. The fixture draws enough alpha-tested pixels to make
that comparison sensitive. All retail runs require the before/after
manifest wrapper.

The deterministic event 1 replay still ends at step 48336 and 13:26.90,
with the state and `states.txt` pins in the race tests. The desktop race
frame pins are checked by `nfsu_race_replay_check`. The new material state
changes frame pixels while preserving simulation and replay inputs.

The prior painted stock reference reported four alpha-tested and 16 blended
groups and 25952 blended fragments. Its previous 20.65 ms handheld mean
exceeded the 16.67 ms frame budget. The additional sampler descriptor and
retail blending cannot establish a 60 fps result from desktop timing. The
console follow-up needs paired old/new handheld and docked frame and GPU
times, draw count, blended fragment or overdraw estimate, memory, and
matching camera captures. No 60 fps claim is made here.

Window and brake texture substitutions, dummy decal suppression and the
unproven stock rim pointer are described in `docs/CAR_TEXTURES.md`. The
left brake's per-instance override derives CPU alpha/blend state after
texture substitution; `nfsu_stock_scene_tests` checks that behavior.
