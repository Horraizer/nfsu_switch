# Car material and alpha state

This document records the bounded rendering state recovered from the retail
PC build. It separates direct executable and effect-source observations from
correlations. The CPU renderer does not yet apply this state; see
`docs/RENDERER.md`.

Reference files:

- `speed.exe`: 3178496 bytes, SHA-256
  `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
- `fx/car.efx`: 7200 bytes, SHA-256
  `4055E8F0FFA65752AD4D9F9CF36AFBDF67A07DD9F0C15B8BA31A57A1F726FEA7`.
- `Cars/240SX/GEOMETRY.BIN`: SHA-256
  `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`.
- `Cars/240SX/TEXTURES.BIN`: SHA-256
  `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA`.
- `GLOBAL/GLOBALB.BUN`: SHA-256
  `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`.

All virtual addresses below use image base 0x00400000. In `.text`, the raw
file offset is `VA - 0x00400000`.

## Observed: draw-state sources

The draw path keeps three inputs separate:

1. Material byte +28 selects a resolved texture pointer from mesh table
   +0x2C at 0x40C9F8..0x40CA03.
2. Material byte +32 selects a material/effect-key object from mesh table
   +0x3C at 0x40CA0E..0x40CA22. A value of 0xFF yields null.
3. Material dword +56 is copied unchanged to queued draw +0x14 by 0x40F1E0.

The selected texture pointer is stored at queue-group +0x10 and passed to
0x40F3D0 by 0x40EE10. Loader code at 0x5453D0..0x5454EA and 0x545900 onward
shows that this pointer addresses the in-place 124-byte 0x33310004
texture-info record. Alpha state therefore comes from texture-info bytes
+85 and +86, not from geometry material flags.

The public `TextureRecord` preserves the raw values as `alpha_test_mode` and
`alpha_blend_mode`. `nfsu_probe textures` and `pack-set` print the raw value
beside its decoded state.

## Observed: alpha test and blending

At 0x40F436, texture-info byte +85 is compared with 1. Function 0x410650
then writes either effect parameters or the equivalent fixed-function D3D
states:

- +85 == 1: alpha test enabled, reference 0xAA;
- every other value: alpha test disabled.

Global setup at 0x408B59..0x408B65 sets render state 25 to 5. These are
`D3DRS_ALPHAFUNC` and `D3DCMP_GREATER`, so the effective test is source
alpha greater than 0xAA.

At 0x40F451..0x40F4CC, byte +86 selects blending through 0x410550:

| Raw +86 | Blend enable | Source | Destination | API classification |
| ---: | --- | --- | --- | --- |
| 0 | off | not used | not used | `disabled` |
| 1 | on | `SRCALPHA` (5) | `INVSRCALPHA` (6) | `source-over` |
| 2 | on | `SRCALPHA` (5) | `ONE` (2) | `additive` |
| other nonzero | on | `SRCALPHA` (5) | `INVSRCALPHA` (6) | `source-over` |

The effect handle slots are +0xBC `BaseAlphaTestEnable`, +0xC0
`BaseAlphaRef`, +0xC4 `BaseAlphaBlendEnable`, +0xC8 `BaseSrcBlend`, and
+0xCC `BaseDestBlend`. The fallback render-state numbers are 15, 24, 27,
19, and 20 respectively.

The 119 records in the 240SX local texture pack have this distribution:

| +85 | +86 | Records |
| ---: | ---: | ---: |
| 0 | 0 | 73 |
| 0 | 2 | 1 |
| 1 | 0 | 1 |
| 2 | 1 | 34 |
| 2 | 2 | 10 |

Only `240SX_TIRE` has +85 == 1. In particular, observed value 2 does not
enable the alpha test.

## Observed: `car.efx`

Every byte of `fx/car.efx` is XOR-obfuscated with 0xAA. Decoding it in
memory produces readable HLSL/FX source. The file defines three techniques:
`car` (VS 1.1 plus PS 1.1), `car_fixed_envmap`, and `car_fixed`. All three
take the five `Base*` alpha and blend states from the runtime; the effect
does not derive them from geometry flags.

The programmable path declares a diffuse 2D texture, cube `EnvMapTex`,
`DiffuseMin/Range`, `SpecularMin/Range`, `EnvmapMin/Range`,
`SpecularPower`, and `EnvmapPower`. The source contains the misspelled
semantic `ENVMAPANGE`; this is in the retail file.

Its relevant equations are:

```text
vdotn = max(0.01, dot(eye_direction, normal))
diffuse_scale = DiffuseMin + vdotn * DiffuseRange
specular_scale = SpecularMin + pow(vdotn, SpecularPower) * SpecularRange
envmap_scale = EnvmapMin + pow(vdotn, EnvmapPower) * EnvmapRange

diffuse_color = diffuse_sample * diffuse_lighting
envmap_color = envmap_sample *
    (2 * (EnvmapMin + specular_color.a * EnvmapRange))
output = 2 * diffuse_color
output.rgb += 2 * diffuse_color.a *
    (specular_color + 2 * envmap_color)
```

The output alpha remains `2 * diffuse_sample.a * diffuse_scale.a`.
Diffuse alpha also scales the specular and environment RGB terms. The fixed
environment technique computes alpha as texture alpha times texture factor
times two and preserves that alpha through its second texture stage.

## Observed: material/effect-key objects and geometry flags

Loader 0x540151..0x540191 resolves every 0x00134013 hash against registry
object +0x0C and writes the pointer into entry +4. There is no key-specific
draw branch. Queue code stores that pointer at command +0x20; 0x40EE7B..
0x40EE88 calls 0x40FAE0 with the object and the queued geometry flags.

The effect-handle lookup at 0x40E458..0x40E47A and the uploads at
0x40FFB4..0x410087 prove this mapping:

| Effect value | Handle slot | Object source used by 0x40FAE0 |
| --- | ---: | --- |
| `DiffuseMin` | +0x50 | scale +0x30, RGB +0x34..+0x3C, W +0x50 |
| `DiffuseRange` | +0x54 | scale +0x40, RGB +0x44..+0x4C, W +0x54, minus minimum |
| `SpecularMin` | +0x58 | scale +0x5C, RGB +0x60..+0x68 |
| `SpecularRange` | +0x5C | scale +0x6C, RGB +0x70..+0x78, minus minimum |
| `SpecularPower` | +0x60 | global 0x7070D4 times object +0x58 |
| `EnvmapMin` | +0x64 | scale +0x80, RGB +0x84..+0x8C, global scale 0x6CC7DC |
| `EnvmapRange` | +0x68 | scale +0x90, RGB +0x94..+0x9C, minus minimum |
| `EnvmapPower` | +0x6C | object +0x7C |
| `EnvMapTex` | +0x80 | bound at 0x40EB30..0x40EB51 |

Direct geometry-flag consumers are bounded as follows:

- Bit 0x1 bypasses a bounds/cull call at 0x40C9DB and gates matrix upload at
  0x40EEA3 through 0x4102F0. This does not prove a stable name for the bit.
- Bit 0x100 is tested at 0x40FBFF. If it is clear, 0x40FC46 zeros only the
  environment min/range vectors. The environment vectors are also zeroed
  when global 0x700E74 is null at 0x40FC4C..0x40FC54.
- With bit 0x100 and the global enabled, 0x40FC5A tests bit 0x80. If 0x80 is
  clear, 0x40FC62..0x40FCE1 zeros diffuse and specular min/range but retains
  environment vectors. If it is set, all three groups survive.

This proves branch effects, not friendly names for bits 0x80 and 0x100.

## Retail correlations, not semantics

The 1242 material records of the 240SX geometry use eight flag values:

| Flags | Records |
| --- | ---: |
| 0x00000000 | 2 |
| 0x00004080 | 4 |
| 0x00004180 | 922 |
| 0x0000C180 | 6 |
| 0x00014080 | 19 |
| 0x00014180 | 263 |
| 0x00018000 | 3 |
| 0x0001C180 | 23 |

Bit 0x8000 correlates with windows but is not an alpha-enable bit. It occurs
on 32 records, while alpha-bearing DXT3 `240SX_BADGING` uses flags 0x4180
or 0x14180 and DXT3 `240SX_KIT00_HEADLIGHT_G` uses 0x4180. The same
`CLEARPLASTIC` and `WINDSHIELD` keys occur both with and without 0x8000.

Bit 0x10000 is not a simple environment-map enable. It occurs on all six
`MIRROR` bindings, but on none of 36 `ALUMINUM` bindings and only one of 17
`CHROME` bindings. It also occurs on ordinary `PLAINNOTHING` and `BOTTOM`
bindings.

`DECAL` is hash 0x02A05578 and has a 0x00135200 record in GLOBALB at file
offset 0x15A8. It resolves through the same registry and coefficient upload
as every other key. No draw-path branch compares the `DECAL` name or hash,
so a special decal blend rule is not proven.

Across the 240SX sample, 28 of 36 material-key hashes have exact
0x00135200 names and cover 887 bindings. Six still-unknown hashes cover 19
bindings. Across 16 cleanly parsed racer cars there are 18969 bound key
references and 70 distinct hashes; 32 keys covering 13583 references match
the GLOBALB table. These counts support the generic object model but do not
assign semantics to untraced flags.

## Implementation boundary

Observed and safe to implement:

- texture-info +85 alpha-test selection and its GREATER 0xAA rule;
- texture-info +86 blend selection;
- the `car.efx` equations and parameter groups;
- the exact flag-controlled zeroing described above.

Still unknown:

- transparent draw ordering and all conditions that choose one of the three
  car techniques;
- stable semantic names for geometry bits 0x1, 0x80, 0x100, 0x4000,
  0x8000, and 0x10000 beyond their direct branches;
- the identities of six 240SX material-key hashes and the exact registry
  object type hierarchy;
- whether all cars and non-car texture packs use the same +85/+86 policy.

Until draw ordering is recovered, the CPU renderer keeps its opaque default
and optional diagnostic cutoff. It must not guess blending from geometry
flags.
