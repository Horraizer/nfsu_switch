# nfsu-switch

Experimental clean-room engine research for Need for Speed: Underground.

The repository contains no Electronic Arts code or game data. A user-provided
PC installation is required for local research and testing.

## Current milestone

The first milestone is `nfsu_probe`, a read-only installation and chunk-format
inspector. It validates the expected game layout and walks NFS chunk containers
without loading whole asset files into memory.

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
overwrite anything, and removes its own files if it fails.
`texture-bindings` lists every mesh material record with its texture slot,
its texture hash and the pack texture that hash resolves to.

All commands are read-only with respect to the game installation. Do not
commit exported meshes or images; they are derived from copyrighted game
data.

## Project direction

1. Validate retail PC data and document chunk layouts.
2. Decode one car geometry and texture set.
3. Render the car in a portable desktop reference application.
4. Load one track region and add camera and collision tests.
5. Add a Nintendo Switch platform backend using libnx and deko3d.

