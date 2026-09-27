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
a six-iteration loop. Its per-face path constructs a view matrix and calls
the scene draw at `0x40B250..0x40B2C0`. The effect binds the resulting
cube pointer from `0x73646C` to its `EnvMapTex` handle at
`0x40EB30..0x40EB51`. The retail reflection source is therefore a live
render target, not a cube stored as a texture in `GLOBALB.BUN`. The trace
does not yet identify every object category drawn into its faces, the
capture position policy, or its update frequency. A static or simplified
cube on Switch would be an approximation of this path.

The Direct3D method interpretation above follows the interface calls and
the [Direct3D 9 cube render-target procedure](https://learn.microsoft.com/en-us/windows/win32/direct3d9/creating-cubic-environment-map-surfaces).

## Material records and paint parameters

`GLOBALB.BUN` contains 139 `0x00135200` records with a 160-byte payload.
For example, the `ALUMINUM` record begins at file offset `0x0000`,
`CHROME` at `0x0690`, `METPAINTBLUE` at `0x2610`, `REGPAINTBLUE` at
`0x3528`, `SKIN` at `0x42F0`, and `WINDSHIELD` at `0x4EC0`. Each payload
starts after the eight-byte chunk header, has a name hash at payload
`+0x0C`, and an ASCII name at payload `+0x14`. The executable resolves a
geometry `0x00134013` key by hash against a registry object's `+0x0C`
at `0x540151..0x540191`; material byte `+32` selects the key slot at
`0x40CA0E..0x40CA22`. The selected object reaches the coefficient upload
at `0x40EE7B..0x40EE88` and `0x40FAE0`. Hence a car's geometry chooses a
named material key, while the coefficients are stored in the global
material record. Runtime replacement or mutation of these objects has
not been traced.

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
`docs/CAR_SCENE.md`). The presence of global `REGPAINTBLUE` and
`METPAINTBLUE` records does not prove either is used for this raw base mesh
or for the project's chosen blue stock skin. Which material key a retail
paint customization substitutes at runtime remains untraced.

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
| `SpecularPower` | `+0x58` | times runtime global `0x7070D4` |
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

## Port boundary and validation

The CPU renderer and deko3d shader currently use a flat diagnostic light
and a constant environment color (`src/render.cpp`,
`shaders/textured.frag.glsl`). Replacing those with decoded material
coefficients and a cube sample will change their images; this trace alone
does not predict a retail-equivalent frame. The six-face capture's scene
contents and harmonic inputs remain unknown, so any initial image-based
lighting term must be named an approximation. `docs/MATERIALS.md` records
the exact geometry-flag gates that the port already exposes.

For issue #182, the stock painted 240SX should be pinned at the default
camera and two orbit angles in tests, with values kept in those tests as
required by `CLAUDE.md`. The race replay also needs to remain equal to its
existing test pins. A fragment cube sample adds one dependent texture
lookup per car pixel; a literal six-face capture adds up to six scene
render passes and their draw/clear work when refreshed. Those are cost
components, not a frame-time estimate. The Switch implementation must
measure GPU frame time against the project's 60 fps budget before a
retail-style live capture can be accepted. This trace adds no render work.
