# Explicit installed parts

Issue #229 adds an explicit per-slot selection for a car scene. This is a
caller-supplied viewer input, not a recovered garage, career or save format.
The stock selection remains the default. No retail or derived asset is stored
in this repository.

## Retail trace

The read-only reference is `speed.exe`, 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
Addresses below are virtual addresses, with image base `0x00400000`.
Capstone 5.0.7 decoded the listed instructions. The 476-file retail manifest
(path digest, length, UTC write time) was identical before and after each
retail run; `Global/GLOBALB.BUN` was used instead of the owner's modified
`GlobalB.lzc` and `GLOBALA.BUN`.

### Observed

- `0x0057D18A..0x0057D195` clears 120 dwords starting at RideInfo
  `+0x44C`. This is one installed part record pointer per slot, in the slot
  order of the 120-entry table at `0x006F0970`. `0x0057D19C..0x0057D1A2`
  clears the corresponding counters at `+0x62C`, and
  `0x0057D1A9..0x0057D1B4` sets the 120 enable bytes at `+0x6A4` to 1.
- The stock installer calls the level-0 lookup at `0x0057D291` and stores
  the returned record pointer at `RideInfo + 0x44C + 4 * slot` at
  `0x0057D31B`. It increments the slot counter at `0x0057D322..0x0057D32C`
  and calls the enable-byte recomputation at `0x0057D333`.
  `0x0057D340..0x0057D492` drives that installer over slots, with its
  documented exceptions. See [CAR_SCENE.md](CAR_SCENE.md) for those rules.
- The enable pass reads each installed pointer from
  `RideInfo + 0x44C + 4 * slot` at `0x0057EABB` before scanning its
  attributes. The render constructor takes the same array's base at
  `0x0056A92E`, loads a pointer at `0x0056A950..0x0056A957`, then reads
  the selected record's LOD hashes at `0x0056A95D` and following code.
  It stops after model slot 29 at `0x0056AB87`.
- `GLOBALB.BUN` supplies the part records, not a per-car installed map.
  For this 240SX example its `0x00034604` table holds records for the
  chosen KIT01 bumpers and skirt. The BBS wheel record belongs to car type
  `WHEELS`; its mesh is in `Cars/WHEELS/GEOMETRY.BIN` and its textures are in
  `Cars/WHEELS/TEXTURES.BIN`. The record hashes
  match `bin_hash` of the names passed below.

### Inferred

The pointer array is the runtime installed-part state that the model builder
consumes. An explicit override resolved to a database record and placed in
our analogous per-slot table reaches the same model resolution, enable,
placement and LOD rules. The displayed part name in a record is a short shop
label; the lookup uses its part-name hash, so the command line accepts the
internal hashed name such as `240SX_KIT01_FRONT_BUMPER`.

### Unknown and approximated

The writer that transfers a player's saved garage choice into this runtime
array, and the save field that holds that choice, have not been traced.
Neither is in the files this viewer loads. `InstalledPartSelection` is our
explicit construct, not a recovered persistence mechanism. It does not
implement shop availability, prices, unlocks, save loading or wide-body
secondary installation. The existing wheel-table placement remains an
approximation: it uses the stock lateral term, while alternate retail
branches, final x87 rounding and dynamic wheel angles are not reproduced.
The scene report marks those instances as stand-ins.

## Reproduce the tuned 240SX

Run from the repository root after building, with `NFSU_ROOT` set to the
read-only retail installation. The output goes under `build`, outside the
installation. This installed set replaces both bumpers, the skirt
and all four wheel copies, and composites `TRIBAL5` over red base paint.

```powershell
$r = $env:NFSU_ROOT
$tunedArgs = @(
    "$r/Cars/240SX/GEOMETRY.BIN", "$r/Cars/240SX/TEXTURES.BIN", 'build/tuned.tga',
    '--shared-textures', "$r/Global/GLOBALB.BUN",
    '--shared-textures', "$r/Cars/TEXTURES.BIN",
    '--shared-textures', "$r/Cars/WHEELS/TEXTURES.BIN",
    '--stock-car', '240SX', '--stock-parts', "$r/Global/GLOBALB.BUN",
    '--stock-geometry', "$r/Cars/BRAKES/GEOMETRY.BIN",
    '--stock-geometry', "$r/Cars/PLATES/GEOMETRY.BIN",
    '--stock-geometry', "$r/Cars/WHEELS/GEOMETRY.BIN", '--place-wheels',
    '--stock-part', 'FRONT_BUMPER', '240SX_KIT01_FRONT_BUMPER',
    '--stock-part', 'REAR_BUMPER', '240SX_KIT01_REAR_BUMPER',
    '--stock-part', 'SKIRT', '240SX_KIT01_SKIRT',
    '--stock-part', 'WHEEL', 'BBS_STYLE01_15_23',
    '--skin-car', '240SX', '--skin-vinyls', "$r/Cars/240SX/VINYLS.BIN",
    '--skin-doorline', 'kit', '--skin-base', '200', '30', '30', '128',
    '--skin-vinyl', '0', 'TRIBAL5', '16', 'no-remap')
& ./build/nfsu_viewer.exe @tunedArgs
& ./build/nfsu_viewer.exe @($tunedArgs[0], $tunedArgs[1], 'build/tuned-120.tga') @($tunedArgs[3..($tunedArgs.Count - 1)]) --yaw 120
& ./build/nfsu_viewer.exe @($tunedArgs[0], $tunedArgs[1], 'build/tuned-230.tga') @($tunedArgs[3..($tunedArgs.Count - 1)]) --yaw 230
```

`nfsu_tuned_frame_retail` (label `retail`) runs
`tests/tuned_frame_retail.cmake` when the build is configured with
`-DNFSU_RETAIL_ROOT=<installation>`, which passes the root to it as `-DROOT`
(issue #233), and pins all three TGA hashes. It also bounds
fallback triangles at the stock painted frame's count. The default
camera's pin is also in `include/nfsu/stock_frame.h` for the Switch viewer.
The three tuned pins include the BBS tyre texture from
`Cars/WHEELS/TEXTURES.BIN` and #225's window alias, decal and per-copy brake
material changes. The two orbit pins verify the side and rear views. After
#225 the painted stock fallback count is zero, so the tuned gate requires zero
fallback triangles too. The stock frame pins are checked separately.

`nfsu_draw_commands_retail` compares the tuned CPU reference draw sequence
with the recorded GPU sequence. Its output gives the draw and triangle cost
against the painted stock 240SX. This documents command count, not a measured
Switch frame time. The coordinator's console run is needed for that timing
and for the photograph.

For the console, run `nfsu_switch_car.nro --tuned` for the orbiting CPU
viewer, `nfsu_switch_deko3d.nro --tuned` for GPU presentation, or
`nfsu_switch_compare.nro --tuned` for the same-scene comparison. Each reads
the six stock files plus `Cars/WHEELS/GEOMETRY.BIN`,
`Cars/WHEELS/TEXTURES.BIN` and `Cars/240SX/VINYLS.BIN` from `sdmc:/nfsu`
and writes nothing there. `StockSlotReport` describes records installed from
`GLOBALB.BUN`; its `BASE_PAINT` and `VINYL_LAYER0` entries remain stock
because the red paint and `TRIBAL5` are separate skin selections.
