# Car scene retail evidence

This note records read-only observations from the retail PC installation used
for acceptance tests. It does not contain game data. Values in the observed
sections come from bounded chunk walks, existing parser reports, and byte-range
checks. Interpretations that still need runtime evidence are kept separate.

## 240SX geometry directory

`Cars/240SX/GEOMETRY.BIN` contains two directory tables before the mesh
containers:

| Chunk | Header offset | Payload offset | Payload size | Record layout |
|---|---:|---:|---:|---|
| `0x00134003` | `0xA0` | `0xA8` | `0xC80` | 400 x 8-byte `(mesh_hash, 0)` |
| `0x00134004` | `0xD28` | `0xD30` | `0x1F40` | 400 x 20-byte `(mesh_hash, absolute_offset, total_size, 0, 0)` |

The two tables contain the same 400 hashes in the same order. Every offset and
size in `0x00134004` exactly identifies one `0x80134010` mesh container. The
stored mesh name hashes to the directory value for all 376 names that do not
fill the name field. The other 24 names occupy all 27 stored characters and are
truncated, so their directory hashes retain identity that the stored text no
longer contains.

This makes `0x00134004` the byte-backed lookup table for selecting a mesh by
hash. It does not, by itself, say which mesh is active for a particular car.
The geometry parser now accepts the directory only when both tables agree,
their reserved words are zero, and every stored range exactly covers one
direct mesh container. `nfsu_probe meshes` reports the verified hash and
range, and `nfsu_viewer --mesh-hash HEX` uses only this verified metadata.

## LOD evidence

Suffix grouping yields 120 `_A`, 117 `_B`, 116 `_C`, and 23 `_D` meshes
across 120 base names. Exactly 116 bases have a complete A/B/C sequence;
23 of those also have D. Triangle counts decrease monotonically in all 116
complete groups. Four bases are incomplete: `240SX_KIT00_ENGINE` has A and B,
while `240SX_KIT00_EXHAUST`, `240SX_STYLE01_ENGINE`, and
`240SX_STYLE02_ENGINE` have only A.

One complete example is:

| Mesh | Vertices | Triangles | Container offset |
|---|---:|---:|---:|
| `240SX_STYLE15_SPOILER_A` | 420 | 368 | 11392 |
| `240SX_STYLE15_SPOILER_B` | 231 | 172 | 29312 |
| `240SX_STYLE15_SPOILER_C` | 126 | 90 | 39168 |
| `240SX_STYLE15_SPOILER_D` | 28 | 16 | 44800 |

The monotonic sequence `368 -> 172 -> 90 -> 16` and the same pattern across
all complete groups strongly support interpreting A through D as LOD variants.
They do not, by themselves, prove that interpretation or reveal the distance
thresholds or active installed part.

## External and runtime material hashes

The 400 meshes contain 1242 material groups. The car-local `TEXTURES.BIN`
resolves 174 groups. The other 1068 groups use the following 25 hashes:

| Hash | Groups | Identification | Classification |
|---|---:|---|---|
| `0x0AB88F5D` | 1 | Unknown | Not found in surveyed packs |
| `0x1B0763A0` | 3 | Unknown | Not found in surveyed packs |
| `0x427B4565` | 30 | `INTERCOOLER` | `GLOBALB.BUN` raw TPK |
| `0x4CDEBFCA` | 6 | Unknown | Not found in surveyed packs |
| `0x5799E60B` | 3 | `DRIVER` | `GLOBALB.BUN` raw TPK |
| `0x5C920FB0` | 3 | `DUMMY_TIRE_4LOD` | `GLOBALB.BUN` raw TPK |
| `0x60F8B13C` | 1 | Unknown | Not found in surveyed packs |
| `0x69C032B9` | 42 | `GRILL_01` | `GLOBALB.BUN` raw TPK |
| `0x69C032BA` | 17 | `GRILL_02` | `GLOBALB.BUN` raw TPK |
| `0x721AFF7C` | 87 | `CARBONFIBRE` | `GLOBALB.BUN` raw TPK |
| `0x77A20245` | 637 | `240SX_SKIN1` | Formatted static fallback; active target is `DUMMY_SKIN1` |
| `0x7B220DDF` | 12 | Unknown | Not found in surveyed packs |
| `0x910E6654` | 18 | `DUMMY_DECAL1` | Name-based runtime decal inference |
| `0x910E6655` | 10 | `DUMMY_DECAL2` | Name-based runtime decal inference |
| `0x910E6656` | 8 | `DUMMY_DECAL3` | Name-based runtime decal inference |
| `0x910E6657` | 6 | `DUMMY_DECAL4` | Name-based runtime decal inference |
| `0x910E6658` | 5 | `DUMMY_DECAL5` | Name-based runtime decal inference |
| `0x910E6659` | 5 | `DUMMY_DECAL6` | Name-based runtime decal inference |
| `0x910E665A` | 1 | `DUMMY_DECAL7` | Name-based runtime decal inference |
| `0x910E665B` | 1 | `DUMMY_DECAL8` | Name-based runtime decal inference |
| `0xA6754656` | 1 | `350Z_SKIN1` | Cross-car name; authoring-leak inference |
| `0xBD1F0E0A` | 156 | `CARBOTTOM` | `GLOBALB.BUN` raw TPK |
| `0xD195BE56` | 6 | `REAR_DEFROSTER` | `GLOBALB.BUN` raw TPK |
| `0xDDA11455` | 3 | `GLOBAL_SKIN1` | Name-based runtime/global inference |
| `0xE7E4EF49` | 6 | Unknown | Not found in surveyed packs |

The eight `GLOBALB.BUN` matches cover 344 material groups and use formats the
project already decodes: five DXT1 textures and three DXT3 textures. Ordered
resolution with the car-local pack first is therefore expected to raise the
resolved count from 174 to 518 without implementing runtime body composition.

`0xA6754656` occurs once in `240SX_KIT12_FRONT_BUMPER_C`. Its name identifies a
350Z skin, so it is most likely an authoring leak. The occurrence is observed;
the explanation is an inference.

## Texture-pack coverage

The installation contains 116 top-level `0xB3300000` BIN packs with 20468
records and 20331 unique hashes:

| Category | Packs | Records |
|---|---:|---:|
| Car-local `TEXTURES.BIN` | 39 | 1820 |
| Car `VINYLS.BIN` | 39 | 17522 |
| Shared `Cars/TEXTURES.BIN` | 1 | 353 |
| Track TEX packs | 31 | 170 |
| Frontend | 3 | 405 |
| Global | 2 | 190 |
| Other | 1 | 8 |

The current parser fully accepts 61 of these packs. Thirty-two raw packs lack
the compressed-blob table `0x33310003`: all 31 track TEX packs and one frontend
pack. Another 23 packs use paletted D3D format 41; these are 20 vinyl packs and
three car packs.

Bounded BUN walks also find embedded texture packs:

- `GLOBALA.BUN`: one pack with 4 textures.
- `GLOBALB.BUN`: packs at offsets 23424 and 921216 with 75 and 8 textures.
- `INGAMEB.BUN`: one pack at offset 34176 with 166 textures.

For the raw layout, the `0x33320002` payload is padded to an absolute 128-byte
boundary. Texture-info field `+48` addresses the aligned raw payload. Sorting
records by that value produces contiguous aligned ranges, and the last offset
plus `data_size` exactly reaches the payload end. Field `+52` does not have the
same next-offset meaning as it does in the compressed-pack path. Reserved
prefixes correlate with P8 palettes: 16 P8 records reserve 16384 bytes and one
P8 record reserves 1024 bytes.

## Body-skin composition inputs

`bin_hash("240SX_SKIN1")` is 0x77A20245. That hash is referenced by 637
geometry material groups, but no static texture record with that hash exists
in the surveyed installation. The literal name is also absent. Executable
evidence below identifies it as a formatted static fallback name, not as the
writable target of the customization compositor.

The 240SX car-local pack (SHA-256
`725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA`)
contains six composition-adjacent A8R8G8B8 inputs. Each is 512x512 with 7
mip levels:

| Hash | Stored name | Info offset | Blob offset + size |
| --- | --- | ---: | ---: |
| `0x3980CD22` | `240SX_DOORLINE_KIT` | 6240 | 75966 + 72925 |
| `0x580576DB` | `240SX_DOORLINE` | 8596 | 206612 + 68192 |
| `0x699B6846` | `240SX_DOORLINE_MASK` | 9588 | 285115 + 28988 |
| `0x9D21E7D1` | `240SX_DOORLINE_WIDEBODY` | 11572 | 365908 + 68194 |
| `0xB13AC2ED` | `240SX_DOORLINE_KIT_MASK` | 12812 | 486812 + 29845 |
| `0xBFD2E2FC` | `240SX_DOORLINE_WIDEBODY_MASK` (stored name truncated) | 13432 | 532628 + 26747 |

`Cars/240SX/VINYLS.BIN` contains 436 P8 base vinyls and exactly 436 P8
masks. Every base has one hash-proven `<base>_MASK` partner. Codes 0x80 and
0x81 use at most 16 and 64 palette entries respectively; mask code 0x08 can
use all 256. The parser now extracts the seven-level index chain and its
separate 1024-byte palette for all 872 records. See `docs/FORMATS.md` for
the exact JDLZ layout and retail hash.

`GLOBALB.BUN` also exposes customization names `VINYL_L1_COLOR01..10`,
`VINYL_L2_COLOR01..20`, `VINYL_L3_COLOR01..30` and `CUSTOMPAINT_*` in its
0x00034606 data, plus top-level `CUSTOMPAINT_1..20` and `SKIN` material
records. These are observed selection/control inputs, not texture records.
Their values and their mapping to palette entries remain unknown.
The 0x00135200 `CUSTOMPAINT_1..20` records have headers at 2184..5376 and
names at 2212..5404; `SKIN` has header 17136 and name 17164. The 0x00034606
payload begins at 1797568; its L3, L2 and L1 color-name ranges are
1807225..1807718, 1807735..1808058 and 1808075..1808228 respectively.

The evidence therefore supports these source roles without yet supporting a
blend formula:

1. Car-local `TEXTURES.BIN` supplies the selected doorline/mask variants.
2. Car-local `VINYLS.BIN` supplies a selected indexed vinyl and its paired
   mask.
3. Runtime customization state supplies base/special paints, four vinyl
   layers and optional four-slot color remaps.
4. Active customization writes a dummy runtime target selected through
   `RideInfo+0x444`; when that override is zero the render path falls back to
   the formatted `%s_SKIN1` hash.

Exact palette recoloring, mask arithmetic, alpha rules, per-pixel blend
equations and mip generation for the final skin are still unknown. The
project must not claim a faithful body compositor until those are recovered.

### Executable composition path

The reference executable is `speed.exe`, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`,
PE32 with image base 0x00400000. The body compositor is at VA 0x005A9CE0
(file offset 0x001A9CE0), with a direct call at 0x0058BE57.

The render-info setup formats `%s_SKIN1`, `%s_SKIN1B` and `GLOBAL_SKIN1` at
0x00580B2B, 0x00580B6E and 0x00580BA6. At 0x00580C37..0x00580C67 it chooses
`RideInfo+0x444` when non-zero, otherwise the `%s_SKIN1` hash. Initialization
at 0x004C4230 writes `bin_hash("DUMMY_SKIN1") = 0x530B82B0` to +0x444.
The compositor reads that target hash, finds the writable texture and exits
without work if either the hash or texture is absent.

The same function requires the installed `BASE_PAINT` part. It reads `RED`,
`GREEN`, `BLUE` and `GLOSS` attributes and packs them as 0xGGBBGGRR, which is
`RED, GREEN, BLUE, GLOSS` in little-endian memory. `PAINT_ENGINE`,
`PAINT_SPOILER`, `PAINT_BRAKE` and `PAINT_EXHAUST` provide four optional
special colors and fall back to the base color. `PAINT_RIM` is handled by a
separate wheel compositor at 0x005AA800.

The body function builds a compact list in this order, omitting absent or
unresolved entries:

1. Optional hood/spoiler vinyl.
2. `VINYL_LAYER0`.
3. `VINYL_LAYER1`.
4. `VINYL_LAYER2`.
5. `VINYL_LAYER3`.
6. Doorline.

Helper 0x005A9B50 reads each installed vinyl part's `TEXTURE` attribute and
hashes `<car-name>_<TEXTURE-value>`. `NUMCOLOURS` is mandatory. A non-zero
`REMAP` enables up to four `VINYL_COLOUR<n>_<slot>` values. Missing slots use
the byte-lane sentinels 0x000000FF, 0x0000FF00, 0x00FF0000 and 0xFF000000.
The doorline helper at 0x005A96B0 selects `_DOORLINE_WIDEBODY`,
`_DOORLINE_KIT` or `_DOORLINE` from the installed wide-body and body-kit
parts.

For every non-zero source hash, the composer continues the same multiply-by-
33 hash with `_MASK`, looks up both source and mask, and skips the layer if
either is absent. This directly explains the one-to-one base/mask inventory
in `VINYLS.BIN` and the three car-local doorline pairs.

The lookup at 0x005461C0 walks a global texture-bucket list and moves a hit
to the front. Car `TEXTURES.BIN` is requested before `VINYLS.BIN`, but lookup
priority becomes stateful/MRU after hits; it is not a permanent two-file
priority rule. The executable evidence establishes target selection, inputs,
layer order and paired-mask lookup. It does not yet establish the exact
per-pixel blend equations or palette-channel meaning, so no compositor is
implemented from guesses.

## Track geometry lead

The large `STREAML1R*.BUN` files form valid bounded chunk trees. Their meshes
reach the car geometry parser but fail its fixed 36-byte vertex-layout check.
Sampled payload arithmetic consistently gives a 24-byte track vertex stride.
For one mesh, `(5560 - 88) / 228 = 24`, where 88 bytes are the aligned prefix
and 228 is the declared vertex count.

The field meanings inside that 24-byte record are not established here. Track
vertex decoding must remain separate from car-vertex decoding until fixtures
and retail samples prove those fields.

## Supported inference

Executable strings list installed-part slots such as body, bumpers, skirt,
hood, roof, spoiler, lights, engine, wheel, exhaust, decals, and wide body.
They also contain part-name suffix fragments. Combined with the verified
`0x00134004` hash directory and LOD suffixes, this supports the following model:

1. Read the installed part choice from runtime car state.
2. Construct the part and requested LOD name.
3. Hash the name.
4. Look up the mesh through `0x00134004`.

This is a design lead, not a decoded stock-car selector. The runtime `RideInfo`
layout, default installed parts, LOD thresholds, and missing-part fallback rules
remain unknown.

## Still unknown

- The exact runtime car-state and installed-part data layout.
- The stock 240SX part selection and LOD distance thresholds.
- The exact per-pixel blend equations, palette-channel interpretation and
  mip-generation rules used when composing `DUMMY_SKIN1`.
- The rendering semantics of material flags and material/effect objects,
  including environment maps, decals, blending, and alpha behavior. The
  former "secondary texture slot" is now verified as a separate 0x00134013
  material-key slot; see `docs/FORMATS.md`.
- The identities of six hashes absent from the surveyed packs.
- The 24-byte track vertex field layout.

Until these are decoded, a renderer may expose an explicit all-meshes or
user-specified selection, but it must not label either result as the stock car.
