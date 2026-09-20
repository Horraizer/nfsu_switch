# Car part placement

Where the game puts each installed car part. Everything here was recovered
read-only from a user-provided retail PC installation. No game data is stored
in this repository.

Observed, inferred and unknown are kept apart. "Observed" means bytes or
instructions that were read at the address given. "Inferred" means a
conclusion drawn from several observations, with the observations named.
"Unknown" means exactly that; no field is given a meaning because its value
looks plausible.

## Reference samples

- `speed.exe`, 3178496 bytes, SHA-256
  `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
  PE32, image base `0x00400000`. All three sections have equal virtual and
  raw addresses, so a file offset equals its RVA and a virtual address is the
  file offset plus `0x400000`. Sections: `.text` `0x00401000..0x00696EA7`,
  `.rdata` `0x00697000..0x006D3439`, `.data` `0x006D4000..0x00794368`.
  Addresses below are virtual addresses.
- `Global/GLOBALB.BUN`, 2361176 bytes, SHA-256
  `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`.
- `Cars/240SX/GEOMETRY.BIN`, 4780816 bytes, SHA-256
  `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619`.

## Summary

There are three separate placement mechanisms, and which one applies depends
on the part:

1. **Most parts are authored in car space and are drawn at the car's own
   matrix**, with no transform of their own. This covers the body, bumpers,
   hood, roof, trunk, skirts, the spoiler, the mirrors and the decals.
2. **The exhaust and the licence plate are placed by named attachment
   markers** stored in geometry chunk `0x0013401A` on another mesh, usually
   the rear bumper. A part with no matching marker is simply not drawn.
3. **The four wheels and the four brake rotors are placed from a per-car
   table in `GLOBALB.BUN`.** The same four positions also feed a per-part
   culling array.

## Attachment markers, geometry chunk `0x0013401A` (observed)

The loader is at `0x005400E3`:

```text
0x005400E3  cmp eax, 0x13401a
0x005400EA  lea eax, [esi + 0x17]        esi is the chunk header
0x005400EF  and ecx, 0xfffffff0          records start at align_up(payload, 16)
0x005400F5  mov [ebx + 0x80], ecx        solid + 0x80 = the marker table
0x005400FB  mov edx, [esi + 4]           the payload size
0x005400FE  sub edx, eax
0x00540100  lea ecx, [edx + esi + 8]     payload size minus the filler
0x00540104  mov eax, 0xcccccccd
0x00540109  mul ecx
0x0054010B  shr edx, 6                   divide by 0x50
0x0054010E  mov [ebx + 0x1b], dl         solid + 0x1B = the marker count
```

`2^38 / 0xCCCCCCCD` is exactly 80, so the record size is `0x50`. The count is
stored in a byte, so at most 255 records of a chunk are reachable.

One 80-byte record:

| Offset | Type | Field |
| --- | --- | --- |
| `+0x00` | u32 | `bin_hash` of the marker name |
| `+0x04` | u32[3] | zero in 4216 of the 4461 retail markers, uninitialised in the rest |
| `+0x10` | f32[16] | a row-major 4x4 matrix; row 3 is the translation |

The translation is in the same raw slot order as a vertex: longitudinal,
lateral, vertical.

### Retail inventory

Two of the 39 `Cars/*/GEOMETRY.BIN` files are not retail-original.
`Cars/LANCER` and `Cars/PEUGOT` carry `NFS-CarToolkit by nfsu360` in their
`0x00134002` header instead of the retail source path, they are the only two
files without `0x80134008`, and they carry a marker chunk on every mesh.
Both totals are given below because the game loads those files and the
parser must too.

| | All 39 files | The 37 retail-original files |
| --- | ---: | ---: |
| Marker chunks | 2860 | 2092 |
| Markers | 4461 | 4216 |
| Chunks holding no record | 647 | 0 |
| Markers with a non-zero word at `+0x04` | 245 | 0 |

`nfsu_probe part-markers` reproduces every figure in the first column.
Both the 647 empty chunks and the 245 markers with a non-zero reserved word
are entirely inside `Cars/LANCER` and `Cars/PEUGOT`, and they are 100% of
those two files' markers. Retail record counts per chunk are 1, 2, 3, 4, 5,
6, 7, 8, 10, 12 and 18.

The parser therefore keeps the three words at `+0x04..+0x0F` raw and never
requires or interprets them: no traced instruction reads them, and requiring
zero would refuse two files the game accepts.

`Cars/BRAKES`, `Cars/EXHAUST` and `Cars/PLATES` carry no markers at all;
`Cars/WHEELS` carries exactly one `MARKER_BRAKE` per rim mesh.

### Marker names

Every name below was recovered and then confirmed by recomputing `bin_hash`
over it; `nfsu::car_marker_name` holds the same table and
`nfsu_part_placement_tests` re-derives every hash from its name.

The table is what the collector at `0x0056BDB0` accepts, not an inventory of
the data. Retail geometry uses 18 distinct hashes: 17 of these names plus
the unrecovered one below. The seven names `BRAKELIGHT_LEFT`,
`BRAKELIGHT_RIGHT`, `SIDE_MIRROR_LEFT`, `SIDE_MIRROR_RIGHT`,
`LICENSE_PLATE_REAR`, `CENTRE_EXHAUST` and `EXHAUST_RIGHT` occur in no car
file in this installation.

| Hash | Name | Role |
| --- | --- | --- |
| `0x02B52399` | `BRAKELIGHT_RIGHT` | brakelight |
| `0x05F63788` | `SIDE_MIRROR_LEFT` | side mirror |
| `0x0BEA251B` | `RIGHT_SIDE_MIRROR` | side mirror |
| `0x2F96D0FD` | `RIGHT_REAR_WINDOW` | window |
| `0x31A66786` | `LEFT_BRAKELIGHT` | brakelight |
| `0x3DF28F69` | `LEFT_FRONT_WINDOW` | window |
| `0x3F68F920` | `REAR_WINDOW` | window |
| `0x494ED280` | `LICENSE_PLATE_REAR` | licence plate |
| `0x64B0BB1C` | `RIGHT_FRONT_WINDOW` | window |
| `0x6EC7E79F` | `FRONT_WINDOW` | window |
| `0x9DB90133` | `LEFT_HEADLIGHT` | headlight |
| `0xA2A2FC7C` | `CENTRE_BRAKELIGHT` | brakelight |
| `0xBCF8A18B` | `LEFT_EXHAUST` | exhaust |
| `0xBD7CF15E` | `RIGHT_EXHAUST` | exhaust |
| `0xBF700A79` | `RIGHT_BRAKELIGHT` | brakelight |
| `0xC4EC23C8` | `LEFT_SIDE_MIRROR` | side mirror |
| `0xC52BF01B` | `SIDE_MIRROR_RIGHT` | side mirror |
| `0xC805F7E0` | `REAR_LICENSE_PLATE` | licence plate |
| `0xD09091C6` | `RIGHT_HEADLIGHT` | headlight |
| `0xD947F346` | `BRAKELIGHT_LEFT` | brakelight |
| `0xDF066A41` | `CENTRE_EXHAUST` | exhaust |
| `0xE898CFAA` | `LEFT_REAR_WINDOW` | window |
| `0xEA8DFB05` | `MARKER_BRAKE` | wheel brake |
| `0xF21BE91E` | `EXHAUST_RIGHT` | exhaust |

Aside: `bin_hash("WINDOW_LEFT_FRONT")` is `0xE7E4EF49`, which
`docs/CAR_SCENE.md` lists as one of the six material hashes found in no
surveyed texture pack. That is a name match on a 32-bit hash and nothing
more; no marker and no material was traced to the other, so neither
document treats it as an identification.

One hash that occurs in retail data, `0x3AC3FFB1`, has no recovered name. It
appears 76 times across 19 cars, always as a symmetric pair on a `BASE` mesh
at about longitudinal `1.04`, lateral `+-0.22`, vertical `0.70`, which is the
inboard base of the windscreen. A brute force over 79500 candidates built
from the vocabulary the other names use - cores such as `WIPER`, `SEAT`,
`ANTENNA` and `COWL`, the `LEFT`/`RIGHT`/`FRONT`/`REAR`/`CENTRE` qualifiers
in both orders, and the `MARKER_`, `CAR_` and `DUMMY_` prefixes - found no
match. It resolves to no name and no role rather than to a guess.

The collector at `0x0056BDB0..0x0056C0DC` switches on the hash and appends a
node to one of 28 intrusive lists at `CarRenderInfo + 0x62C`. It walks slots
0..29 (`0x0056C0D6`: `cmp eax, 0x1d`), skips a slot whose enable byte at
`RideInfo + 0x6A4 + slot` is zero (`0x0056BD57`), and skips a slot whose LOD
A model has no solid (`0x0056BD79`).

## The exhaust and the licence plate (observed)

Both are drawn from a fixed model and one node per marker.

| | Exhaust | Licence plate |
| --- | --- | --- |
| Model | `CarRenderInfo + 0x140C` | `CarRenderInfo + 0x152C` |
| List head | `CarRenderInfo + 0x67C` | `CarRenderInfo + 0x704` |
| Identity built at | `0x0056F624..0x0056F6C6` | `0x0056F7CE..0x0056F87B` |
| Quarter turn | `0x0056F62E` / `0x0056F6D1` | same pair |
| List walk | `0x0056F711`, empty test `0x0056F71D` | `0x0056F880`, empty test `0x0056F88F` |
| Matrix multiplies | `0x0056F76F`, `0x0056F77B` | `0x0056F901`, `0x0056F90D` |
| Draw | `0x0056F799` | `0x0056F92B` |

Both take the model at LOD A only; neither applies a LOD offset.

The per-instance transform is

```text
placement = quarter_turn * marker.matrix
world     = placement * car_world_matrix
```

with row-major matrices and the row-vector convention `v' = v * M`, which is
what `0x005AC69C` implements.

`0x0040BA40` is called with `edx = 0x4000`. `0x00564B10` multiplies an angle
by the `2 * pi / 65536` at `0x006CC980`, so `0x4000` is a quarter turn.
`0x0040BA40` writes the cosine into elements 0 and 10, `-sin` into element 2
and `+sin` into element 8, so the turn is in the longitudinal-vertical plane,
about the lateral axis. At a quarter turn the sine is exactly 1 and the
cosine exactly 0, so the matrix is exact and needs no trigonometry;
`nfsu::car_marker_quarter_turn` writes those constants directly.

### Why the quarter turn is not optional

The exhaust and plate meshes are authored at the origin, so the marker
matrix alone does not orient them. Applying the retail
`240SX_KIT15_REAR_BUMPER_A` markers to the meshes, in the raw slot frame:

| Part | Marker alone | Quarter turn then marker |
| --- | --- | --- |
| `240SX_KIT00_EXHAUST_A` | lon `-2.024..-1.943`, vert `0.035..0.566` | lon `-1.988..-1.457`, vert `0.000..0.081` |
| `LICENSE_PLATE_STYLE01_A` | vert constant `0.302` (horizontal) | lon constant `-2.032` (vertical) |

With the turn the exhaust is a horizontal pipe 0.53 long running forward
from its tip under the rear left, and the plate is a vertical quad on the
rear face; the host bumper's rear face is at longitudinal `-2.050`. Without
the turn the exhaust stands upright and the plate lies flat. The turn is
therefore load-bearing, not cosmetic.

## Parts drawn at the car's own matrix (observed)

The per-slot draw loop is `0x0056F4B0`, bounded by `cmp esi, 0x1e` at
`0x0056F607`, with the jump table at `0x00570788` and its 16 index bytes at
`0x00570794`: `00 00 02 02 02 02 02 02 02 02 02 02 01 01 01 01`. Slots 3 and
4 take the mirror branch, slots 5..14 the generic branch, and slots 15..18
are skipped by the loop because they have their own code.

In the generic branch `0x0056F557..0x0056F5FE`:

```text
0x0056F582  lea ecx, [ebx + ebx*2]
0x0056F585  lea edi, [edi + ecx*8]     the 24-byte model for this slot and LOD
0x0056F5EB  mov ecx, [esp + 0x3c]      the car's own matrix
0x0056F5FB  push ecx                   passed straight to the draw
0x0056F5FC  push edi
```

Neither the generic branch nor the mirror branch at `0x0056F4D7..0x0056F549`
contains a call to `0x005AC69C` or a marker read. The only per-slot work in
the loop is patching material keys into the solid. So every body part is
drawn with exactly the car's matrix, that is at identity relative to the car.

Measured mesh bounding boxes agree. In the raw slot frame,
`240SX_KIT00_SPOILER_A` spans longitudinal `-1.965..-1.713` and vertical
`0.705..0.756`, sitting on the trunk lid; `240SX_KIT00_FRONT_BUMPER_A` spans
`1.039..2.338`; the mirror pair sits at lateral `+-0.67..0.94`. Each is
already in car space. By contrast `240SX_KIT00_EXHAUST_A`,
`LICENSE_PLATE_STYLE01_A`, `BRAKE_KIT00_A` and `240SX_KIT00_WHEEL_A` are all
authored at the origin, and each of those four is one of the parts that gets
a placement.

## The wheel table in `GLOBALB.BUN` (observed)

### The car type table

Chunk `0x00034600`. The loader is `0x0057C8B0`:

| Address | Instruction | Consequence |
| --- | --- | --- |
| `0x0057C8BA` | `cmp eax, 0x34600` | the chunk is accepted by id alone |
| `0x0057C8C2` | `lea edi, [esi + 0x17]` | `esi` is the chunk header |
| `0x0057C8C5` | `and edi, 0xfffffff0` | records start at `align_up(header + 8, 16)` |
| `0x0057C8C8` | `mov [0x734588], edi` | the array base is kept in that global |
| `0x0057C8D2` | `imul eax, eax, 0xc90` | the record stride is `0xC90` |
| `0x0057C8F1` | `cmp esi, 0x23` / `jl` | exactly 35 records |

The count is a hard-coded `0x23`; it is not derived from the payload size.
The unloader at `0x0057CB70` writes zero to the same global, and
`0x00424C50` is the accessor `base + index * 0xC90`.

In the sample the chunk header is at absolute offset 1678848 with a payload
of 112568 bytes. The payload starts at 1678856, the aligned record array at
1678864, and 35 records of `0xC90` bytes end exactly at the payload end. The
eight bytes in between are all `0x11`, the same filler the geometry chunks
use.

Fields decoded in a record:

| Offset | Type | Field | Evidence |
| --- | --- | --- | --- |
| `+0x000` | char[0x20] | car name | `0x0057D751`, `0x0057D883` |
| `+0x020` | char[0x20] | lookup name | `0x005A96BB`, `0x00504646` |
| `+0x040` | char[0x20] | geometry path | `0x0058B5DD` into the car loader `0x0058C220` |
| `+0x060` | char[0x20] | the same path with a `.LZC` extension | same field pair |
| `+0x0C0` | char[0x20] | brand name | `0x00562B6E`, formatted with `BRAND_NAME_%s` |
| `+0x110` | byte[0x2B0] | setup sub-object | `0x00575960` copies `0xAC` dwords |
| `+0x120` | 4 x 0x30 | the wheel table | below |
| `+0xC54` | u32 | class | `0x0057C8D8`, `0x0056E171`, `0x0058B645` |

The name and the lookup name are equal in all 35 retail records, which
`docs/CAR_SCENE.md` already relies on, so a record where they differ is
refused rather than silently resolved one way. The class is 0 for the 20
racers and 2 for the 15 traffic vehicles.

### The setup sub-object and the per-instance copy

`0x0057F2B0` is a RideInfo method; `[edi]` is the car type index, which
`docs/CAR_SCENE.md` records as RideInfo `+0x00`. At `0x0057F2F6` it takes
`lea ecx, [esi + 0x110]` and calls `0x00575960`, whose `mov ecx, 0xac` /
`rep movsd` at `0x0057596C` copies `0x2B0` bytes from `CarTypeInfo + 0x110`
into `RideInfo + 0x60`. `0x0057F2A7` is a one-instruction accessor returning
`this + 0x60`.

So each car instance owns a copy, and wheel record `k` of that copy is at
`RideInfo + 0x70 + k * 0x30`. The renderer reads the copy, not the file, so
the static table is the source of truth but tuning can move it.

### One 0x30-byte wheel record

| Offset | Type | Field | Evidence |
| --- | --- | --- | --- |
| `+0x00` | f32 | longitudinal | copied at `0x0056944E` |
| `+0x04` | f32 | lateral | copied at `0x0056944B` |
| `+0x08` | f32 | vertical | copied at `0x00569450`; the suspension upgrade adjusts it |
| `+0x0C` | u32 | zero in all 140 retail records | no reader traced |
| `+0x10` | f32 | rolling radius | `0x0056B3F5`, `0x004B17F5` |
| `+0x14` | f32 | read into `CarRenderInfo + 0x530` | `0x0056C283`, used at `0x0056F9C6` |
| `+0x18` | u32 | the record's own index, 0..3 | equals the position in all 140 records |
| `+0x1C` | f32 | read negated into `CarRenderInfo + 0x510` | `0x0056C267` |
| `+0x20` | f32 | read negated into `CarRenderInfo + 0x514` | `0x0056C275` |
| `+0x24` | u32[3] | zero in all 140 retail records | no reader traced |

The stride and count are proven twice. In the code: `0x004B1AAB` does
`add esi, 0x30`, `0x004B1ABE` bounds the loop with `cmp edi, 0x100` after
`add edi, 0x40`, `0x00432569` sets `mov esi, 4` with `add eax, 0x30` at
`0x00432573`, and the constructor reaches the four records at `RideInfo +
0x70`, `+0xA0`, `+0xD0` and `+0x100`. In the data: field `+0x18` holds the
record's own position in all 35 cars, so the array describes its own shape.

`+0x10` is the distance the shadow is offset from the plane of the four
wheel centres (`0x0056B3F5`), and `0x004B17F5` combines it as
`(+0x10) - (+0x08) + 0.01`. Both make it the height of a wheel centre above
the ground, that is a rolling radius. It is **not** the visual wheel mesh's
radius: 240SX stores 0.288 front and 0.3135 rear while
`240SX_KIT00_WHEEL_A` measures 0.311064 in both non-axle axes, and the
aftermarket rims in `Cars/WHEELS` quantize to exact inch diameters (0.29210,
0.30480, 0.31750, 0.33020) that neither stored value matches.

`+0x14`, `+0x1C` and `+0x20` do have readers, listed above, but the
expression the wheel draw builds from them was not reproduced here, so this
project gives none of the three a name beyond its offset.

### The suspension upgrade confirms the axle pairing

`0x00575A10`, reached from `0x00575980`, takes the copied block in `eax` and
a table entry in `edx` at `CarTypeInfo + 0x7F0 + 80 * level`, with the level
gated to 1..3 by `0x00575A15` and `0x00575A1A`:

```text
0x00575A4A  fld [eax+0x18]  ; fadd [edx+0x44] ; fstp [eax+0x18]   wheel 0 +0x08
0x00575A53  fld [eax+0x48]  ; fadd [edx+0x44] ; fstp [eax+0x48]   wheel 1 +0x08
0x00575A5C  fld [eax+0x78]  ; fadd [edx+0x48] ; fstp [eax+0x78]   wheel 2 +0x08
0x00575A67  fld [eax+0xa8]  ; fadd [edx+0x48] ; fstp [eax+0xa8]   wheel 3 +0x08
0x00575A76  fld [eax+0x08]  ; fsub [edx+0x40] ; fstp [eax+0x08]   block +0x08
```

Block offset `0x18 + k * 0x30` is `CarTypeInfo + 0x128 + k * 0x30`, which is
field `+0x08` of wheel `k`. Wheels 0 and 1 share one delta and wheels 2 and
3 share the other, so 0 and 1 are one axle and 2 and 3 are the other. The
same split appears in the steering step at `0x004B1A39`, which applies only
to `k = 0` and `k = 1`. Retail deltas, monotonic for racers and zero for
every traffic vehicle:

| Car | level 1 | level 2 | level 3 |
| --- | --- | --- | --- |
| 240SX | 0.01190 | 0.02345 | 0.03500 |
| GOLF | 0.01105 | 0.02177 | 0.03250 |
| 350Z | 0.00680 | 0.01340 | 0.02000 |
| BUS, FIRETRUCK | 0 | 0 | 0 |

## The cullable car part array (observed)

`0x00704670` holds 14 entries of `0x50` bytes. Each carries its own index at
`+0x00`, a name pointer at `+0x04`, a flag at `+0x08`, a vector count at
`+0x0C` and that many 16-byte direction vectors from `+0x10`.
`0x005693F5..0x0056943A` normalizes each vector whose length differs from
the 1.0 at `0x006CC7BC` by more than the 0.01 at `0x006CC79C`.

| Index | Name | Vectors |
| ---: | --- | ---: |
| 0 | `CULLABLE_CAR_PART_TIRE_FL` | 3 |
| 1 | `CULLABLE_CAR_PART_TIRE_FR` | 3 |
| 2 | `CULLABLE_CAR_PART_TIRE_RR` | 3 |
| 3 | `CULLABLE_CAR_PART_TIRE_RL` | 3 |
| 4 | `CULLABLE_CAR_PART_TIRE_BLURS_LEFT` | 1 |
| 5 | `CULLABLE_CAR_PART_TIRE_BLURS_RIGHT` | 1 |
| 6 | `CULLABLE_CAR_PART_BRAKE_FL` | 2 |
| 7 | `CULLABLE_CAR_PART_BRAKE_FR` | 2 |
| 8 | `CULLABLE_CAR_PART_BRAKE_RR` | 2 |
| 9 | `CULLABLE_CAR_PART_BRAKE_RL` | 2 |
| 10 | `CULLABLE_CAR_PART_SIDE_FRONT` | 1 |
| 11 | `CULLABLE_CAR_PART_SIDE_REAR` | 1 |
| 12 | `CULLABLE_CAR_PART_UNDERNEATH` | 2 |
| 13 | `CULLABLE_CAR_PART_SHADOW` | 1 |

These 14 are the only `CULLABLE_` strings in `speed.exe`. There is no
`ATTACH`, `HUB`, `AXLE` or `PIVOT` token anywhere in the file, and every
`MARKER` string other than `MARKER_BRAKE` is a front-end or HUD icon name
such as `MARKER_GOLD` or `MARKER_BOUNDARY`.

The CarRenderInfo constructor clears a matching array:

```text
0x0056A2C3  lea edi, [esi + 0x1c00]      esi is the CarRenderInfo
0x0056A2CC  mov ecx, 0x70
0x0056A2D1  rep stosd                    0x70 dwords = 0x1C0 bytes
```

`0x1C0 / 14 = 0x20`, one entry per cullable part. The same constructor
stores the RideInfo at CarRenderInfo `+0x04` (`0x0056A2D6`) and the
CarTypeInfo record at `+0x08` (`0x0056A364`).

The installer `0x005693D0` takes the index in `eax`, the array base in `ecx`
and a source position on the stack:

```text
0x005693D5  lea esi, [eax + eax*4]
0x005693D8  shl eax, 5                   index * 0x20, the entry
0x005693DB  shl esi, 4                   index * 0x50, the table entry
0x005693DE  add eax, ecx
0x005693E0  add esi, 0x704670
...
0x0056943C  mov eax, [ebp + 8]           the source position
0x0056943F  fld [eax + 4]
0x00569443  fld [eax + 8]
0x00569446  mov eax, [eax]
0x0056944B  fstp [ebx + 4]
0x0056944E  mov [ebx], eax
0x00569450  fstp [ebx + 8]
```

The three floats are copied verbatim: no rotation, scale or offset is
applied on the way into this array.

### What each index is placed from

With `[ebx + 0x0C]` the RideInfo, `edi` = `RideInfo + 0x70`, `esi` =
`RideInfo + 0xA0` and the 0.5 at `0x006CC7DC`:

| Address | Index | Source |
| --- | ---: | --- |
| `0x0056B18D` | 0 | wheel 0 |
| `0x0056B19B` | 1 | wheel 1 |
| `0x0056B1B2` | 2 | wheel 2 |
| `0x0056B1C8` | 3 | wheel 3 |
| `0x0056B1D8` | 4 | (wheel 3 + wheel 0) * 0.5 |
| `0x0056B228` | 5 | (wheel 2 + wheel 1) * 0.5 |
| `0x0056B27B` | 6 | wheel 0 |
| `0x0056B289` | 7 | wheel 1 |
| `0x0056B294` | 8 | wheel 2 |
| `0x0056B2AA` | 9 | wheel 3 |
| `0x0056B2C0` | 10 | wheel 0 |
| `0x0056B2CE` | 11 | wheel 2 |
| `0x0056B2E4` | 12 | (index 5 + index 4) * 0.5 |
| `0x0056B33C` | 13 | index 12 offset along the wheel-plane normal |

Index 12 is therefore the centroid of the four wheel positions.

Index 13, the shadow, with `a` = wheel 0 - wheel 1 and `b` = wheel 1 -
wheel 2:

```text
0x0056B36A..0x0056B392   n = a x b, component by component
0x0056B397..0x0056B3AD   len = sqrt(n . n)
0x0056B3AF               fcom [0x6cc7a4] = 0.01
  len  > 0.01            0x0056B3BC scales n by 1.0 / len
  len <= 0.01            0x0056B3E2 clears n.z and loads 1.0 and 0.01 instead
0x0056B3F5               mov edx, [ebx+0xc] ; mov eax, [edx + 0x80]
0x0056B401..0x0056B43A   index 13 = index 12 + n * that value
```

`RideInfo + 0x80` is block offset `0x20`, which is wheel 0's field `+0x10`.

The wheel draw reads byte `+0x18` of the four wheel entries of this array
(`0x0056FCA6`, `0x0056FCAD`, `0x0056FCB4`, `0x0056FCC9`, at `0x1C18`,
`0x1C38`, `0x1C58` and `0x1C78`), and the brake draw reads the same byte of
its four (`0x1CD8`, `0x1CF8`, `0x1D18`, `0x1D38`). So the array is a
visibility record keyed by position; the drawn wheel matrix comes from
elsewhere, below.

## The drawn wheel and brake matrices (observed, partly)

The four wheel matrices and the four brake matrices reach
`CarRenderInfo::Render` as parameters `ebp+0x18` and `ebp+0x1C`. The caller
`0x00570950` copies them from render-node `+0x790 + k * 0x40` and
`+0x890 + k * 0x40` at `0x00570A77..0x00570B02`, with the element size
`0x40` and count 4 given literally to `0x004010A0`.

Those node matrices are rebuilt every frame by the function at `0x004B175D`.
Its loop `0x004B1950..0x004B1AE9` runs exactly four times
(`add edi, 0x40` against `cmp edi, 0x100`) over a source of stride `0x30`
(`add esi, 0x30` at `0x004B1AAB`), which is the `RideInfo + 0x70` wheel
array. Per wheel it writes row 3 of both the wheel and the brake matrix from
the record's `+0x00`, `+0x04` and `+0x08` (`0x004B1A87..0x004B1AC4`), turns
the wheel matrix by the spin angle at `car + 0x108` (`0x004B1A31`), and for
`k = 0` and `k = 1` only turns both by the steering angle
(`0x004B1A39..0x004B1A7E`).

Inside `Render` the lateral component of row 3 is then discarded and
recomputed:

```text
0x0056FBA5  mov dword ptr [edx + edi], 0    wheel matrix element 13 := 0
0x0056FBB3  mov dword ptr [edx], 0          brake matrix element 13 := 0
```

and rebuilt at `0x0056F945..0x0056FBCF` from `CarRenderInfo + 0x530`
(wheel record `+0x14`), one of `+0x510` / `+0x514` (wheel record `+0x1C` or
`+0x20`, selected at `0x0056F980` by a RideInfo field), the constants
`0.184` at `0x006F1CCC` and `0.5` at `0x006F1CC8`, and `CarRenderInfo +
0x540`, which `0x0056B8B0..0x0056B90D` reads from element 13 of the wheel
mesh's own `MARKER_BRAKE` marker.

Wheels 0 and 3 are additionally turned by a half turn built at `0x0056FCE3`
and `0x0056FD87`, and brakes 0 and 3 by the diagonal
`(1, -1, 1, 1)` at `0x005703A3..0x00570439`, which is a reflection rather
than a rotation.

**This project does not reproduce that lateral expression.** The operand
selection and the sign rule were traced, but the exact floating-point
stack order was not verified, so `nfsu::part_placement` exposes the wheel
records and the culling placements and stops there. Anything that needs the
drawn wheel matrix must trace `0x0056F945..0x0056FBCF` first.

## The coordinate frame (observed, then inferred)

Each sign is fixed by an observation, not by assumption.

- **Lateral is positive to the left.** Index 4 is named
  `CULLABLE_CAR_PART_TIRE_BLURS_LEFT` and is built from wheels 3 and 0;
  index 5 is `..._RIGHT` and is built from wheels 1 and 2. In the retail
  240SX record wheels 0 and 3 carry `+0.73` and wheels 1 and 2 carry
  `-0.73`. The names `TIRE_FL`, `TIRE_FR`, `TIRE_RR` and `TIRE_RL` agree,
  and so do the marker names: the `LEFT_SIDE_MIRROR` marker sits at lateral
  `+0.956` and `RIGHT_SIDE_MIRROR` at `-0.958`.
- **Longitudinal is positive forward.** Wheels 0 and 1 are the front pair by
  name and carry the larger value (`+1.39` against `-1.04878` for the
  240SX). `LEFT_HEADLIGHT` markers sit at longitudinal `+1.91` and
  `LEFT_BRAKELIGHT` at `-1.99`.
- **Vertical is positive up.** For the 240SX, `a` = `(0, +1.46, 0)` and `b`
  = `(+2.4388, 0, 0)`, so `a x b` = `(0, 0, -3.5606)` and the normalized
  normal is `(0, 0, -1)`. The shadow is placed one rolling radius along it,
  so it ends up below the wheel centres.

Inferred from those: the wheel table and the marker matrices use the same
right-handed frame the geometry file uses in its raw slot order
`(px, pz, py)`, that is +X forward, +Y left, +Z up. `docs/FORMATS.md`
establishes the geometry side of that independently.

## Fallback rules (observed)

| Condition | Address | Result |
| --- | --- | --- |
| No marker of the wanted name on any installed part | `0x0056F71D` exhaust, `0x0056F88F` plate | the list is empty and the part is never drawn; there is no default position |
| The part's slot is disabled | `0x0056BD57` collector, `0x0056F55A` draw | the slot contributes no markers and is not drawn |
| The slot's LOD A model has no solid | `0x0056BD79` | the slot contributes no markers |
| A model has no solid at draw time | `0x0040C919` | the draw returns at once |
| `MARKER_BRAKE` absent from the wheel mesh | `0x0056B8B0` loads the 0.0 at `0x006CC7A4` and `0x0056B90D` stores it unconditionally | `CarRenderInfo + 0x540` becomes 0.0, which selects a different branch at `0x0056FB71` |
| `CarRenderInfo + 0x08` is null | `0x0056C250` | `+0x510..+0x52F` is zeroed; `+0x530..+0x53F` keeps its previous contents |
| The wheel plane is degenerate | `0x0056B3E2` | the normal is replaced instead of normalized |
| The scratch matrix pool is exhausted | `0x0056FC28`, `0x0056F8D0`, `0x00570280` | the whole draw block is skipped |

## Retail validation

`nfsu_probe car-placement Global/GLOBALB.BUN 240SX` prints, for record 18 at
absolute offset 1736752:

| Wheel | Offset | Longitudinal | Lateral | Vertical | `+0x10` | `+0x14` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 FL | 1737040 | 1.39000 | 0.73000 | 0.08000 | 0.28800 | 0.19500 |
| 1 FR | 1737088 | 1.39000 | -0.73000 | 0.08000 | 0.28800 | 0.19500 |
| 2 RR | 1737136 | -1.04878 | -0.73000 | 0.08000 | 0.31350 | 0.20500 |
| 3 RL | 1737184 | -1.04878 | 0.73000 | 0.08000 | 0.31350 | 0.20500 |

and the derived culling placements:

| Index | Part | Longitudinal | Lateral | Vertical |
| ---: | --- | ---: | ---: | ---: |
| 4 | `TIRE_BLURS_LEFT` | 0.17061 | 0.73000 | 0.08000 |
| 5 | `TIRE_BLURS_RIGHT` | 0.17061 | -0.73000 | 0.08000 |
| 12 | `UNDERNEATH` | 0.17061 | 0.00000 | 0.08000 |
| 13 | `SHADOW` | 0.17061 | 0.00000 | -0.20800 |

The wheelbase is `1.39 - (-1.04878) = 2.43878` and the track is `1.46`.
The command runs over all 35 records without a car type; no retail car type
produces a degenerate wheel plane.

A structurally different vehicle, `BUS` (record 30, class 2, wheelbase 8.7):

| Wheel | Longitudinal | Lateral | Vertical | `+0x10` |
| ---: | ---: | ---: | ---: | ---: |
| 0 FL | 4.20000 | 1.21100 | 0.10000 | 0.52000 |
| 1 FR | 4.20000 | -1.21100 | 0.10000 | 0.52000 |
| 2 RR | -4.50000 | -1.19200 | 0.10000 | 0.52000 |
| 3 RL | -4.50000 | 1.19200 | 0.10000 | 0.52000 |

with `SHADOW` at `(-0.15000, 0.00000, -0.42000)`. The BUS rear axle is 0.019
narrower than its front, so the laterals mirror within an axle but not
across axles; that holds in all 35 records.

Independently, the raw table positions land in the wheel arches. Taking the
outer skin of `240SX_KIT00_BODY_A` (`|lateral| > 0.70`, 556 vertices) and
its radial profile in the longitudinal-vertical plane, the centres
`(1.39, 0.08)` and `(-1.04878, 0.08)` each sit in an empty disc of radius
0.34 and 0.36 with a dense ring of arch vertices at 0.35 to 0.40. Adding the
rolling radius to the vertical destroys that, and two control points show no
ring at all. The stored vertical is the wheel-centre height directly.

`nfsu_probe part-markers` over all 39 files reports 2860 marker chunks and
4461 markers with 16 to 18 distinct hashes per car, 76 of which carry the
one unrecovered name. For 240SX it reports 60 chunks, 168 markers, 16
distinct hashes and no unrecovered name.

## Parser rules

`nfsu::parse_car_type_table` is fail-closed. A table that reports any issue
is not `valid`, and `find_car_type` and the derivation refuse it.

1. The file must be a well-formed sequence of top-level chunks with exactly
   one `0x00034600` among them.
2. The record array starts at `align_up(payload, 16)`, computed from the
   absolute file offset as `0x0057C8C2` does. Every byte between the payload
   start and that offset must be `0x11`.
3. The payload must hold all 35 records of `0xC90` bytes after the filler.
4. Each of the five string fields must be NUL-terminated within its `0x20`
   bytes, non-empty and printable ASCII, and the two name fields equal.
5. Each wheel record's stored index must equal its position, its `+0x0C`,
   `+0x24`, `+0x28` and `+0x2C` words must be zero, and all seven of its
   float fields must be finite. All seven have a traced reader.

The marker parser in `nfsu::parse_geometry` accepts at most one
`0x0013401A` per mesh, requires `0x11` filler up to the 16-byte boundary,
requires the remaining payload to be a whole number of 80-byte records,
refuses more records than the game's byte count can address, and refuses a
matrix element that is not finite. A mesh that fails any of these is skipped
with a diagnostic, as every other geometry failure is. The three reserved
words are kept raw and never checked, for the reason in "Retail inventory"
above.

`derive_cullable_part_placements` reproduces the 14 culling placements. It
fails only for a wheel value that is not finite. A degenerate wheel plane is
reported in the shadow entry rather than refused, because the game takes a
fallback branch for it.

## Probes

```text
build/nfsu_probe car-placement X:/Games/NFSU/Global/GLOBALB.BUN
build/nfsu_probe car-placement X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe part-markers X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN
build/nfsu_probe part-markers X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN exhaust
```

Both are read-only and write nothing. `car-placement` prints the table
location, each car type's strings and class, its four wheel records with
their file offsets, and the 14 derived placements with the wheels each one
came from. `part-markers` prints every marker with its resolved name, role,
translation and the local placement the exhaust and plate draw paths build,
plus a per-hash summary; an optional role argument filters the listing.

## Unknown

- The lateral expression the wheel draw builds at
  `0x0056F945..0x0056FBCF`, and therefore the drawn wheel and brake
  matrices. The inputs are identified; the arithmetic is not reproduced.
- Wheel fields `+0x14`, `+0x1C` and `+0x20` beyond the addresses that read
  them. `+0x14` is equal within an axle and the other two mirror the sign of
  the lateral, but no name is claimed for any of them.
- Bytes `+0x0C..+0x1F` of a `0x20`-byte culling entry. `0x005693D0` writes
  only the first three floats and the constructor cleared the rest, yet the
  wheel draw reads byte `+0x18`, so something else writes it; where was not
  traced.
- The meaning and sign convention of the 14 direction vectors at
  `0x00704670`. They are normalized at `0x005693F5` and the left and right
  members of each pair differ only in the sign of their second component,
  but the consumer that dots them with a view direction was not traced.
  Nothing here depends on them.
- The name behind marker hash `0x3AC3FFB1`.
- Marker record bytes `+0x04..+0x0F`. Zero in every retail-original record
  and read by no traced instruction, so whether they are a field at all is
  unknown.
- The rest of the `0x2B0`-byte setup sub-object at `+0x110`. The post-load
  pass `0x00581D30` accumulates global extents from its fields into
  `0x006F0D30..0x006F0D3C`, but nothing there is placement.
- The consumers of the marker lists other than the exhaust and plate ones:
  `CarRenderInfo + 0x694`, `+0x69C`, `+0x6A4`, `+0x6B4`, `+0x6C4`, `+0x6DC`,
  `+0x6E4`, `+0x6EC` and `+0x6F4` are populated by the collector, but what
  reads them was not traced, so this project claims no transform for the
  brakelight, headlight, window or mirror markers.
- Every field of a car type record not listed above.
