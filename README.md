# nfsu-switch

Experimental clean-room engine research for Need for Speed: Underground.

The repository contains no Electronic Arts code or game data. A user-provided
PC installation is required for local research and testing.

## Current milestone

The first milestone is `nfsu_probe`, a read-only installation and chunk-format
inspector. It validates the expected game layout and walks NFS chunk containers
without loading whole asset files into memory. `nfsu_viewer` renders a decoded
car to a screenshot with a portable CPU reference renderer.

## Build

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```text
build/nfsu_probe summary X:/Games/NFSU
build/nfsu_probe chunks X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN 64
build/nfsu_probe meshes X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN
build/nfsu_probe obj X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN build/240sx.obj
build/nfsu_probe textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe pack-set X:/Games/NFSU/GLOBAL/GLOBALB.BUN
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/tex tga
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/dds dds
build/nfsu_probe texture-bindings X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
build/nfsu_probe car-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe stock-parts X:/Games/NFSU/Global/GLOBALB.BUN 240SX X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/BRAKES/GEOMETRY.BIN X:/Games/NFSU/Cars/PLATES/GEOMETRY.BIN
build/nfsu_probe car-placement X:/Games/NFSU/Global/GLOBALB.BUN 240SX
build/nfsu_probe part-markers X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN exhaust
build/nfsu_probe body-skin-inputs 240SX --source car-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN --source car-vinyls X:/Games/NFSU/Cars/240SX/VINYLS.BIN --source other X:/Games/NFSU/Cars/TEXTURES.BIN --target-texture DUMMY_SKIN1 --vinyl 0 TRIBAL5 16 no-remap --doorline plain --base 200 30 30 128 --assemble
```

`meshes` lists every mesh with its name, vertex count, face count and
validation status. `obj` writes all valid meshes to a diagnostic OBJ file at
the given path. Both commands print precise diagnostics for malformed meshes
and exit with status 3 if any issue was found. See `docs/FORMATS.md` for the
decoded layout and the exact OBJ axis and winding conventions.

`textures` lists every texture in a pack: hash, format, dimensions, mip
count, record and blob offsets and name. `export-textures` decompresses each
valid texture and writes it to the output directory. `tga` writes the
decoded top level as a 32-bit TGA. `dds` writes the decompressed data of all
levels unchanged, after a DDS header. File names are
`t<index>_<hash>_<name>`, so they stay unique. The export refuses to
overwrite anything. If it fails, it tries to remove the files it created,
and it names any file it could not remove.
`texture-bindings` lists every mesh material record with its texture slot,
texture hash, resolved pack texture, material-key slot and material-key hash.
Its summary also inventories the material flags and material-key usage.
`pack-set` finds every texture pack in a file, including packs nested in a
BUN such as `GLOBAL/GLOBALB.BUN`, and lists their metadata and records.
It never decodes or writes pixel data. Observed P8 records are parsed and
their indexed mip chain and 1024-byte palette can be extracted through the
library API. Diagnostic TGA and DDS export still refuses P8 because palette
channel semantics and runtime skin composition are not yet implemented.
`car-placement` parses the car type table of `GLOBAL/GLOBALB.BUN` and
prints, per car type, its name, class, geometry paths and brand, the four
wheel records with their file offsets, and the 14 placements the game
derives from them, each with the table its source indices belong to.
`part-markers` lists every `0x0013401A` attachment marker of a
`GEOMETRY.BIN` with its resolved name, role, translation and the local
placement the exhaust and licence-plate draw paths build from it; an
optional role argument filters the listing. It reports only meshes that
validated, and prints how many it skipped, so a mesh rejected after its
marker chunk parsed contributes nothing. Between them, the wheels and
brakes come from the car type table and the exhaust and plate from markers,
while every other installed part is drawn at the car's own matrix. See
`docs/PART_PLACEMENT.md`.
`car-parts` parses the car part database of `GLOBAL/GLOBALB.BUN` and lists
every part record: car type hash, slot, upgrade level, part name hash and
the four per-LOD mesh hashes. A car type name lists only its records.
`stock-parts` applies the game's stock-part rules to one car type and
prints the chosen record per slot with each LOD mesh hash resolved against
the `GEOMETRY.BIN` files given after the car type, including the mesh the
game falls back to when the one a part names is missing. `car-parts`
reports every structural problem and marks the database invalid;
`stock-parts` refuses to run on such a database. See `docs/CAR_SCENE.md`
for the evidence and for what a "stock" part set does and does not cover.

## Reference viewer

`nfsu_viewer` renders a complete car with a deterministic CPU rasterizer
and writes the frame as a TGA screenshot. It needs no window and no
graphics library:

```text
build/nfsu_viewer X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/240sx.tga
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/front.tga --yaw 0 --pitch 5 --distance 3 --width 800 --height 600
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/shared.tga --shared-textures X:/Games/NFSU/GLOBAL/GLOBALB.BUN
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/part.tga --mesh-name 240SX_KITW01_BODY_A --mesh-index 4 --mesh-hash 0x013953B3
```

Textures come only from files named on the command line. The car's
`TEXTURES.BIN` is searched first. Each `--shared-textures FILE` is searched
after it, in the order given. A
hash is taken from the first source that has any record for it, even an
unusable one, so a broken car texture is never silently replaced by a
shared one. At most 8 shared files are accepted, and at most 512 MiB is
read for `TEXTURES.BIN` and all shared files together (each file at most
256 MiB); a file that would exceed the total is refused before it is read.
See `docs/RENDERER.md`.

By default every valid mesh is drawn (the "all meshes" profile), so every
kit and style variant overlaps. `--mesh-index N` (source position) and
`--mesh-name NAME` (exact, case-sensitive stored name), and `--mesh-hash HEX`
(verified 0x00134003/0x00134004 directory hash) select an explicit subset
instead; all may be repeated and mixed, at most 256 selectors and
16384 name bytes. Selected meshes are always drawn in source order. An
out-of-range index, an unknown or ambiguous hash, an unknown, empty or
ambiguous name, or a mesh selected twice is a usage error (exit 1) and
nothing is written. Neither profile is
the game's stock car. Which parts the game installs, which mesh each one
uses at a LOD (`nfsu_probe stock-parts`), where the four wheels go and which
parts hang off an attachment marker (`nfsu_probe car-placement`,
`nfsu_probe part-markers`) are decoded, but the viewer does not consume any
of it yet, and paint and skin are still missing (see `docs/CAR_SCENE.md` and
`docs/PART_PLACEMENT.md`).

## Body-skin compositor reference

`nfsu_body_skin` (`include/nfsu/body_skin.h`) is a bounded CPU reference for
the game's runtime body-skin compositor. It reproduces the weighted lane
sum both composite paths share, the mask arithmetic, the colour remap, the
target markers, and the palette the indexed path builds, for both target
types the game dispatches on. Every bound the game leaves unchecked is
refused with a message instead, and the colour quantizer the indexed path
falls back to is not implemented and never guessed. Its API numbers byte
lanes instead of naming channels, because the two paths store a colour with
two lanes exchanged; which channel each lane carries is documented once, with
its evidence. No pixel format is named anywhere, because no instruction names
one. See `docs/BODY_SKIN.md`.

`nfsu_skin_inputs` (`include/nfsu/skin_inputs.h`) feeds it. It takes an
explicit customization selection - which vinyl each of the four layers
uses with its `NUMCOLOURS` and `REMAP` colors, which hood/spoiler and
doorline variant is installed, and the five paint colors - and turns it
into a `BodySkinRequest` whose every buffer the result owns. It invents no
customization state: where the game itself substitutes a value, the
substitution is optional and reported back. It resolves each source and its
exact `_MASK` partner out of read-only parsed packs, keeps every raw
identifier it saw, converts only in the three cases retail evidence proves,
and refuses the rest. `nfsu_probe body-skin-inputs` prints the whole
assembled layer list with its provenance and writes nothing. See
`docs/SKIN_INPUTS.md`.

Selection and material lookup happen once, in `prepare_car_scene`
(`include/nfsu/scene.h`). It builds a self-contained `PreparedCarScene`
that owns copies of the selected vertices, triangles and resolved textures
and holds one validated draw per material group. The CPU renderer draws
only that scene and never looks up a texture hash; a future deko3d backend
is meant to consume the same draw list. Parsed texture packs become
`RenderTextureSet` through the separate backend-neutral
`nfsu_texture_decode` library, so neither backend depends on the other's
implementation.

The camera orbits `--target X,Y,Z` at `--distance`, with `--yaw` (0 = front,
90 = right side) and `--pitch` (positive = from above) in degrees. Other
options are `--fov`, `--near`, `--far`, `--flip-v`, `--alpha-cutoff`,
`--vertex-color`, `--cull` and `--shading`. Every numeric argument is
validated against a documented range. The same inputs and options always
give a byte-identical file. Textures that are in none of the given sources
are drawn with a hash-colored checker and counted; another texture is never
substituted for them. By default texel alpha is ignored and decoded RGB is
drawn opaquely, and `--alpha-cutoff 128` is a diagnostic alpha test.

`--material-state retail` applies the car material render state recovered
in `docs/MATERIALS.md`: the texture-info alpha test with its GREATER 0xAA
rule, the texture-info blend selection, the retail draw queue order, and
the flag-controlled coefficient gates. The retail order is recovered, not
guessed: the queue buckets a material by two more texture-info bytes,
batches by texture inside a bucket and keeps depth writes on for blended
car draws. The undecoded coefficients are explicit options
(`--material-alpha`, `--environment`, `--environment-color`,
`--environment-global`) whose defaults make the environment term vanish and
leave the alpha lane at the identity, which is an assumption and not a
retail value. The renderer never supplies a retail value for one. `off` is the default and
its frame is byte for byte what it was before the option existed. Decoded textures are
capped at 128 MiB in total, over all sources together. The viewer never overwrites an existing file:
it creates `<out>.partial` exclusively and publishes it with a no-replace
rename (Windows) or `link` (POSIX), and it refuses to write on other
platforms.
See `docs/RENDERER.md` for the camera, projection, clipping, fill, depth,
sampling and alpha rules.

All commands are read-only with respect to the game installation. Do not
commit exported meshes, images or screenshots; they are derived from
copyrighted game data.

## Project direction

1. Validate retail PC data and document chunk layouts.
2. Decode one car geometry and texture set.
3. Render the car in a portable desktop reference application.
4. Load one track region and add camera and collision tests.
5. Add a Nintendo Switch platform backend using libnx and deko3d.

