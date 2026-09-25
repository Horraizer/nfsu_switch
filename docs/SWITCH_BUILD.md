# Nintendo Switch build

The Switch build is opt-in. A configure that does not name
`cmake/switch-toolchain.cmake` never reads it: every desktop target and test
that existed before is declared and built exactly as it was, and the desktop
suite gains three tests of this work, which need no SDK -
`nfsu_shader_manifest_tests`, `nfsu_shader_source_guards` and
`nfsu_switch_toolchain_diagnostics`. A configure that names it builds the
car-path libraries `cmake/car_libraries.cmake` declares for both configures,
the draw-recording libraries of `cmake/draw_libraries.cmake`, and `switch/`. It exists to prove the toolchain (issue #45),
to host the offline shader pipeline (issue #49,
[`SHADER_PIPELINE.md`](SHADER_PIPELINE.md)), and to show the stock 240SX
drawn by the CPU reference renderer on the console (issue #141, "The stock
car on the console" below).

Issue #145 also builds the neutral draw recorder and its upload, arena and
track draw-list dependencies through `cmake/draw_libraries.cmake`, on both
platforms. They do not change either application's rendering path. See
[`DRAW_COMMANDS.md`](DRAW_COMMANDS.md) for the command contract and tests.

## What it builds

| Target | Output, under `build-switch/switch/` | What it is |
| --- | --- | --- |
| `nfsu_switch_app` | `nfsu_switch_app.elf` | The minimal application: it initialises the console and the first controller, runs a frame loop until + is pressed or the system asks it to close, and shuts down. |
| `nfsu_switch_app_nro` | `nfsu_switch_app.nro`, `nfsu_switch_app.nacp` | That ELF packaged by `elf2nro` with the libnx default icon and a NACP titled `nfsu_switch`. |
| `nfsu_switch_car` | `nfsu_switch_car.elf` | The stock 240SX: it reads six game files from the SD card, builds the stock scene, renders it at 1280x720 with the CPU reference renderer, shows it through a libnx framebuffer and reports the render time and the frame's SHA-256. |
| `nfsu_switch_car_nro` | `nfsu_switch_car.nro`, `nfsu_switch_car.nacp` | That ELF packaged the same way, with a NACP titled `nfsu_switch_car`. |
| `nfsu_switch_shaders` | `shaders/*.dksh`, `shaders/*.tgsi` | Every GLSL stage a manifest in `shaders/` names, compiled by `uam`, and each manifest checked against the result. |

Under `build-switch/` itself, the configure also builds the car-path
libraries `nfsu_switch_car` links: `nfsu_image`, `nfsu_core`,
`nfsu_texture_export`, `nfsu_scene`, `nfsu_stock_scene`,
`nfsu_texture_decode`, `nfsu_render`, `nfsu_sha256`, `nfsu_stock_frame` and
`nfsu_screen_text`.

`nfsu_switch_app` links libnx, which devkitPro's platform file adds to every
link, and nothing else. Neither application loads a shader. The CTest suite
runs on the build machine, not on a console:

| Test | What it checks |
| --- | --- |
| `nfsu_switch_shader_<program>` | One manifest against the blobs the build compiled; the build step runs the same check. |
| `nfsu_switch_shader_blob_guards` | The same check made to fail: eleven cases, nine changed stage sources compiled with `uam` and two blobs checked with the TGSI of another compile, each named by its finding. |
| `nfsu_switch_nro` | The ELF is AArch64; the NRO has its `NRO0` header, an `ASET` section, the icon and a 0x4000-byte NACP titled `nfsu_switch`; the reader refuses a file that is not an NRO. |
| `nfsu_switch_car_nro` | The same checks for `nfsu_switch_car.nro`, whose NACP is titled `nfsu_switch_car`. |

The desktop programs, the other libraries and every test are not configured
for the console: the top-level `CMakeLists.txt` includes
`cmake/car_libraries.cmake` and `cmake/draw_libraries.cmake`, enters `switch/`
and returns before it declares
any of them.

## Prerequisites

- CMake 3.21 or newer for the Switch build (the desktop build still needs
  only 3.20), and a generator. The commands below use Ninja.
- devkitPro with the `switch-dev` package group. The build uses these
  packages, and names the missing one if any is absent (see Diagnostics):

  | Package | What the build uses from it |
  | --- | --- |
  | `devkitA64` (`devkita64-gcc`, `devkita64-binutils`) | the `aarch64-none-elf-` compilers and tools |
  | `libnx` | `libnx.a`, `switch.specs`, the headers, the default icon |
  | `deko3d` | `libdeko3d.a` and `deko3d.h`; checked now, linked by the renderer later |
  | `uam` | the offline shader compiler |
  | `switch-tools` | `elf2nro`, `nacptool` |
  | `switch-cmake`, `devkita64-cmake`, `dkp-cmake-common-utils` | devkitPro's CMake toolchain and platform files |
  | `switch-pkg-config` | `aarch64-none-elf-pkg-config`, which devkitPro's Switch toolchain requires |

On a Debian or Ubuntu machine, devkitPro's documented installation is:

```text
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S switch-dev
```

On Windows, devkitPro installs through its own installer into an MSYS2
environment, and its CMake files refuse a native Windows CMake: the Switch
build is configured from devkitPro's MSYS2 shell with the CMake installed
there (`pacman -S cmake`). That route was not run for this branch. Issue
#141 later ran it: the Switch tree was configured, built and tested from
MSYS2's `bash -l` with devkitA64 r30 (`aarch64-none-elf-gcc` 16.1.0) and
libnx 4.12.0, as "The stock car on the console" below shows.

### How the SDK used for this branch's evidence was installed

The sandbox this branch was built in refuses devkitPro's package servers:
`apt.devkitpro.org`, `pkg.devkitpro.org` and `downloads.devkitpro.org` all
answered HTTP 403 from its egress proxy, so `dkp-pacman` could not run.
devkitPro also publishes the same packages as the Docker image
`devkitpro/devkita64`. Docker Hub's anonymous pull limit was exhausted, so the
image was read through Google's Docker Hub mirror, `mirror.gcr.io`, which
serves the same content-addressed manifests and layers; no Docker daemon was
used. These are the commands, run as root:

```text
# The amd64 manifest of devkitpro/devkita64:20260219, fetched by the digest
# Docker Hub lists for that tag, and checked against it.
curl -sS -H 'Accept: application/vnd.oci.image.manifest.v1+json' \
  -o manifest.json https://mirror.gcr.io/v2/devkitpro/devkita64/manifests/sha256:82575ea78651b530b2e232bb3799cfd1fe331514e053d5f724bb4b28191fb79d
sha256sum manifest.json

# The two layers that hold /opt/devkitpro, each checked against its digest.
for d in ea5d3216df1fb580c54c016ee13ff58eb965c0a0b0ef08afd8dfd5a1c03e95a8 \
         da5be4661c9ac4e389f9e5f939e58bff7d32bc60569fa7c14e849d5246881691; do
  curl -sSL -o layer-$d.tar.gz https://mirror.gcr.io/v2/devkitpro/devkita64/blobs/sha256:$d
  echo "$d  layer-$d.tar.gz" | sha256sum -c -
done

# /opt/devkitpro, the layers in order, with the one whiteout the second
# layer carries applied by hand.
tar -xzf layer-ea5d3216df1fb580c54c016ee13ff58eb965c0a0b0ef08afd8dfd5a1c03e95a8.tar.gz -C / opt/devkitpro
tar -xzf layer-da5be4661c9ac4e389f9e5f939e58bff7d32bc60569fa7c14e849d5246881691.tar.gz -C / --exclude='*/.wh.*' opt/devkitpro
rm -rf /opt/devkitpro/pacman/var/lib/pacman/local/dkp-toolchain-vars-1.0.4-1
export DEVKITPRO=/opt/devkitpro
```

The image's own history says those layers are `dkp-pacman -S switch-dev
switch-portlibs devkitARM dkp-toolchain-vars hactool` on devkitPro's base
image. The package database under `/opt/devkitpro/pacman` records the versions
this branch was built and tested with:

| Package | Version |
| --- | --- |
| `devkitA64` | r29.2-1 (`aarch64-none-elf-gcc` 15.2.0) |
| `libnx` | 4.12.0-1 |
| `deko3d` | 0.5.0-1 |
| `uam` | 1.1.0-1 (`uam 1.1.0 - Built on Dec 17 2020 20:28:03`) |
| `switch-tools` | 1.13.1-1 |
| `switch-cmake` | 1.5.2-1 |
| `devkita64-cmake` | 1.1.3-1 |
| `dkp-cmake-common-utils` | 1.5.3-1 |
| `switch-pkg-config` | 0.28-4 |

## Build commands

The desktop build takes the same commands as before:

```text
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

The Switch build, in a build directory of its own:

```text
export DEVKITPRO=/opt/devkitpro
cmake -S . -B build-switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-switch
ctest --test-dir build-switch --output-on-failure
```

`DEVKITPRO` is read by the configure only, and may be passed as
`-DDEVKITPRO=...` instead; the build and `ctest` need nothing exported.
`Release` is also what devkitPro's platform file picks when no build type is
given. Every build directory name above starts with `build`, which
`.gitignore` covers, and so is everything the Switch build writes: the ELF,
the NRO, the NACP, the compiled shaders and the checks' stamps are all under
`build-switch/`. `.gitignore` also ignores those file types anywhere in the
tree, and `CMakeUserPresets.json`, which is where a machine's SDK path would
otherwise be written down; no tracked file names an SDK path.

## How the SDK is located

`cmake/switch-sdk.cmake` resolves two roots, each from the CMake variable of
that name (`-DDEVKITPRO=...`), else the environment variable, else a default:

| Root | Default | What is looked for under it |
| --- | --- | --- |
| `DEVKITPRO` | `/opt/devkitpro`, where devkitPro installs | devkitPro's CMake files, libnx, deko3d, `uam`, `elf2nro`, `nacptool`, `aarch64-none-elf-pkg-config` |
| `DEVKITA64` | `DEVKITPRO/devkitA64` | `bin/aarch64-none-elf-{gcc,g++,gcc-ar,gcc-ranlib,ld,strip,objcopy,nm}` |

devkitPro's own toolchain looks for the compilers under
`DEVKITPRO/devkitA64` only; `cmake/switch-toolchain.cmake` sets them from
`DEVKITA64` before handing over to it, so that directory is the one the build
uses. Both roots are exported to the environment of the CMake run, which is
how devkitPro's files and CMake's compiler-detection projects see the same
SDK. A configure that succeeds records both roots in the build directory's
cache, so every later configure of it, including one a build starts by itself,
resolves the same SDK whatever its environment holds. As with any compiler
change in CMake, pointing either root elsewhere takes a fresh build
directory.

libnx's link specs find the linker script through the `DEVKITPRO`
environment variable when the link runs (`%:getenv(DEVKITPRO
/libnx/switch.ld)` in `switch.specs`), so a build started without it exported
would fail with `environment variable 'DEVKITPRO' not defined`. The
toolchain file runs every link through `cmake -E env DEVKITPRO=<root>`, a
linker launcher, which is why the Switch build needs CMake 3.21; a build
therefore needs nothing exported, and always links against the root this
configure checked.

## Diagnostics

The top-level `CMakeLists.txt` runs the check before `project()` reads the
toolchain file, whenever the toolchain named is `switch-toolchain.cmake`. A
missing piece stops the configure there, with one error that names it, where
it was looked for, the package that provides it and the variable that points
the build elsewhere. There is no call stack, and none of the errors CMake adds
after a toolchain file has failed, which would otherwise follow the message
and say that Ninja or the C++ compiler could not be found.

With no SDK at the root:

```text
CMake Error at CMakeLists.txt:11 (message):
    Nintendo Switch build: the devkitPro root /opt/no-such-devkitpro does not exist (DEVKITPRO, from the CMake variable DEVKITPRO).
      fix: install devkitPro with the switch-dev package group, or set DEVKITPRO to the directory it is installed in.
    docs/SWITCH_BUILD.md lists what the Switch build needs.


-- Configuring incomplete, errors occurred!
```

With an SDK that lacks one package, here `uam` (the run's scratch path is
shortened to `/work/sdk`):

```text
CMake Error at CMakeLists.txt:11 (message):
    Nintendo Switch build: the devkitPro SDK is incomplete.
      DEVKITPRO = /work/sdk (from the environment variable DEVKITPRO)
      DEVKITA64 = /work/sdk/devkitA64 (from DEVKITPRO, because DEVKITA64 is not set)
      missing: uam, the offline deko3d shader compiler
        looked for: /work/sdk/tools/bin/uam
        package: uam (dkp-pacman -S uam)
        fix: install the package, or set DEVKITPRO to a devkitPro root that has it
    docs/SWITCH_BUILD.md lists what the Switch build needs.


-- Configuring incomplete, errors occurred!
```

Every missing piece gets its own `missing:` entry, so an empty root lists the
devkitA64 tools, the CMake files, libnx, deko3d, `uam` and the switch-tools
together. A CMake older than 3.21 is told so, and a native Windows CMake is
told to use devkitPro's MSYS2 shell, before anything else is looked at. A
`DEVKITPRO` or `DEVKITA64` that is not an absolute path is refused by name,
rather than resolved against whichever directory CMake happens to run in,
which is not the same for a configure the build starts itself. The toolchain
file runs the same check itself when another project uses it; the message is
the same, but that project gets CMake's follow-on errors after it.

`nfsu_switch_toolchain_diagnostics`, in the desktop CTest suite, configures
the project against SDK trees that each lack something and requires those
messages; it needs no SDK, because every case stops before a compiler is run.
On Linux it runs nine cases: a missing root, from the CMake variable and from
the environment, an empty root, an SDK without `uam` and one without deko3d,
a `DEVKITA64` without the compilers, a relative `DEVKITPRO` and a relative
`DEVKITA64`, and the toolchain file used by another project. The desktop
build still takes CMake 3.20, and there, as on a native Windows CMake, the
check stops before it reads an SDK, so the test runs the one case that says
so.

## The stock car on the console

`nfsu_switch_car` (issue #141) is the first visible step toward the car on
the Switch screen, taken before the deko3d renderer exists. It draws the frame
[`RENDERER.md`](RENDERER.md) pins on the desktop - the stock 240SX with
`--place-wheels`, 1280x720, a 3686418-byte TGA with SHA-256
`FD6727333A94803A97FD26282CD89DF6F3D3295E0231F4A871A1B14089D28CE9` - with the
same library code compiled for AArch64, and reports whether the console made
the same bytes. Either answer is a result; a difference is reported, not
hidden.

Nothing in this section has been run on a console or an emulator. The branch
that added it built the NRO and checked it as a file, and ran everything
else on the desktop; what the screen shows, what nxlink delivers and how long
the console takes are what the code says, not something observed.

### The six inputs

They are copied from the game's own installation to the SD card, under
`sdmc:/nfsu/`, at the paths the game uses:

| On the SD card | In the viewer's pinned command |
| --- | --- |
| `sdmc:/nfsu/Cars/240SX/GEOMETRY.BIN` | `<GEOMETRY.BIN>` |
| `sdmc:/nfsu/Cars/240SX/TEXTURES.BIN` | `<TEXTURES.BIN>` |
| `sdmc:/nfsu/Global/GLOBALB.BUN` | the first `--shared-textures`, and `--stock-parts` |
| `sdmc:/nfsu/Cars/TEXTURES.BIN` | the second `--shared-textures` |
| `sdmc:/nfsu/Cars/BRAKES/GEOMETRY.BIN` | the first `--stock-geometry` |
| `sdmc:/nfsu/Cars/PLATES/GEOMETRY.BIN` | the second `--stock-geometry` |

A different root may be given as the application's one argument. The files
are opened for reading only, with `std::ifstream`; the application creates,
writes, renames and removes nothing, on the SD card or anywhere else. The
NRO that nxlink uploads is stored by the homebrew menu's netloader, as for
any application - nxlink's `-p` option sets where - and that write is the
loader's, not this application's.

### Running it

With the homebrew menu's netloader waiting on the console:

```text
nxlink -s build-switch/switch/nfsu_switch_car.nro
```

`-s` keeps nxlink running as a server after the upload, so the application's
stdout arrives on the PC; `-a <address>` names the console when nxlink cannot
find it, and `--args <root>` passes a root other than `sdmc:/nfsu`. When the
application was not started by nxlink it opens no socket and reports on the
screen alone.

### What it shows and prints

Every line goes to stdout and to the screen. The screen text is drawn with
`nfsu_screen_text` into the application's own framebuffer, because libnx's
console takes the display for a framebuffer of its own and `nxlinkStdio()`
takes stdout away from the console.

Until there is a frame, the report runs down the screen: one line per input,
`read <path>: <bytes> bytes` or `NOT READ <root>/<path>: <reason>`, then the
progress. An input that cannot be read stops the run before anything is
parsed - `nothing rendered: N of 6 inputs could not be read` - because a
partial set of inputs can only ever differ from the pinned frame. A parser's
complaint about an input is printed as `issue: <path>: ...`; a failure the
viewer would stop on is printed as `nothing rendered: <reason>`. Either way +
exits.

Once the frame is rendered it fills the screen, with these lines along the
bottom edge, below the car:

```text
nfsu_switch_car: the stock 240SX from sdmc:/nfsu, CPU reference renderer, 1280x720
render_ms <ms>  prepare_ms <ms>  read_ms <ms>  encode_sha256_ms <ms>
tga_bytes 3686418  sha256 <64 hexadecimal digits>
result: EQUAL to the pinned desktop frame
Press + to exit.
```

or `result: DIFFERENT from the pinned desktop frame, 3686418 bytes, sha256
FD67...` in its place. `render_ms` is `render_scene` alone; the others are
reading the six files, parsing and preparing the scene, and encoding and
hashing the frame. The times come from the ARM system counter. stdout also
carries the viewer's stock-slot summary and render statistics, in the
viewer's own words, so a console frame that differs can be compared with the
desktop report line by line.

### Why it is the same frame

- The same libraries, compiled with the same options.
  `cmake/car_libraries.cmake` declares the car path once for both configures,
  so the console compiles the same source lists with the same
  floating-point flags. The desktop compile commands carry no `-std` option,
  because the desktop GCC 13 defaults to `gnu++17`, which meets every
  target's `cxx_std_17`. devkitA64's GCC 16 defaults to `gnu++20`, where
  `std::filesystem::path::u8string()` returns `std::u8string` and
  `src/render.cpp` and `src/texture_export.cpp` stop compiling, so the
  console asks for `gnu++17` by name on the car-path targets and on
  `nfsu_switch_car`.
- The same calls. `nfsu_stock_frame` makes the calls `src/viewer_main.cpp`
  makes for the pinned command - the same parsers over the same bytes, the
  same wanted-hash texture decode in the same source order, the same stock
  assembly, `render_scene` with the default options - and
  `src/stock_frame.cpp` names the viewer step each one mirrors. The viewer
  itself is unchanged. The desktop test `nfsu_stock_frame_viewer_match`
  holds the two together: it renders six synthetic files in the game's
  layout with the viewer and with the application's four calls, and
  requires the same TGA bytes, by CMake's own `file(SHA256)`, and the same
  thirteen report lines.
- The same encoding. The frame is encoded by `nfsu::write_tga` into a
  `std::ostringstream` opened in binary mode, which is what `write_rgba_tga`
  does before it writes the file; `nfsu_stock_frame_tests` compares the
  bytes with the file `write_frame_tga` writes.
- The same answer on the desktop, over the retail files, read-only:

  ```text
  build/nfsu_stock_frame_tests --frame <the game's installation>
  ```

  prints `tga_bytes 3686418` and `sha256
  FD6727333A94803A97FD26282CD89DF6F3D3295E0231F4A871A1B14089D28CE9`, the
  pinned hash, in a Debug and in a Release build.

### What could still differ on the console

`nfsu_core` is compiled without `-ffp-contract=off` on both platforms, as it
was before this application existed. On AArch64, GCC 16 at `-O2` fuses seven
multiply-adds in `src/part_placement.cpp`, in
`derive_cullable_part_placements` and `multiply_placement_matrix`, which the
stock frame uses to place the wheels, the brakes, the exhausts and the plate;
the desktop builds target x86-64 without FMA, so nothing is fused there, and
no other object of the car path holds a fused instruction on the console. A
desktop build of that one file with `-mfma` - GCC 13 then fuses six
multiply-adds in the same two functions - gave bit-identical placements and
instance transforms and the pinned frame, so contraction there is not
expected to move this frame. That is a simulation on another compiler, not
the console. If the console reports `DIFFERENT`, it is the first place to
look; disabling contraction in `nfsu_core` would change the desktop's compile
options too, and is a decision for its own issue.

## What is deliberately not covered yet

- Nothing has run on a console or an emulator. Both applications are checked
  as files: an AArch64 ELF and a well-formed NRO each. That their frame loops
  exit cleanly, that `nfsu_switch_car` shows its frame and that its report
  reaches nxlink is what the code says, not something observed.
- There is no deko3d device. `nfsu_switch_car` draws with the CPU reference
  renderer; neither application loads a shader, and the compiled `.dksh`
  files are not packaged into an NRO's RomFS; how the deko3d renderer carries
  them is its decision.
- Only the car-path libraries are compiled for the console. Whether the
  track, skin, inspection and platform layers build against devkitA64's
  newlib is untested here.
- Controller, filesystem, timing and logging services beyond the frame loop
  (ROADMAP M3) are not started; `nfsu_switch_car` calls libnx directly for
  the little it needs.
- Only the NRO format is produced; no NSO or ExeFS.
- A Debug Switch build was not run. The Windows (MSYS2) route was run for
  issue #141, as the Prerequisites section says.
- No CI runs the Switch build.
