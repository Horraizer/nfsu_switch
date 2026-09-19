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
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/tex tga
build/nfsu_probe export-textures X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/dds dds
build/nfsu_probe texture-bindings X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN
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
its texture hash and the pack texture that hash resolves to.

## Reference viewer

`nfsu_viewer` renders a complete car with a deterministic CPU rasterizer
and writes the frame as a TGA screenshot. It needs no window and no
graphics library:

```text
build/nfsu_viewer X:/Games/NFSU/Cars/240SX/GEOMETRY.BIN X:/Games/NFSU/Cars/240SX/TEXTURES.BIN build/240sx.tga
build/nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> build/front.tga --yaw 0 --pitch 5 --distance 3 --width 800 --height 600
```

The camera orbits `--target X,Y,Z` at `--distance`, with `--yaw` (0 = front,
90 = right side) and `--pitch` (positive = from above) in degrees. Other
options are `--fov`, `--near`, `--far`, `--flip-v`, `--alpha-cutoff`,
`--vertex-color`, `--cull` and `--shading`. Every numeric argument is
validated against a documented range. The same inputs and options always
give a byte-identical file. Textures that are not in the car's pack are
drawn with a hash-colored checker and counted; a local texture is never
substituted for them. By default texel alpha is ignored and decoded RGB is
drawn opaquely, because material flags and blending are not decoded yet;
`--alpha-cutoff 128` is a diagnostic alpha test. Decoded textures are
capped at 128 MiB in total. The viewer never overwrites an existing file:
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

