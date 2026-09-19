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
The game itself searches the other table: it binary-searches `0x00134003`
by hash and keeps the loaded mesh pointer in each entry's second word,
while `0x00134004` gives the ranges its loader streams (see
`docs/FORMATS.md` and "LOD models, choice and fallback" below). Both tables
carry the same hashes, and they are ascending in all 39 retail files.
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
all complete groups suggested interpreting A through D as LOD variants. The
executable confirms it: a part record carries one mesh hash per LOD, and the
renderer picks one of the four by pixel size. See "Installed parts" below
for the thresholds and the missing-mesh fallback.

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

## Installed parts (issue #17)

Addresses are `speed.exe` virtual addresses (sample and hash in
`docs/FORMATS.md`, "Car part database"). The record layout of the part
database is in the same section.

### Observed in the executable

| Address | Role |
| --- | --- |
| 0x6f0970 | 120 x {int id, char* name} slot table; id = position |
| 0x57d160 | RideInfo init: clears the 120 part pointers at +0x44C and the counters at +0x62C, sets the 120 enable bytes at +0x6A4 to 1, clears the preview part at +0x71C |
| 0x57c927 | loader of GLOBALB chunk 0x80034602 (part database) |
| 0x57ce10 | part lookup (car type hash, slot, part name hash, previous record, upgrade level) |
| 0x53e400 | binary search that returns the first record of an equal-key run |
| 0x504630 | first record of the car type and slot whose upgrade level (+0x11) equals the argument |
| 0x57d280 | install the level-0 record of one slot; slot 18 falls back to car type `PLATES` (any level) |
| 0x57d340 | stock-part routine: slots 0..119 except 6, 12 and 31..34; slot 16 from car type `BRAKES` at level 0; then slots 20, 21, 22, 23, 24, 25 and 19 by name |
| 0x4f42f0 | `sprintf` into a 256-byte buffer, then `bin_hash`; used for the decal names |
| 0x57ea80 | recompute the enable bytes from the installed parts |
| 0x4c4230, 0x4bca40, 0x4bd0f0 and five more | front-end callers of 0x57d340, each right after 0x57d160 |

- RideInfo +0x0 is the car type index into the CarTypeInfo array
  (GLOBALB 0x00034600, 35 records of 0xC90 bytes). The part lookup uses
  `bin_hash` of CarTypeInfo +0x20; the decal names format CarTypeInfo
  +0x00. The two names are equal in all 35 retail records.
- The stock decal names are `<car>_DECAL_FRONT_WINDOW_WIDE_MEDIUM`,
  `_DECAL_REAR_WINDOW_WIDE_MEDIUM`, `_DECAL_LEFT_DOOR_RECT_MEDIUM`,
  `_DECAL_RIGHT_DOOR_RECT_MEDIUM`, `_DECAL_LEFT_QUARTER_RECT_MEDIUM`,
  `_DECAL_RIGHT_QUARTER_RECT_MEDIUM` and `_DECAL_HOOD_RECT_MEDIUM`
  (pointers at 0x6f88ac..0x6f88ec). These slots are overwritten even when
  no record has the name.
- 0x57ea80 first sets all 120 enable bytes to 1. Then, in slot order, each
  installed part's first attribute list is scanned: an attribute with key
  0xE80A3B62 names a slot by `bin_hash` of its name, and that slot is
  disabled. Within the same pass, at slot 6 without a wide-body part,
  slots 1, 2, 5, 10 and 22..25 are enabled and 6 and 26..29 disabled; at
  slot 8 an enabled hood hides an installed engine (slot 12); at slot 17
  a preview part can hide the wheel.
- Front-end callers such as 0x4c4230 run 0x57d160, set the skin and wheel
  texture hashes to `DUMMY_SKIN1` and `DUMMY_WHEEL1`, run 0x57d340 and then
  install a BASE_PAINT record named by CarTypeInfo +0xC5C. Paint is not
  geometry.

### Observed in retail data

- Key 0xE80A3B62 occurs on 400 records, all HOOD (slot 8) records with
  upgrade level 1 to 4, one attribute each, value `bin_hash("DECAL_HOOD")`.
  Stock hoods lack it, so on a stock car every enable byte stays 1 except
  6 and 26..29.
- The engine slot is never installed by the stock routine, and no racer
  has a level-0 record in slots 6 or 19..29.
- `nfsu_probe stock-parts GLOBALB.BUN 240SX Cars/240SX/GEOMETRY.BIN
  Cars/BRAKES/GEOMETRY.BIN Cars/PLATES/GEOMETRY.BIN` gives 25 installed
  slots:

| Slot | Rule | Record | A | B | C | D |
| --- | --- | ---: | --- | --- | --- | --- |
| 0 BASE | level 0 | 1274 | `240SX_BASE_A` | `_B` | `_C` | none |
| 1 FRONT_BUMPER | level 0 | 1275 | `240SX_KIT00_FRONT_BUMPER_A` | `_B` | `_C` | none |
| 2 REAR_BUMPER | level 0 | 1291 | `240SX_KIT00_REAR_BUMPER_A` | `_B` | `_C` | none |
| 3, 4 side mirrors | level 0 | 1307, 1308 | `240SX_KIT00_{LEFT,RIGHT}_SIDE_MIRROR_A` | `_B` | `_C` | none |
| 5 BODY | level 0 | 1309 | `240SX_KIT00_BODY_A` | `_B` | `_C` | `_D` |
| 7 ROOF, 8 HOOD, 9 TRUNK, 10 SKIRT | level 0 | 1312, 1322, 1343, 1344 | `240SX_KIT00_<SLOT>_A` | `_B` | `_C` | none |
| 11 SPOILER | level 0 | 1360 | `240SX_KIT00_SPOILER_A` | `_B` | `_C` | 0xB5E01D47, absent |
| 13 HEADLIGHT, 14 BRAKELIGHT | level 0 | 1385, 1390 | `240SX_KIT00_<SLOT>_A` | `_B` | `_C` | none |
| 15 EXHAUST | level 0 | 1395 | `240SX_KIT00_EXHAUST_A` | none | none | none |
| 16 BRAKE | BRAKES | 2822 | `BRAKE_KIT00_A` (Cars/BRAKES) | `_B` | none | none |
| 17 WHEEL | level 0 | 1396 | `240SX_KIT00_WHEEL_A` | `_B` | `_C` | none |
| 18 LICENSE_PLATE | PLATES | 3261 | `LICENSE_PLATE_STYLE01_A` (Cars/PLATES) | none | none | none |
| 19..25 decals | by name | 1397, 1399..1403, 1405 | `240SX_DECAL_<SLOT>_<SHAPE>_A` | absent | none | none |
| 119 MISC | level 0 | 1415 | 0xC93AE898 = `bin_hash("240SX_CV")`, absent | none | none | none |

  The table lists what each record names, before the renderer's fallback
  for a missing mesh (see below). Mirror and decal names are stored
  truncated to 27 characters; the directory hashes identify them. Every
  hash in the table that is present resolves to exactly one mesh in
  exactly one of the three files.
- Of the 25 installed slots, 24 can have a model; slot 119 cannot.
- The same command runs for all 20 racers. At LOD A, 23 or 24 of the 24
  slots with a model draw something. Eleven cars draw 23: in ten of them
  (350Z, FOCUS, GOLF, IMPREZA, LANCER, MIATA, NEON, PEUGOT, RSX, S2000)
  the stock spoiler names meshes that are absent at every LOD, and in
  SKYLINE it is slot 9, whose `TRUNK KIT00` record 4168 names
  0xABE49618, 19 and 1A, none of which `Cars/SKYLINE/GEOMETRY.BIN` has.
- For CIVIC, ECLIPSE and SUPRA the command still exits 3, because the
  geometry parser rejects a mesh in those files:
  `CIVIC_KIT13_REAR_BUMPER_A` has a zero-length material range,
  `ECLIPSE_KIT00_SPOILER_D` has 0 vertices, and three SUPRA
  `*_HEADLIGHT_C` meshes have a short vertex payload. Those are
  pre-existing geometry issues, not part selection ones; the stock parts
  of those cars still resolve.

### Name construction

- Observed: geometry is selected by hashes stored in the part records,
  not by names built at runtime. The only runtime-built part names are
  the stock decal names above and the wide-body decal names
  `"%s_WIDE%d_"` + `DECAL_<SIDE>_RECT_MEDIUM` (0x57ed4b), both hashed into
  part name hashes for the lookup.
- Inferred: the tool that wrote the database built the model hashes from
  `<part name>_A` .. `_D`: 9619 of 10150 nonzero model hashes over all
  records equal that extension of +0x08 (`docs/FORMATS.md`).
- Inferred: the key 0xE80A3B62 is `bin_hash("EXCLUDEDECAL")`; that string
  is not in `speed.exe`.

### LOD models, choice and fallback

Observed in the executable:

| Address | Role |
| --- | --- |
| 0x56a130 | CarRenderInfo constructor; the model array starts at +0xE6C and holds 30 slots x 4 LODs x 24 bytes, in slot then LOD order |
| 0x56a95d..0x56aae9 | fills the four models of one installed part |
| 0x540680, 0x540630 | resolve a mesh hash to a loaded solid |
| 0x56dc30 | CarRenderInfo::Render: one LOD index per car, then the per-slot draws |
| 0x545180 | eView::GetPixelSize(position, radius) |
| 0x58b050 | car loader: copies the same four hashes of slots 0..29 into the list of solids to stream |

- A model keeps a mesh hash at +8 and a solid pointer at +0xC. Models
  exist for slots 0..29 only (`cmp eax, 0x1e` at 0x56ab87), so the
  collision volume in slot 119 never gets one.
- Resolution: for every loaded solid list, 0x540630 binary-searches that
  file's 0x00134003 table (8-byte entries, hash first, ascending) and
  returns the entry's second word. That word is the loaded solid pointer:
  it is zero in the file, written when the mesh container is loaded
  (0x540201) and cleared when it is unloaded (0x5403d1). A hash that no
  loaded list has gives null, and a model whose solid is null draws
  nothing (0x40c919 returns at once).
- Fallback, at construction time only: the constructor keeps a fallback
  hash that starts at the record's LOD B hash (0x56a95d). For LOD A, B, C
  and D in turn: a zero hash leaves the model empty; a hash whose solid is
  found becomes the model and the new fallback (0x56aacb); a hash whose
  solid is missing is replaced by the current fallback, which is looked up
  again and may also fail (0x56aa19). So LOD A without a solid shows the
  LOD B mesh, and a later LOD without a solid shows the nearest
  higher-detail mesh that was found. There is no second chance at draw
  time. `nfsu::resolve_part_models` implements exactly this.
- LOD choice, once per car per frame (0x56dc30): the pixel size is
  `eView::GetPixelSize(position, 3.0)`; below 4 the car is not drawn at
  all (0x56dc8a). Starting at a minimum index, the index is raised while
  the size is below the thresholds at 0x6b5dec, `{120.0, 45.0, 20.0,
  0.0}`, and clamped to 3 (0x56e0c6..0x56e0f8). So LOD A is used from 120
  pixels, B from 45, C from 20 and D below that.
  `nfsu::car_lod_for_pixel_size` implements this mapping.
- The minimum index is 0 in the normal in-world pass. With two player
  views the size is first multiplied by 0.7 (0x6b5e00) and the minimum
  becomes 1 (0x56dc6c). The road-reflection pass uses 2 or 3 (0x570e48).
  The `CarLOD` option (settings row 0x700fa4, variable 0x700eb4, applied
  at 0x40a58b) sets the in-world minimum to 1 when it is 0 and to 0 when
  it is 1. The two debug overrides at 0x6f1b30 and 0x6f1b34 are -1 in the
  retail file and nothing writes them. `WorldLOD` is a different setting
  and does not affect cars.
- The same LOD index is used for every slot, with these exceptions in the
  draw loop (0x56f4b0, dispatch tables at 0x570788 and 0x570794):
  slots 0..2, 5..14 and 19..29 are drawn only when bit 0 of their enable
  byte is set (0x56f556); the mirrors in slots 3 and 4 are drawn only at
  LOD A or B, are skipped in the reflection pass and ignore the enable
  byte; and slots 15..18 are drawn by their own code from fixed models:
  exhaust LOD A (+0x140C), brake LOD A in the front end and LOD B in the
  world (+0x146C), wheel at `min(LOD, 2)` (+0x14CC) gated by the slot 17
  enable byte, and the license plate LOD A (+0x152C).
- The loader builds its streaming list from the same four hashes of slots
  0..29 (0x58b085..0x58b0c5), so a hash that no geometry file contains is
  simply never loaded.

Observed for the retail stock 240SX, with
`nfsu_probe stock-parts GLOBALB.BUN 240SX Cars/240SX/GEOMETRY.BIN
Cars/BRAKES/GEOMETRY.BIN Cars/PLATES/GEOMETRY.BIN`:

| LOD | Slots naming a mesh | Drawn | By fallback | Nothing drawn |
| --- | ---: | ---: | ---: | ---: |
| A | 24 | 24 | 0 | 0 |
| B | 22 | 22 | 7 | 2 |
| C | 14 | 14 | 0 | 10 |
| D | 2 | 2 | 1 | 22 |

The seven LOD B fallbacks are the decal slots, whose B mesh is absent, so
they keep drawing their A mesh. At LOD D only the body and the spoiler
name a mesh, and the spoiler's D hash 0xB5E01D47 is absent, so it draws
`240SX_KIT00_SPOILER_C`. The exhaust and the license plate name no B mesh
and disappear from LOD B on. The same command runs for all 20 racers; in
ten of them the stock spoiler names meshes that the car file does not
contain at any LOD, so a stock car there has no spoiler at all.

### Still open after this slice

- The pixel size itself: `[view+0xC]` in 0x545180 was not traced to a
  writer, so "pixels" is the unit by inference.
- The LOD 3 to 2 clamp at 0x56e103 depends on a car field (+0x3CC) and a
  cullable-part flag that were not decoded.
- Whether a car's solids are always loaded before its CarRenderInfo is
  built. If they are not, the fallback hashes are latched, because the
  constructor overwrites the model's hash.
- Wheels, exhausts and plates are placed at markers, and wheels are drawn
  four times; none of that placement is decoded, so a stock part set is
  not yet a renderable stock car.

## Still unknown

- The rest of the runtime car state: everything in RideInfo outside the
  part table at +0x44C, the counters at +0x62C and the enable bytes at
  +0x6A4.
- Where each part is placed: markers, the four wheel instances, exhaust
  and plate positions, and the damage states the draw code reads.
- The exact per-pixel blend equations, palette-channel interpretation and
  mip-generation rules used when composing `DUMMY_SKIN1`.
- The rendering semantics of material flags and material/effect objects,
  including environment maps, decals, blending, and alpha behavior. The
  former "secondary texture slot" is now verified as a separate 0x00134013
  material-key slot; see `docs/FORMATS.md`.
- The identities of six hashes absent from the surveyed packs.
- The 24-byte track vertex field layout.

A renderer may now build the stock part set for a car and the mesh list it
uses at a given LOD (`nfsu_probe stock-parts`, `nfsu::select_stock_parts`).
That is a decoded part and mesh selection, not a stock car: placement,
paint, skin, vinyls and decal textures are still missing, so neither viewer
profile may be labelled the stock car.
