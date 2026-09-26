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
0x005400ED  mov ecx, eax
0x005400EF  and ecx, 0xfffffff0          records start at align_up(payload, 16)
0x005400F2  and eax, 0xfffffff0          the same value, kept in eax
0x005400F5  mov [ebx + 0x80], ecx        solid + 0x80 = the marker table
0x005400FB  mov edx, [esi + 4]           the payload size
0x005400FE  sub edx, eax                 eax is aligned, so this is size - aligned
0x00540100  lea ecx, [edx + esi + 8]     payload size minus the filler
0x00540104  mov eax, 0xcccccccd
0x00540109  mul ecx
0x0054010B  shr edx, 6                   divide by 0x50
0x0054010E  mov [ebx + 0x1b], dl         solid + 0x1B = the marker count
```

`0xCCCCCCCD` with `shr edx, 6` is an exact divide by 80, so the record size
is `0x50`. `mov [ebx + 0x1b], dl` truncates the count to a byte, and every
reader sign-extends it again (`movsx` at `0x0056BD89` and `0x0056B8CC`), so
a count above 127 comes back negative and the walk at `0x0056B8DE` computes
a negative end. Only counts up to 127 behave as written. The most any retail
chunk holds is 18.

One 80-byte record:

| Offset | Type | Field |
| --- | --- | --- |
| `+0x00` | u32 | `bin_hash` of the marker name |
| `+0x04` | u32[3] | zero in 4216 of the 4461 markers in the files, uninitialised in the rest |
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

Those are the records present in the files, counted by walking the chunk
tree with no mesh validation at all.

`nfsu_probe part-markers` reports one chunk and two markers fewer, 2859 and
4459 over all 39 files and 2091 and 4214 over the 37 retail-original ones,
because it reports only meshes that validated. The single difference is
`Cars/CIVIC` mesh 286 `CIVIC_KIT13_REAR_BUMPER_A`, whose marker chunk parses
and whose material record 3 then declares an index count of zero. The mesh
is skipped, so its two markers, `RIGHT_EXHAUST` and `REAR_LICENSE_PLATE`,
are withdrawn with the rest of its data; see "Parser rules" below. No other
file changes: across the 39 `Cars/*/GEOMETRY.BIN` files the parser rejects
eight further meshes, one in `Cars/ECLIPSE`, three in `Cars/SUPRA` and four
in `Cars/TIBURON`, and none of the eight has a `0x0013401A` chunk in its
container at all.

Both the 647 empty chunks and the 245 markers with a non-zero reserved word
are entirely inside `Cars/LANCER` and `Cars/PEUGOT`, and they are 100% of
those two files' markers. Retail record counts per chunk are 1, 2, 3, 4, 5,
6, 7, 8, 10, 12 and 18.

The parser therefore keeps the three words at `+0x04..+0x0F` raw and never
requires or interprets them: no traced instruction reads them, and requiring
zero would refuse two files the game accepts.

`Cars/BRAKES`, `Cars/EXHAUST` and `Cars/PLATES` carry no markers at all. In
`Cars/WHEELS`, 984 of the 1476 meshes carry exactly one `MARKER_BRAKE`: the
492 `_A` and the 492 `_B` rim meshes do, and the 492 `_C` meshes do not.

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
in a band that spans longitudinal `0.5609..1.2049`, lateral
`-0.3672..0.3740` and vertical `0.6341..0.7819` over the 19 cars, which puts
it at the inboard base of the windscreen. A brute force over 79500 candidates built
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

with row-major matrices and the row-vector convention `v' = v * M`.
`0x005AC69C` is a lazily resolved dispatch thunk (`jmp [0x006D4ACC]`, which
starts at the resolver `0x005AC68F`); the x87 body it reaches is
`0x005AC6A2`, and the multiply order was read there.

`0x0040BA40` is called with `edx = 0x4000`. `0x00564B10` multiplies an angle
by the `2 * pi / 65536` at `0x006CC980`, so `0x4000` is a quarter turn.
`0x0040BA40` writes the cosine into elements 0 and 10, `-sin` into element 2
and `+sin` into element 8, so the turn is in the longitudinal-vertical plane,
about the lateral axis. `0x00564B10` is a ninth-order Taylor sine, so at a
quarter turn it returns `1.00000358` rather than exactly 1, while the cosine
does come out exactly `-0.0`. `nfsu::car_marker_quarter_turn` writes the
exact 0 and 1 instead. That 3.6e-06 difference is deliberate. It was the
only place this project knowingly departed from the instruction stream
until issue #161 made the same substitution for the wheels' half turn
("Which way the wheel and brake models face" below).

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
rear face. The host mesh `240SX_KIT15_REAR_BUMPER_A` reaches longitudinal
`-2.067047` overall and `-2.050148` within the band the plate occupies
(`|lateral| < 0.35`, `0.25 < vertical < 0.37`), so the plate at `-2.032`
sits just in front of it. Without the turn the exhaust stands upright and
the plate lies flat. The turn is therefore load-bearing, not cosmetic.

## Parts drawn at the car's own matrix (observed)

The per-slot draw loop is `0x0056F4B0`, bounded by `cmp esi, 0x1e` at
`0x0056F607`, with the jump table at `0x00570788` and its 16 index bytes at
`0x00570794`: `00 00 02 02 02 02 02 02 02 02 02 02 01 01 01 01`. The table
covers slots 3..18 only: `0x0056F4B6` computes `slot - 3` and sends anything
above 15 straight to the generic branch, so slots 0..2 and 19..29 are
generic as well. Within the table, slots 3 and 4 take the mirror branch,
slots 5..14 the generic branch, and slots 15..18 are skipped by the loop
because they have their own code.

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
`240SX_KIT00_WHEEL_A` measures 0.311064 longitudinally and 0.311065
vertically, and the
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
| `0x0056B2E4` | 12 | (placement 5 + placement 4) * 0.5 |
| `0x0056B33C` | 13 | placement 12 offset along the wheel-plane normal |

Index 12 is therefore the centroid of the four wheel positions.

Row 12 is the only one whose sources are placements rather than wheels, and
it is the reason `PartPlacement` carries a `PlacementIndexSpace`: 4 and 5
are valid placement indices and there is no wheel 4 or 5, so the two cannot
share a field without a consumer being able to run off the four-entry wheel
table. `placement_wheel_sources` is the accessor that returns wheel indices
and refuses this row.

Index 13, the shadow, with `a` = wheel 0 - wheel 1 and `b` = wheel 1 -
wheel 2:

```text
0x0056B36A..0x0056B392   n = a x b, component by component
0x0056B397..0x0056B3AD   len = sqrt(n . n)
0x0056B3AF               fcom [0x6cc7a4], which is 0.0
0x0056B3B7               test ah, 0x44 / jnp: the fallback is taken only on
                         C3 set with C2 clear, that is len exactly 0.0
  len != 0.0             0x0056B3BC scales n by 1.0 / len
  len == 0.0             0x0056B3E2 clears the third component, then
                         0x0056B3E9 and 0x0056B3EF push 1.0 and 0.0
0x0056B3F5               mov edx, [ebx+0xc] ; mov eax, [edx + 0x80]
0x0056B401..0x0056B43A   index 13 = index 12 + n * that value

The normalizing path reaches the join at `0x0056B3F5` having stored the
third component and popped it (`0x0056B3DB`), leaving the second on top of
the first, so on the fallback path the 1.0 is the first component and the
0.0 the second. `0x0056B40A` confirms it: it multiplies `st(2)`, the first
component, by the radius, and `0x0056B423` adds that to the centroid's first
component. A degenerate car therefore puts its shadow at
`centroid + (radius, 0, 0)`.

The 0.01 at `0x006CC79C` is a different constant: it is the tolerance the
direction-vector normalizer uses at `0x005693F5`. Reading it as the
wheel-plane threshold would make this project call a plane degenerate that
the game normalizes.
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
than a rotation. The next section traces both, and the multiply order.

**This project does not reproduce that lateral expression.** The operand
selection and the sign rule were traced, but the exact floating-point
stack order was not verified, so `nfsu::part_placement` exposes the wheel
records and the culling placements and stops there. Anything that needs the
drawn wheel matrix must trace `0x0056F945..0x0056FBCF` first.

### Which way the wheel and brake models face (observed, issue #161)

**The half turn.** `0x0056FCEF..0x0056FD7C` write an identity into a local
at `esp + 0x60`; `0x0056FCE3` loads `esi = 0x8000` and `0x0056FD87` calls
`0x0040B930` into a second local, which `0x0056FD9E` multiplies into the
first. `0x0040B930` stores `sin(esi + 0x4000)`, the cosine, into elements
0 and 5 (`0x0040B957`, `0x0040B95B`), `+sin` into element 1 (`0x0040B947`),
`-sin` into element 4 (`0x0040B954`), 1 into elements 10 and 15 and zero
elsewhere. That is a turn in the longitudinal-lateral plane, about the
vertical axis, and `0x8000` is half of a full turn, so it maps
`(longitudinal, lateral, vertical)` to `(-longitudinal, -lateral,
vertical)`, determinant +1. The game's sine `0x00564B10` (read, and replayed
from the constants it loads at `0x006CC7BC`, `0x006CC8A8`, `0x006CC980` and
`0x006CCA40..0x006CCA90`) gives
`-1.0000036` (`0xBF80001E`) for the cosine and `-0.0` for the sine there.
`nfsu::car_wheel_half_turn` writes -1 and 0 exactly: the same deliberate
3.6e-06 departure the quarter turn makes above, and the second place this
project knowingly departs from the instruction stream.

**The brake reflection.** `0x005703B4..0x0057041B` zero twelve elements of
a local at `esp + 0x60`, `0x00570426` stores 1.0 into element 15, and
`0x00570431`, `0x00570435` and `0x00570439` store the floats at
`0x006B5DDC`, `0x006B5DE0` and `0x006B5DE4` - 1.0, -1.0 and 1.0 in the
retail file - into elements 0, 5 and 10. `nfsu::car_brake_reflection` is
that matrix. Its determinant is -1.

**Which copies, in which order.** The wheel draw reads the visibility byte
of culling entries 0..3 (`0x0056FCA6..0x0056FCC9`) and draws each visible
wheel through `0x005AC69C`, `out = a * b` with row vectors:

| Wheel | Row-vector chain | Multiplies |
| ---: | --- | --- |
| 0 | `X0 * H * W0 * Q0 * C` | `0x0056FEBE`, `0x0056FED7`, `0x0056FEF4`, `0x0056FF0B` |
| 1 | `X1 * W1 * Q1 * C` | `0x0056FFB4`, `0x0056FFD1`, `0x0056FFEB` |
| 2 | `X2 * W2 * Q2 * C` | `0x00570081`, `0x0057009E`, `0x005700BB` |
| 3 | `X3 * H * W3 * Q3 * C` | `0x0057014D`, `0x0057016C`, `0x00570189`, `0x005701A6` |

and the brake draw, from culling entries 6..9 (`0x00570239..0x0057024E`):

| Brake | Row-vector chain | Multiplies |
| ---: | --- | --- |
| 0 | `R * B0 * P0 * C` | `0x00570457`, `0x00570474`, `0x00570487` |
| 1 | `B1 * P1 * C` | `0x0057053B`, `0x00570555` |
| 2 | `B2 * P2 * C` | `0x005705FA`, `0x00570617` |
| 3 | `R * B3 * P3 * C` | `0x005706B4`, `0x005706D1`, `0x005706EE` |

`H` is the half turn, `R` the reflection, `W_k` and `B_k` the node wheel
and brake matrices of the parameters `ebp + 0x18` and `ebp + 0x1C`, and `C`
the matrix at `[esp + 0x3C]` that the exhaust and plate draws also
multiply last. `X_k`, `Q_k` and `P_k` are the locals the lateral
expression above writes, at `esp + 0x1E0`, `esp + 0x3E0` and `esp + 0x2E0`
plus `k * 0x40`: identities with element 13, a lateral offset, written, and
in `X_k` element 5, a scale of the lateral axis, as well
(`0x0056FA4B`, `0x0056FA66`, `0x0056FB64`, `0x0056FBAC`). So the half turn
acts on the wheel model in its own frame, after the lateral scale and
before the wheel is spun, steered and positioned, and the reflection acts
on the brake model before its matrix. The index is all that selects them:
nothing in either block reads the sign of a lateral. Wheels 0 and 3 are the
left pair, lateral positive, in all 35 retail car types.

**Why.** Both models are authored for the right-hand side of the car.
`240SX_KIT00_WHEEL_A` spans lateral `0.0018..0.2006`, measured from the
retail mesh. Its material 2, key `0x22719FA9` - the key the wheel draw
rebinds at `0x0056FDD4` from `CarRenderInfo + 0x4D8` - is 319 triangles at
lateral `0.0102..0.0559` within 0.222 of the axle; its material 0, key
`0x78743CA1`, is 160 triangles over the whole width out to radius 0.311;
and its material 1, key `0x010CB64A`, is one flat quad of two triangles at
lateral `0.2006`, 0.617 by 0.621. By place and shape those are the rim,
the tyre and a backing quad behind the rim (inferred). At a right wheel,
lateral -0.73, the rim is outboard and the quad inboard; the half turn
keeps it so at a left wheel. `BRAKE_KIT00_B`, in the same way, is a flat
quad at lateral `-0.0037`, 0.31 square, and 25 triangles at longitudinal
`-0.176..-0.021` and vertical `-0.039..0.172`, behind and above it: a disc
and a caliper by inference. The reflection keeps the caliper behind the
disc on the left, where a half turn would move it in front.

**Culling plays no part in the wheel.** All three wheel groups bind
`240SX_TIRE`, whose texture-info `+85` is 1 and `+86` is 0, so the
per-draw rule of [`MATERIALS.md`](MATERIALS.md) ("Depth writes and
culling") gives the wheel the alpha test and, under the car effect,
`D3DCULL_NONE`: the retail wheel is two-sided whichever way it faces, and
the half turn is what decides which face is outboard. The half turn is a
rotation and keeps the winding anyway. The caliper's texture,
`BRAKE_GLOBAL` (`0x17F9F794`), has `+86` = 1, for which the same rule gives
`D3DCULL_CW`, and the reflection reverses the winding of brakes 0 and 3. No
instruction of the wheel or brake block itself writes a render state;
whether `0x0040C910`, or the `0x00544390` the brake block calls before it
on one branch, compensates for a reflected matrix was not traced. The CPU
reference applies no per-draw cull mode, so none of this shows in its
frames.

**Also in the brake block, not reproduced.** Before each brake draw the
block points the brake model's `+0x10` at `CarRenderInfo + 0xE2C` for
brakes 0 and 3 and at `+0xE4C` for brakes 1 and 2, with `+0x14` = 2
(`0x0057049B`, `0x00570569`, `0x0057062B`, `0x00570702`). `0x0040C98A`
hands a model whose `+0x10` is set to `0x0053F540`, which walks that many
16-byte entries and looks textures up through `0x005461C0`: a per-side
texture substitution for the brake, whose tables were not decoded.

**What the stand-in carries.** With `place_wheels_from_culling_table`, wheel
copy `k` is `car_wheel_half_turn() * translation(p_k)` and brake copy `k` is
`car_brake_reflection() * translation(p_k)` when `car_wheel_turned_by_draw(k)`
holds, that is for `k` = 0 and 3, and `translation(p_k)` alone for 1 and 2,
with `p_k` the culling placement. That is the draw's order with `X_k`,
`Q_k`, `P_k`, the spin and steering in `W_k`, and the texture substitution
left out, so every such copy is still `placement_exact` false. Before issue
#161 no copy carried `H` or `R`: all eight were pure translations,
determinant +1, and the two left wheels showed the inboard side of the
right-hand model outboard. A camera on the car's left then saw each left
wheel's backing quad in front of its rim - with the default material state,
which does not apply the alpha test, as a flat dark square the size of the
wheel, and with `--material-state retail` as a black disc. The frames and
hashes are in [`SWITCH_BUILD.md`](SWITCH_BUILD.md), "The left side of the
car (issue #161)".

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

`nfsu_probe part-markers` over all 39 files reports 2859 marker chunks and
4459 markers on meshes that validated, out of the 2860 and 4461 the files
hold; the one withdrawn chunk is `Cars/CIVIC` mesh 286, as "Retail
inventory" above sets out. 18 distinct hashes occur across the whole
installation; no single file holds more than 17, and the twenty full car
files span 8 to 17. 76 markers carry the one unrecovered name. For 240SX it
reports 60 chunks, 168 markers, 16 distinct hashes and no unrecovered name,
and no mesh of that file is skipped.

## Parser rules

`nfsu::parse_car_type_table` is fail-closed. A table that reports any issue
is not `valid`, and `find_car_type` and the derivation refuse it.

1. The image must be at most `CarTypeLimits::max_file_size`. Both entry
   points apply it: `load_car_type_table` before it reads the file, and
   `parse_car_type_table` before it parses an image the caller already has,
   so lowering the limit bounds an in-memory image as well as a file.
2. The file must be a well-formed sequence of top-level chunks with exactly
   one `0x00034600` among them.
3. The record array starts at `align_up(payload, 16)`, computed from the
   absolute file offset as `0x0057C8C2` does. Every byte between the payload
   start and that offset must be `0x11`.
4. The payload must hold all 35 records of `0xC90` bytes after the filler.
5. Each of the five string fields must be NUL-terminated within its `0x20`
   bytes, non-empty and printable ASCII, and the two name fields equal.
6. Each wheel record's stored index must equal its position, its `+0x0C`,
   `+0x24`, `+0x28` and `+0x2C` words must be zero, and all seven of its
   float fields must be finite. All seven have a traced reader.

The marker parser in `nfsu::parse_geometry` accepts at most one
`0x0013401A` per mesh, requires `0x11` filler up to the 16-byte boundary,
requires the remaining payload to be a whole number of 80-byte records,
refuses more records than the game's byte count can address, and refuses a
matrix element that is not finite. A mesh that fails any of these is skipped
with a diagnostic, as every other geometry failure is. That is a behaviour
change for third-party data: before this slice a malformed `0x0013401A` was
ignored as an unknown chunk and the mesh still parsed. No retail file has a
marker chunk that fails one of these checks; all 2860 pass.

Skipping a mesh withdraws its markers as well. The marker chunk is read
before the counts, the vertex and face buffers and the materials, so a mesh
can reach a later failure with its markers already decoded. `Mesh::markers`
is then emptied, `Mesh::has_marker_chunk` goes back to false and the two
marker offsets back to zero, exactly as the vertices, faces, texture and
material-key hashes and materials are cleared, so an invalid mesh carries no
decoded data of any kind. `nfsu_probe part-markers` also skips a mesh whose
`valid` is false outright and prints how many it skipped. Retail
`Cars/CIVIC` mesh 286 is the one place in the 39 car files where this is
observable; the figures are in "Retail inventory" above. The three reserved
words are kept raw and never checked, for the reason given there.

`derive_cullable_part_placements` reproduces the 14 culling placements. It
fails for a wheel value that is not finite, and for a derived position that
is not finite, which a finite but enormous wheel table can still produce
through the differences and the cross product. A degenerate wheel plane is
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
their file offsets, and the 14 derived placements with the entries each one
came from. Those entries are printed with the table they index, `wheels` or
`placements`, because the underneath placement is built from two earlier
placements and its 5 and 4 are not wheel numbers. `part-markers` prints
every marker of every mesh that validated, with its resolved name, role,
translation and the local placement the exhaust and plate draw paths build,
plus the number of meshes it skipped as invalid and a per-hash summary; an
optional role argument filters the listing.

`nfsu_probe stock-scene` is the consumer of both: it assembles the stock
part set and this placement into the `stock car` profile of
`PreparedCarScene` (`include/nfsu/stock_scene.h`,
[`RENDERER.md`](RENDERER.md)). The exhaust and the licence plate are
placed there from their markers, exactly as above. The wheel and the brake
are not: the profile reports them as `placement unreproduced` and draws
nothing for them, because of the lateral expression below, and the opt-in
that places them at the culling placements marks every instance it
produces as not exact. Since issue #161 the opt-in does carry which way
each model faces: the half turn on wheels 0 and 3 and the reflection on
brakes 0 and 3, in the draw's order ("Which way the wheel and brake models
face" above).

`nfsu_viewer --stock-car NAME` renders that profile, applying each instance
transform ([`RENDERER.md`](RENDERER.md), "Instances"). The distinction
survives into the picture: without `--place-wheels` the frame holds no
wheel at all, and with it the stand-in copies are drawn but counted apart
from the exact ones and named as a stand-in in the report, so nothing in
the output presents a culling placement as the drawn matrix.

## Unknown

- The lateral expression the wheel draw builds at
  `0x0056F945..0x0056FBCF`, and therefore the lateral offsets and scale of
  the drawn wheel and brake matrices (`X_k`, `Q_k` and `P_k` above). The
  inputs are identified; the arithmetic is not reproduced. Which way the
  models face is decoded and reproduced (issue #161); the spin and the
  steering are traced to `0x004B1A31..0x004B1A7E` and not reproduced.
- The brake's per-side texture substitution tables at `CarRenderInfo +
  0xE2C` and `+0xE4C`, and what fills them.
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
