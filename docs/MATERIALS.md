# Car material and alpha state

This document records the bounded rendering state recovered from the retail
PC build. It separates direct executable and effect-source observations from
correlations. `nfsu_render` applies the observed part of it under
`--material-state retail`, and only that part; `docs/RENDERER.md` states
exactly what the renderer does with each rule and where it stops.

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

The follow-up [car shading trace](CAR_SHADING.md) decodes the numeric
`0x00135200` material records, the runtime cube-map creation and the
device-dependent technique choice. This document keeps the earlier
alpha-state and flag-gate trace.

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

### Observed: the same pair over every car pack and the global bundles

`nfsu_probe textures` over the 1820 records of the 39 `Cars/*/TEXTURES.BIN`
packs, and `nfsu_probe pack-set` over the 253 records of `GLOBALA.BUN`,
`GLOBALB.BUN`, `INGAMEA.BUN` and `INGAMEB.BUN`, give:

| +85 | +86 | Car pack records | Global bundle records |
| ---: | ---: | ---: | ---: |
| 0 | 0 | 1079 | 33 |
| 0 | 2 | 11 | 2 |
| 1 | 0 | 62 | 21 |
| 2 | 0 | 20 | 5 |
| 2 | 1 | 460 | 117 |
| 2 | 2 | 184 | 75 |
| 2 | 6 | 4 | 0 |

Three facts follow, and they are counts, not semantics:

- +85 takes only the values 0, 1 and 2 in every pack read, and the same
  three `TextureAlphaBlend` classes cover both car and global packs. The
  240SX pack is not special; it simply has a single alpha-tested record
  while `GLOBALB.BUN` has 13.
- +86 == 6 occurs on four records, in `350Z` and `IMPREZA`, all named
  `350Z_STYLE03_HEADLIGHT_*` or `IMPREZA_STYLE03_HEADLIG*`. Retail data
  therefore reaches the "other nonzero" row of the table above, so the
  source-over fallback is exercised and is not a hypothetical branch.
- +85 == 2 occurs with +86 == 0 as well as with 1 and 2, so the two bytes
  are independent selectors and neither implies the other.

This narrows, but does not close, the open question about non-car packs:
only texture packs that a car scene can read were surveyed.

## Observed: the draw queue and the order it produces

The draw path does not draw a material when it reaches it. It appends the
material to a queue and the queue is walked later. The queue is a fixed
table, so the order is a function of the data, never of the view.

### The table

`0x689B30..0x689B43` constructs 16 elements of 0x38 bytes at `0x78A4E8`
with element constructor `0x40ED20`, which itself builds 7 elements of 8
bytes. The table is therefore `buckets[16][7]`, 112 circular list heads of
`{next, prev}`, spanning `0x78A4E8..0x78A868`. `0x78A6A8` is exactly
`0x78A4E8 + 8 * 0x38`, the address of `buckets[8][0]`.

Two pools feed it, both named by the retail strings they are constructed
with:

| Pool | Element | Count | Handle | Name string |
| --- | ---: | ---: | --- | --- |
| slot | 0x20 | 1024 | `0x7363D4` (`0x40F375`) | `0x6CBFE0` `eTextureBucketSlotPool` |
| command | 0x2C | 4096 | `0x7363D8` (`0x40F390`) | `0x6CBFD4` `eMeshRender` |

Both counts are the first block only. `0x566DF5` sets pool flag bit 0x1 and
the three creation sites clear only bit 0x2 (`0x40F31F`, `0x40F366`,
`0x40F38D`), so `0x56716A..0x56717D` grows the pool instead of failing.
Neither count is a cap.

Slot fields: `+0x10` the texture-info record (`0x40F20D`), `+0x14` the
effect object (`0x40F2CE`), `+0x18`/`+0x1C` its command list, `+0x08` the
command count (`0x40F2C7`). Command fields include `+0x14` the material
dword +56 (`0x40F287`) and `+0x20` the material-key object (`0x40F295`),
which is what `0x40EE7B..0x40EE88` and `0x40EEA3` later read.

### The bucket a material lands in

`0x40F1E0` is the append, called only from `0x40CA54`. At
`0x40F225..0x40F24D`, with the texture-info record in `esi` and the effect
object in `ebp`:

```text
row    = (texture_info[+0x54] != 0 ? 8 : 0) + (signed char) texture_info[+0x51]
column = effect_object[+0x00]
```

`0x40F233` and `0x40F241` multiply the row by 7 and `0x40F238` /`0x40F246`
scale by 8 against the two table bases, which is the two-dimensional index.
`0x40F24D..0x40F258` links the slot in before the head, that is at the
tail, so a bucket is a FIFO.

So two more texture-info bytes are draw state: `+81` and `+84`. The public
`TextureRecord` and `RenderTexture` preserve both raw, and
`texture_draw_layer` / `render_draw_layer` hold the one decoded rule.
`nfsu_probe textures` and `pack-set` print the row beside the raw pair.

The retail code does not range-check the row, so an out-of-range `+81`
would index outside the table. It never happens in the data read here. Over
the 2073 records of the 39 car packs and the four global bundles:

| Row | +84 | +81 | Records |
| ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 1104 |
| 3 | 0 | 3 | 8 |
| 5 | 0 | 5 | 359 |
| 6 | 0 | 6 | 1 |
| 8 | 1 | 0 | 124 |
| 11 | 1 | 3 | 23 |
| 12 | 1 | 4 | 1 |
| 13 | 1 | 5 | 440 |
| 14 | 1 | 6 | 12 |
| 15 | 1 | 7 | 1 |

`+81` is never negative and never above 7, and `+84` is only 0 or 1, so
every row is inside 0..15 on its own. `nfsu_render` clamps anyway, because
it must not reproduce an out-of-bounds index.

### One open slot per texture

`0x40F1E9..0x40F1F5` reads the texture's open slot through
`*(texture_info + 0) + 0x1C` and reuses it when slot `+0x14` still holds
the incoming effect object, jumping straight past slot allocation to the
command allocation at `0x40F260`.

`*(texture_info + 0)` is a 32-byte companion record: the loader at
`0x545900` walks the 124-byte `0x33310004` records with stride 0x7C and the
`0x33310005` chunk with stride 0x20 and writes the companion into record
`+0x00` at `0x545995`. `0x54546A..0x5454A5` refuses a pack whose
`0x33310005` size over 32 does not equal the record count. The counts do
match in every pack read: 119 records and 3808 bytes in
`Cars/240SX/TEXTURES.BIN`, 80 and 2560 in `CIVIC`, 120 and 3840 in
`SKYLINE`, 75 and 2400 plus 8 and 256 in `GLOBAL/GLOBALB.BUN`.

The open-slot pointer has exactly three writers: `0x40F222` and `0x40F128`
set it on append, and `0x40F079` clears it when the flush finishes draining
that slot. It is not cleared at the start of a flush and not per frame.

The scope that batching therefore covers is one flush cycle. `0x40ED50` has
14 call sites: 0x409623, 0x4096FF, 0x409724, 0x409763, 0x4097A4, 0x409867,
0x409972, 0x409983, 0x4099AC, 0x409A55, 0x40ABD3, 0x40D2F1, 0x40D664 and
0x40D9AB. None of them is inside the per-model material walk at
0x40C890..0x40CB00, so no flush can split one car's materials and all of
them share one fill. Whether a fill ever holds more than one car is not
established here and does not matter for a single-car scene.
Therefore, inside one queue fill, a material reuses its texture's slot even
when other textures were submitted in between: the queue batches by
texture over the whole fill, not over consecutive runs. A mismatched effect
object does not search for another slot, it replaces the pointer.

### The walk

`0x40ED50` walks `0x40ED66` onward with the inner loop ending at
`0x40F0AC..0x40F0C3` (7 effects, `0x71ABB8 + i * 0xE0`, bound
`0x71B1F0`) and the outer at `0x40F0B2..0x40F0CF` (16 rows, bound
`0x78A868`). Rows ascend outermost, effects inside them, and each list
drains from the front, so the walk is row-major FIFO.

The 7 effects are `0x71ABB8 + i * 0xE0` with `field +0` set to `i` at
`0x40E210` and the file name taken from the 0x80-stride table at
`0x7014E0`:

| Index | File |
| ---: | --- |
| 0 | `fx\world.efx` |
| 1 | `fx\worldheadlight.efx` |
| 2 | `fx\worldreflect.efx` |
| 3 | `fx\worldheadlightreflect.efx` |
| 4 | `fx\worldbone.efx` |
| 5 | `fx\car.efx` |
| 6 | `fx\filter.efx` |

`0x40C890` picks the effect from the model flags against the 12-byte-stride
table at `0x701440`: mask 0x01 selects 4, 0x40 selects 2, 0x80 selects 5,
and everything else 0. A car is effect 5.

`0x40C910` walks the material records strictly sequentially, stride 0x3C at
`0x40CA6C`, and `0x40C96B..0x40C97D` loads the effect once before that
loop. The only per-material effect override, `0x40CA26..0x40CA38`, requires
effect index 1, so it cannot fire for a car.

### What that means for one car

Every material of one car uses effect 5, so the column is constant and the
row alone selects the bucket. The order is fully determined:

1. ascending row, `8 * (texture_info[+84] != 0) + (signed) texture_info[+81]`;
2. inside a row, the textures in the order each was first used;
3. inside a texture, the material records in record order.

There is no sort of any kind in the append or the walk: no comparison of
two entries, no float key, no distance, no `qsort`. The only ordering
inputs are the two table bases, the two texture-info bytes, the effect
index and submission order.

For the 240SX pack the rows present are:

| Row | +84 | +81 | Records | What they are |
| ---: | ---: | ---: | ---: | --- |
| 0 | 0 | 0 | 67 | opaque body and interior, plus alpha-tested `240SX_TIRE` |
| 5 | 0 | 5 | 18 | source-over glass and lights |
| 8 | 1 | 0 | 8 | opaque light glass, plus additive `240SX_NEON` |
| 13 | 1 | 5 | 26 | source-over and additive glass and lights |

### Depth writes and culling

`0x40F4D4..0x40F543` reads texture-info `+86` a second time and branches on
it and on the current effect (`0x73640C`, set per bucket at `0x40EDAD`).
The listing is contiguous; `0x40F51B` is a `mov`, which leaves the flags of
`0x40F518` intact, so `0x40F51D` still tests `effect index == 5`:

| `+86` | effect | Writes |
| --- | --- | --- |
| 0 | 5 | `CULLMODE = D3DCULL_NONE` (`0x40F4F2`), `ZWRITEENABLE = TRUE` (`0x40F504`) |
| 0 | not 5 | `ZWRITEENABLE = TRUE` (`0x40F504`) |
| nonzero | 5 | `CULLMODE = D3DCULL_CW` (`0x40F524`), `ZWRITEENABLE = TRUE` (`0x40F536`) |
| nonzero | not 5 | `ZWRITEENABLE = FALSE` (`0x40F543`) |

The pushed cull values are the literals 1 at `0x40F4ED` and 2 at
`0x40F51F`. `D3DCULL` is `NONE = 1`, `CW = 2`, `CCW = 3`, so the blended
branch selects `CW`.

**A blended car draw writes depth.** The `ZWRITEENABLE = FALSE` store is on
the branch that requires the effect not to be the car effect. No shipped
`.efx` declares `ZWriteEnable` except `filter.efx` and the two
`worldheadlight` effects, so nothing downstream overrides it.

The cull mode is the only other state the blend byte changes for a car, and
`car.efx` is the only one of the seven effects that declares no `CullMode`
of its own, which is why the runtime sets it explicitly for effect 5.

### Alpha function and reference are stable across a car draw

`D3DRS_ALPHAFUNC` (25) has exactly one write in the whole image,
`0x408B65`, value 5. No `.efx` declares `AlphaFunc`, so the effect state
applier cannot change it either. `D3DRS_ALPHAREF` (24) has three writes,
`0x408DF7`, `0x40D949` and `0x4106D6`; the last is inside `0x410650`, which
`0x40F44C` calls for every slot, and every `.efx` pass declares
`AlphaRef = (BaseAlphaRef)` fed from the same place. So the GREATER 0xAA
rule is live for every car draw.

### Materials bound to `DEFAULTALPHA` are skipped

`0x40CA03..0x40CA0C` compares texture-info `+0x24`, the name hash, with the
global at `0x78A4E0` and jumps past the material when they are equal. The
global is the hash of the string `"DEFAULTALPHA"` at `0x6CC00C`, built at
`0x689AD0..0x689AF0`; it is 0x66994F4A. `GLOBALB.BUN` record 29 is that
texture. No material record of any of the 39 car geometries binds that
hash, so the rule never fires on a car and `nfsu_render` does not implement
it.

### The executable uses Direct3D 9

`Direct3DCreate9` is imported from `d3d9.dll` (IAT `0x69742C`, name strings
`0x6D2396` and `0x6D23A6`), the device global is `0x73636C`, and
`SetRenderState` is vtable `+0xE4`, which is the Direct3D 9 slot. The
render-state numbers are the same in Direct3D 8 and 9, so every state
decoded above is unaffected.

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

Diffuse alpha also scales the specular and environment RGB terms. The fixed
environment technique computes alpha as texture alpha times texture factor
times two and preserves that alpha through its second texture stage.

### The environment term, written out exactly

The block above omits the two `* 0.5f` factors the vertex shader applies,
so it cannot be implemented as written. The vertex shader ends its
diffuse and specular work with

```text
OUT.diffuse_color.w   = diffuse_scale.w
OUT.diffuse_color    *= 0.5
OUT.specular_color.w  = envvdotn
```

where `envvdotn = pow(vdotn, EnvmapPower)`, and the pixel shader uses
`IN.specular_color.w`, not the vertex `envmap_scale`, for the environment
scale. Substituting gives two exact identities:

```text
output.a = diffuse_sample.a * diffuse_scale.w

environment_term.rgb =
    4 * diffuse_sample.a * diffuse_scale.w * envmap_sample.rgb *
    (EnvmapMin.rgb + pow(vdotn, EnvmapPower) * EnvmapRange.rgb)
```

Both are in the shader's 0..1 space. `diffuse_scale.w` is
`DiffuseMin.w + vdotn * DiffuseRange.w`. The vertex shader computes
`envmap_scale` and never reads it; only `EnvmapMin`, `EnvmapRange` and
`envvdotn` reach the environment term.

These two identities are what `nfsu_render` reproduces. The retail
`0x00135200` coefficient values are decoded in [CAR_SHADING.md](CAR_SHADING.md),
but `HarmonicCoeff` and the cube contents are not. The reference still
takes coefficients as explicit inputs and never supplies a retail value.

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

Every one of those stores is a single `mov dword ptr [esp+disp], 0`, one
per float lane, and the stack slots are the six `D3DXVECTOR4` buffers the
six `SetVector` uploads read from (`lea edi` at 0x40FFBC, 0x40FFD7,
0x40FFF2, 0x41000D, 0x410028 and 0x410043). The W lane is among them:
`DiffuseMin.w` is zeroed at 0x40FC7A, `DiffuseRange.w` at 0x40FC9A,
`EnvmapMin.w` at 0x40FCFC and `EnvmapRange.w` at 0x40FD1C. Each vector is
uploaded whole, so a zeroed W lane reaches the effect as such.

That makes the whole consequence of the 0x80 branch exact without needing
the coefficient values. The vertex shader computes
`OUT.diffuse_color = harmonic(normal) * diffuse_scale` and
`OUT.specular_color.xyz = specular * specular_lum * specular_scale * 0.5`,
and the pixel shader computes
`output = 2 * diffuse_sample * IN.diffuse_color` with
`output.rgb += 2 * diffuse_color.w * (specular_color + 2 * envmap_color)`.
With `diffuse_scale` and `specular_scale` zero in every lane, every one of
those terms is zero, so the effect writes `(0, 0, 0, 0)` whatever
`HarmonicCoeff`, the texture and the remaining coefficients hold. Colour
and alpha both vanish, not just the alpha.

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

### Observed: how much retail data each gate branch reaches

`nfsu_probe texture-bindings` over the 28940 material records of the 39
`Cars/*/GEOMETRY.BIN` files finds 16 distinct flag values. Classified only
by the two branch conditions above, with the runtime global taken as
enabled:

| Branch | Records |
| --- | ---: |
| environment vectors kept (0x100 set) | 26787 |
| environment vectors zeroed (0x100 clear) | 2153 |
| diffuse and specular zeroed (0x100 set, 0x80 clear) | 6 |

The six records of the third row are four with flags 0x00014100 and two
with 0x00004100. The branch is therefore reached by retail data and is not
dead, but it is rare enough that no car is visibly characterised by it. In
the 1242-record 240SX sample it does not occur at all: every value there
that carries 0x100 also carries 0x80.

These are counts of a branch condition and they name no bit. The retained
coefficients are decoded in [CAR_SHADING.md](CAR_SHADING.md), but their
effect on a complete frame still depends on the cube and lighting inputs.
The zeroed case is exact: a closed diffuse gate forces `diffuse_scale.w`
to zero, so those six records draw with source alpha 0 whatever their
coefficient object holds.

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

Observed, and applied by `nfsu_render --material-state retail`:

- texture-info +85 alpha-test selection and its GREATER 0xAA rule;
- texture-info +86 blend selection, as the two integer blend rules in
  `docs/RENDERER.md`;
- the draw queue order: texture-info +84 and +81 give the row, textures
  batch in first-use order inside a row, and material records keep their
  order inside a texture;
- depth writes stay on for a blended car draw;
- the exact flag-controlled zeroing described above, as
  `material_coefficient_gates`, including the W lane, so a closed diffuse
  gate leaves `diffuse_scale.w` at zero and takes the source alpha and the
  environment term with it;
- the `car.efx` environment identity, evaluated from explicitly supplied
  coefficients.

Observed, and not applied, each with its reason:

- the per-draw cull mode, `D3DCULL_NONE` when +86 is 0 and `D3DCULL_CW`
  when it is not. The reference draws both faces by default because 43
  source triangles of the 240SX wind against their normals, and mapping
  `D3DCULL_CW` onto its own screen-space winding needs the retail vertex
  and viewport convention proven first. `--cull` stays the diagnostic it
  was;
- the `DEFAULTALPHA` skip, which no car material reaches;
- the effect's harmonic diffuse lighting and its specular term.
  `HarmonicCoeff` is undecoded; the on-disk `Specular*` coefficients are
  decoded in [CAR_SHADING.md](CAR_SHADING.md), but are not wired into the
  renderer. It keeps its own diagnostic flat shade and says so.

Still unknown:

- which technique a particular retail PC device selects. The technique
  selection loop and its capability guards are traced in
  [CAR_SHADING.md](CAR_SHADING.md), but no specific device run is recorded;
- runtime mutations of the material/effect-key objects. Their on-disk
  values are decoded in [CAR_SHADING.md](CAR_SHADING.md); the renderer still
  takes each coefficient as an explicit input whose default makes its term
  vanish;
- the contents and full update policy of `EnvMapTex`. Its runtime cube-map
  creation and six-face render path are traced in
  [CAR_SHADING.md](CAR_SHADING.md); the renderer takes a constant colour in
  place of a cube sample;
- stable semantic names for geometry bits 0x1, 0x80, 0x100, 0x4000,
  0x8000, and 0x10000 beyond their direct branches. `PreparedDraw::flags`
  and the untouched 60-byte record are carried raw, and the two branch
  conditions are the only thing any code reads out of them;
- the identities of six 240SX material-key hashes and the exact registry
  object type hierarchy;
- whether non-car texture packs use the same +85/+86 policy. Every car pack
  and every global bundle that a car scene reads was surveyed above; track
  and front-end packs were not.

`--material-state off` stays the default, so none of this changes a frame
unless it is asked for. Blending is still never guessed from geometry
flags.
