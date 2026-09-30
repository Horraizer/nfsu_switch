# Retail car shading trace for issue #182

This is a static trace of the PC retail renderer, not a measurement of a
particular GPU run. Virtual addresses use the `speed.exe` image base
`0x00400000`; a `.text` file offset is the virtual address minus that base.
The files examined are `speed.exe` (3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`),
`fx/car.efx` (7200 bytes, SHA-256
`4055E8F0FFA65752AD4D9F9CF36AFBDF67A07DD9F0C15B8BA31A57A1F726FEA7`),
and `GLOBAL/GLOBALB.BUN` (2361176 bytes, SHA-256
`39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`),
and `Cars/240SX/GEOMETRY.BIN` (SHA-256
`002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`).
No game file was executed or modified for this trace.

## Technique and light equation

After XOR 0xAA decoding, `fx/car.efx` defines techniques in this order:
`car` (VS 1.1 and PS 1.1), `car_fixed_envmap`, and `car_fixed`. The latter
two enable fixed lighting and specular, while only the first evaluates the
ten `HarmonicCoeff` vectors in its vertex shader. The environment fallback
uses `CAMERASPACEREFLECTIONVECTOR` in texture stage 1; the final fallback
does not bind `EnvMapTex`. These are effect-source observations, not proof
of which technique a given PC device uses. The executable obtains technique
handles in a loop at `0x40E3A0..0x40E3AA`, applies capability guards at
`0x40E3B6..0x40E458`, validates a candidate at `0x40E4A0..0x40E4B3`,
and selects an accepted handle at `0x40E4CD..0x40E4E4`. Thus the selected
technique is device dependent. The `0x40FD2A..0x40FD38` branch later
chooses effect uploads or fixed-function texture factor work according to
the active renderer object and its pass state.

The programmable effect computes a harmonic normal term, a harmonic
reflection term, and a sampled cube environment term. The shader equations
and their alpha dependency are derived in [MATERIALS.md](MATERIALS.md),
under "The environment term, written out exactly". In particular, the
cube term is multiplied by diffuse texture alpha and by the view-dependent
`DiffuseMin.w + vdotn * DiffuseRange.w`. These are direct `car.efx` source
expressions; no constant environment color is present in that source.

## Where the cube comes from

At `0x40B340..0x40B36D`, the executable calls the Direct3D 9 device's
`CreateCubeTexture` method with edge length from `0x702A84` (128), one
level, render-target usage, format from `0x702A88` (21,
`D3DFMT_A8R8G8B8`) and default pool. It stores the result at `0x73646C`.
The loop at `0x40B372..0x40B3C1` gets each of six cube face surfaces into
`0x71AB6C` and makes a matching depth surface in `0x71AB84`. The
recreation path checks the feature flag `0x700E74` at `0x40B340..0x40B347`;
the disabled branch clears those pointers at `0x40B3C5..0x40B3E9`.

The face surfaces enter render-target descriptors at `0x40AB0A..0x40AB92`.
At `0x40AF2B..0x40AF7D`, the renderer selects a six-face group and enters
a six-iteration loop. The per-face path constructs a view matrix and calls
`0x47D070` at `0x40B2BB`. Correction to the earlier trace: this call sets up
a view; it is not sufficient evidence of a scene draw. At `0x47D09D` it
copies prior view state, then sets matrices and derived vectors. The effect
binds the resulting cube pointer from `0x73646C` to `EnvMapTex` at
`0x40EB30..0x40EB51`. The reflection source is a live render target, not a
cube stored as a texture in `GLOBALB.BUN`.

Observed scheduling: `0x40AF31` reads counter `0x7364DC`, which increments
at `0x40A77A..0x40A78D`. The divide at `0x40AF38` uses the quality-indexed
period table at `0x702AAC`; enabled quality values 1, 2 and 3 have periods
6, 4 and 1. The mask row at `0x702AC0` is selected by
`6 * (quality - 1) + counter % period`. `0x40AF8C..0x40AF92` writes the
selected mask bit into each face descriptor's byte `+8`:

| Quality value | Face indices enabled per counter increment |
| --- | --- |
| 1 | One face, cycling through 0, 1, 2, 3, 4, 5 |
| 2 | Faces 0 and 4, plus one cycling through 1, 2, 3, 5 |
| 3 | All six faces |

This is the observed descriptor schedule, not measured capture timing.
The caller at `0x5707B0` selects position/orientation sources through
runtime objects. Its normal path adds the vector at `0x704AD0` to the
selected object's vector before calling `0x40AD60` at `0x5708E2`; another
path uses globals `0x7365A0..0x7365A8` and calls at `0x57086E`. Which object
is selected in every gameplay/camera mode and which scene categories the
later capture includes remain unknown. Static disassembly does not prove
that every scheduled face is actually rendered in a given frame. A static
or simplified environment on Switch is therefore an approximation.

The Direct3D method interpretation above follows the interface calls and
the [Direct3D 9 cube render-target procedure](https://learn.microsoft.com/en-us/windows/win32/direct3d9/creating-cubic-environment-map-surfaces).

## Material records and paint parameters

`GLOBALB.BUN` contains 139 `0x00135200` records with a 160-byte payload.
For example, the `ALUMINUM` record begins at file offset `0x0000`,
`CHROME` at `0x0690`, `DEFAULT` at `0x1650`, `METPAINTBLUE` at `0x2610`,
`REGPAINTBLUE` at `0x3528`, `REGPAINTRED` at `0x3870`, `SKIN` at `0x42F0`,
and `WINDSHIELD` at `0x4EC0`. Each payload
starts after the eight-byte chunk header, has a name hash at payload
`+0x0C`, and an ASCII name at payload `+0x14`. The executable resolves a
geometry `0x00134013` key by hash against a registry object's `+0x0C`
at `0x540151..0x540191`; material byte `+32` selects the key slot at
`0x40CA0E..0x40CA22`. The selected object reaches the coefficient upload
at `0x40EE7B..0x40EE88` and `0x40FAE0`. Hence a car's geometry chooses a
named material key, while the coefficients are stored in the global
material record. Runtime replacement or mutation of these objects beyond
the traced paint binding has not been established.

For a concrete car-file join, the stock base mesh `240SX_BASE_A`
(`docs/CAR_SCENE.md`, base level 0) has these `0x00134013` key selections
in its `Cars/240SX/GEOMETRY.BIN` material records. The material record
offsets and key hashes come from `nfsu_probe texture-bindings`; the name
hashes match the `GLOBALB.BUN` payloads at the listed offsets:

| Base material record | Texture hash | Key hash | Global key name and record | Flags |
| --- | --- | --- | --- | --- |
| `0xF398C` | `0x77A20245` | `0x3ED70C43` | `WINDOWMASK`, `0x4E18` | `0x0001C180` |
| `0xF39C8` | `0x77A20245` | `0x54949AFD` | `CHROME`, `0x0690` | `0x00014180` |
| `0xF3A04` | `0x77A20245` | `0x010CB64A` | `PLAINNOTHING`, `0x3288` | `0x00014180` |
| `0xF3A40` | `0x77A20245` | `0x0FEDEE40` | `DULLPLASTIC`, `0x1998` | `0x00014180` |
| `0xF3A7C` | `0x77A20245` | `0x0FEDEE40` | `DULLPLASTIC`, `0x1998` | `0x00014080` |
| `0xF3AF4` | `0xD195BE56` | `0x471A1DCA` | `WINDSHIELD`, `0x4EC0` | `0x0001C180` |

`0x77A20245` is the `240SX_SKIN1` hash, supplied by runtime skin
composition rather than this local texture pack (`docs/BODY_SKIN.md`,
`docs/CAR_SCENE.md`). The nine painted stock groups instead use the geometry
material key `CARSKIN` (`0xD6D6080A`). There is no `CARSKIN` record among the
139 `GLOBALB.BUN` material records. The `0x00135200` loader at
`0x540F54..0x540F5D` stores the on-disk `DEFAULT` record (hash
`0x2C420D64`, offset `0x1650`) in `[0x6F3DF0]`. For paint, the game reads
the installed `BASE_PAINT` part in RideInfo slot 30 (`+0x4C4`), searches its
attributes for key `0x6BA02C05`, and resolves the value in the material
registry (`0x56B4F2..0x56B571`). A missing record falls back to `DEFAULT`.
The result is bound to `CARSKIN` at `0x56F520` and `0x56F5A0`. In the part
database 373 of 379 attributes with this key name a material record; the
remaining six name `0xD6640DFF`, which takes the fallback. For example,
`GLOSS_L1_COLOR03` selects `REGPAINTBLUE`, while `GLOSS_L1_COLOR05` and
`GLOSS_L1_COLOR08` select `REGPAINTRED`.
Retail stock 240SX installs `GLOSS_L1_COLOR08` (165, 75, 15, gloss 128),
which selects `REGPAINTRED` and explains the orange car in retail-01/05/06.

The following are disk payload offsets, with `p` the payload start. The
executable reads the same offsets from the selected object at
`0x40FAF0..0x40FC42`, applies flag gates at `0x40FBFF..0x40FD24`, and
uploads the vectors at `0x40FFB4..0x410087`:

| Effect input | Fields in `p` | Calculation before upload |
| --- | --- | --- |
| `DiffuseMin` | scale `+0x30`, RGB `+0x34..+0x3C`, alpha `+0x50` | RGB times scale; alpha unchanged |
| `DiffuseRange` | scale `+0x40`, RGB `+0x44..+0x4C`, alpha `+0x54` | RGB times scale minus minimum; alpha minus minimum |
| `SpecularMin` | scale `+0x5C`, RGB `+0x60..+0x68` | RGB times scale |
| `SpecularRange` | scale `+0x6C`, RGB `+0x70..+0x78` | RGB times scale minus minimum |
| `SpecularPower` | `+0x58` | times runtime global `0x7070D4`, initially 1.1 in this executable |
| `EnvmapMin` | scale `+0x80`, RGB `+0x84..+0x8C` | RGB times scale times the 0.5 constant at `0x6CC7DC` |
| `EnvmapRange` | scale `+0x90`, RGB `+0x94..+0x9C` | RGB times scale times 0.5, minus minimum |
| `EnvmapPower` | `+0x7C` | copied unchanged |

The on-disk values distinguish the materials without assigning semantics
to unnamed geometry flags. Selected examples below are IEEE-754 floats
read directly from `GLOBALB.BUN`, not fitted visual values:

| Name (record offset) | Diffuse min/range scales | Diffuse alpha min/max | Specular scale/power input | Environment min/max scales, power |
| --- | --- | --- | --- | --- |
| `CHROME` (`0x0690`) | 0 / 0 | 1 / 1 | 1 / 4 | 1.9 / 1.8, 2 |
| `METPAINTBLUE` (`0x2610`) | 0.396 / 1.21 | 1 / 1.115 | 0.831 / 3.446 | 1.032 / 0.151, 0.338 |
| `REGPAINTBLUE` (`0x3528`) | 0.662 / 1.072 | 0.993 / 0.165 | 1 / 5.2 | 0.674 / 0.742, 0.262 |
| `SKIN` (`0x42F0`) | 1.093 / 1.354 | 1.019 / 1.379 | 0.311 / 2.236 | 0 / 0, 0 |
| `WINDSHIELD` (`0x4EC0`) | 1 / 0.367 | 0.697 / 0.082 | 0.669 / 2.392 | 1.5 / 0.18, 1.4 |

The table labels `+0x40` and `+0x54` as range targets because the executable
subtracts the computed minimum (`0x40FB2E..0x40FB5F`), and likewise
subtracts the environment minimum (`0x40FBED..0x40FC42`). A value such as
`CHROME` environment scale 1.9 is a stored multiplier, not a sampled RGB
value or a measured on-screen brightness. The actual pixel still depends
on the six cube faces, the camera vector, the diffuse texture and alpha,
the harmonic coefficients, the geometry flags and the selected technique
(`car.efx`, `0x40FBFF..0x40FD24`).

## Port implementation and approximation boundary

`--car-shading on|off` selects the new path in the desktop viewer, desktop
race and Switch race. Default is `off`, pending the mandatory console
performance and image gates. The off-state stock and replay image pins
remain unchanged. This switch affects rendering only; no shading value
feeds the fixed-step simulation.

Observed material coefficients are decoded from bounded top-level
`0x00135200` records by `src/car_shading.cpp` only when car shading is
enabled, joined through the geometry material key, and uploaded to the CPU
reference and deko3d. The default shading-off preparation does not parse this
table. Signed ranges
are retained, including chrome's decreasing environment range. The port
uses the executable's initial specular multiplier 1.1; later mutation of
that global is untraced. The installed `BASE_PAINT` part supplies the paint
material through attribute `0x6BA02C05`; an unmatched hash selects the
on-disk `DEFAULT` record. A color-only skin has no part identity, so the port
chooses the nearest `GLOSS_L1_COLOR` swatch by squared distance over RED,
GREEN, BLUE and GLOSS, then reads that swatch's material attribute. This
surrogate part choice is a port rule. The project's blue stock skin
(30, 60, 160, gloss 128) chooses `GLOSS_L1_COLOR03` (8, 60, 161) and
`REGPAINTBLUE`; the tuned red skin (200, 30, 30, gloss 128) chooses a
gloss-red swatch and `REGPAINTRED`. The project's blue stock skin predates
this PR and differs from retail's orange stock 240SX paint. Other decoded keys
distinguish chrome, windshield, plastic and lamp lenses. Lamp emission
remains the separate existing lamp state.

The port reads the existing smooth vertex normals and transforms them by
the inverse transpose of each placed instance, including the raw-axis to
world-axis conversion. A zero normal falls back to world up; normals face
the viewer for this two-sided approximation. The view dot is clamped to
0.01..1. The decoded diffuse, specular and environment ranges depend on
that dot and their respective powers. The geometry flag gates remain those
traced in [MATERIALS.md](MATERIALS.md).

Approximations replace the untraced harmonic inputs and live cube: one broad
light supplies diffuse and gloss; the retail harmonic specular luminance and
its 0.5 factor are not reproduced. A world-fixed analytical sky, ground,
horizon and bright vertical band supply the environment. These lighting
colors and widths are chosen port constants, not retail data. Lighting is
evaluated per vertex and interpolated perspective-correctly; the fragment
stage combines it with the base texel and its alpha. Retail instead samples
a cube in the fragment stage. This is not an image-equivalence claim for
the original game, nor a track-local reflection of passing objects.

The viewer can set `--material-state retail` independently. In both race
apps, `--car-shading on` also enables the existing retail car alpha/blend
state so glass uses its decoded view-dependent alpha. `off` preserves the
historical race baseline. Consequently, the paired on/off measurement
includes that state change as well as the new shading; it must not be
reported as the cost of vertex arithmetic alone. Both variants use the
same retail track state.

## Cost and validation

The expected GPU cost, stated before implementation, was extra normal/view
arithmetic per car vertex, three power evaluations, seven interpolated
scalars and a short fragment combination. There is still one base-texture
lookup, no added texture allocation, no added car draw and no capture pass.
Normals already occupy the uploaded vertex stride. The material payload
extends from 80 to 192 bytes inside the existing aligned 256-byte slot;
CPU command metadata grows because each draw stores the appended vectors.
`nfsu_race_run_tests` pins and prints the resulting memory estimate, and
compares complete cached and reference command streams, including program,
uniforms and state. None of these structural costs is a GPU-time estimate.

`nfsu_draw_commands_retail` pins the shaded stock 240SX at yaw 35, -45 and
145 degrees. Comments beside each hash state what that angle exercises;
legacy off-state pins stay intact. `tools/race_replay_check.py --car-shading`
pins race frame 180 while checking the complete existing result, per-step
hashes and trajectory. Its default mode retains all legacy race frame pins.
Desktop default and Release runs must both agree with these pins. The
synthetic `nfsu_gpu_upload_tests` checks bounded record parsing, signed
ranges, normal-dependent pixels, off-state invariance and command data.
Shader manifests check the shared uniform layout and normal attribute in
both source and compiled Switch stages.

Console evidence remains required: the same build, each of `--car-shading
on` and `--car-shading off`, launched with `-- sdmc:/nfsu --replay
--profile-frame`, with GPU SCENE page 7 and REPLAY page 2 captured after
60 seconds. The page footer identifies the mode. Collect handheld and
docked measurements and a photograph of the shaded car. Compare the GPU
scene mean and worst against the 16.67 ms budget. No 30 fps fallback is
allowed. Until those measurements exist, the port cannot claim that this
effect fits 60 fps or that the console image matches the CPU reference.
