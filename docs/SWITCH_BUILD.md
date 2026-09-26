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
drawn by the CPU reference renderer on the console (issue #141), painted
and orbited with the left stick (issue #148; "The stock car on the
console" below).

Issue #145 also builds the neutral draw recorder and its upload, arena and
track draw-list dependencies through `cmake/draw_libraries.cmake`, on both
platforms. They do not change either application's rendering path. See
[`DRAW_COMMANDS.md`](DRAW_COMMANDS.md) for the command contract and tests.

## What it builds

| Target | Output, under `build-switch/switch/` | What it is |
| --- | --- | --- |
| `nfsu_switch_app` | `nfsu_switch_app.elf` | The minimal application: it initialises the console and the first controller, runs a frame loop until + is pressed or the system asks it to close, and shuts down. |
| `nfsu_switch_app_nro` | `nfsu_switch_app.nro`, `nfsu_switch_app.nacp` | That ELF packaged by `elf2nro` with the libnx default icon and a NACP titled `nfsu_switch`. |
| `nfsu_switch_car` | `nfsu_switch_car.elf` | The stock 240SX: it reads six game files from the SD card, builds the stock scene, composes and binds the body skin, renders it at 1280x720 with the CPU reference renderer, shows it through a libnx framebuffer and reports the render time and the frame's SHA-256 against the pinned desktop frame; then the left stick orbits the camera, - returns it to the default and + exits. |
| `nfsu_switch_car_nro` | `nfsu_switch_car.nro`, `nfsu_switch_car.nacp` | That ELF packaged the same way, with a NACP titled `nfsu_switch_car`. |
| `nfsu_switch_shaders` | `shaders/*.dksh`, `shaders/*.tgsi` | Every GLSL stage a manifest in `shaders/` names, compiled by `uam`, and each manifest checked against the result. |

Under `build-switch/` itself, the configure also builds the car-path
libraries `nfsu_switch_car` links: `nfsu_image`, `nfsu_core`,
`nfsu_texture_export`, `nfsu_scene`, `nfsu_stock_scene`,
`nfsu_texture_decode`, `nfsu_render`, `nfsu_sha256`, `nfsu_stock_frame`,
`nfsu_screen_text`, the skin layers `nfsu_body_skin`, `nfsu_skin_inputs`
and `nfsu_runtime_skin`, the camera and controller layers `nfsu_inspect`,
`nfsu_input` and `nfsu_input_camera`, and `nfsu_stock_view`.

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
the Switch screen, taken before the deko3d renderer exists. Issue #148 paints
it and lets the left stick orbit the camera. It draws the stock 240SX with
`--place-wheels` at 1280x720, with the runtime body skin composed and bound
exactly as the desktop viewer's `--skin-car` route does it, with the same
library code compiled for AArch64, and at the default camera reports whether
the console made the same bytes as the desktop. Either answer is a result; a
difference is reported, not hidden.

Two frames are pinned, both from the viewer on the retail files, read-only:

| Frame | Viewer options added to the stock command of [`RENDERER.md`](RENDERER.md) | TGA bytes | SHA-256 |
| --- | --- | ---: | --- |
| painted, the default (issue #148) | `--skin-car 240SX --skin-doorline plain --skin-base 30 60 160 128` | 3686418 | `52B9303A822F82242717B3A2F363BE6584E4A24B533B69B6AA78EED6DFBED994` |
| unpainted, `--unpainted` (issue #141) | none | 3686418 | `E7A042A560E807EE74595449275FFBDC1AE0A0BF321CE06EEF434BC5B1C8274A` |

Issue #161 re-pinned both, because both showed the defect it fixed: two
pixels of the front-left wheel's backing quad above the far fender. They
were `FE51E57A...` painted and `FD672733...` unpainted; "The left side of
the car (issue #161)" below has the reason and the evidence.

The painted pin was produced with this command, from a Debug and from a
Release build, both writing the same file and the same report:

```text
nfsu_viewer <Cars/240SX/GEOMETRY.BIN> <Cars/240SX/TEXTURES.BIN> painted.tga
    --shared-textures <Global/GLOBALB.BUN> --shared-textures <Cars/TEXTURES.BIN>
    --stock-car 240SX --stock-parts <Global/GLOBALB.BUN>
    --stock-geometry <Cars/BRAKES/GEOMETRY.BIN> --stock-geometry <Cars/PLATES/GEOMETRY.BIN>
    --place-wheels --skin-car 240SX --skin-doorline plain --skin-base 30 60 160 128
```

It exits 0 with nothing on standard error. The skin is 512x512, type 32, one
layer - the plain doorline, `240SX_DOORLINE` and `240SX_DOORLINE_MASK` from
the car's own `TEXTURES.BIN` - composed into `DUMMY_SKIN1` from
`Cars/TEXTURES.BIN` and bound under `0x530B82B0` (`DUMMY_SKIN1`) and
`0x77A20245` (`240SX_SKIN1`), displacing nothing. Against the unpainted frame
the geometry is the same - 24 meshes, 30 instances, 6137 triangles, 116746
pixels drawn - and the body's groups now resolve: 52 of 79 material groups
instead of 20, 5904 textured triangles instead of 2610, and 33795 fallback
pixels instead of 103808. The body is blue with its door lines; the windows
and the decals still show the fallback checker, and 11 texture hashes stay
unresolved, as 12 did without the skin.

Where each part of that skin selection comes from, as `stock_240sx_skin()`
in `include/nfsu/stock_frame.h` records it:

- `--skin-doorline plain` is what the doorline helper at `0x005A96B0` picks
  for this car: no `WIDE_BODY` part is installed and the front bumper, rear
  bumper and body parts are at upgrade level 0 ([`CAR_SCENE.md`](CAR_SCENE.md)).
- No vinyl and no hood or spoiler vinyl: a stock car installs none. That is
  also why the car's `VINYLS.BIN` is not needed.
- The base paint is a choice, not the game's. The front end installs a
  `BASE_PAINT` record named by `CarTypeInfo +0xC5C`, which is not decoded, so
  the stock colour is unknown; 30/60/160 with GLOSS 128 is the value
  [`RENDERER.md`](RENDERER.md) already renders the retail skin with.

The #141 frame was reported `EQUAL` on the owner's console in the review of
pull request #143, against the pin of that time. Nothing that issue #148
added had been run on a console or an emulator when it merged: the branch
built the NRO and checked it as a file, and ran everything else on the
desktop. What the screen shows, how the stick feels, what nxlink delivers
and how long the console takes are what the code says, not something
observed. The one console observation since is the owner's, orbiting the
painted car of pull request #160, which is how issue #161 was found.

### The six inputs

They are copied from the game's own installation to the SD card, under
`sdmc:/nfsu/`, at the paths the game uses. The painted frame needs no other
file: the skin's target and its doorline are in two of these six.

| On the SD card | In the viewer's pinned command |
| --- | --- |
| `sdmc:/nfsu/Cars/240SX/GEOMETRY.BIN` | `<GEOMETRY.BIN>` |
| `sdmc:/nfsu/Cars/240SX/TEXTURES.BIN` | `<TEXTURES.BIN>`, and skin source 0 (the doorline and its mask) |
| `sdmc:/nfsu/Global/GLOBALB.BUN` | the first `--shared-textures`, and `--stock-parts` |
| `sdmc:/nfsu/Cars/TEXTURES.BIN` | the second `--shared-textures`, which holds the skin target `DUMMY_SKIN1` |
| `sdmc:/nfsu/Cars/BRAKES/GEOMETRY.BIN` | the first `--stock-geometry` |
| `sdmc:/nfsu/Cars/PLATES/GEOMETRY.BIN` | the second `--stock-geometry` |

The files are opened for reading only, with `std::ifstream`; the application
creates, writes, renames and removes nothing, on the SD card or anywhere
else. The NRO that nxlink uploads is stored by the homebrew menu's
netloader, as for any application - nxlink's `-p` option sets where - and
that write is the loader's, not this application's.

### Running it

With the homebrew menu's netloader waiting on the console:

```text
nxlink -s build-switch/switch/nfsu_switch_car.nro
```

`-s` keeps nxlink running as a server after the upload, so the application's
stdout arrives on the PC; `-a <address>` names the console when nxlink cannot
find it. `--args` passes the application an argument: `--unpainted` draws
and checks the unpainted frame of issue #141 instead, and any other
argument is a root other than `sdmc:/nfsu`. One argument per run is what
this was written for; how nxlink passes an argument that starts with `-`,
or several in one `--args`, was not tried. When the application was not
started by nxlink it opens no socket and reports on the screen alone.

### Controls

Through the controller model of [`INPUT.md`](INPUT.md): player 1 and the
handheld controls, read with `padInitializeDefault`, sampled once a frame
into a `GamepadFrame` stamped with the ARM system counter, mapped by the
table in that document, and applied by a `GamepadCamera` with the control
table of `nfsu_stock_view`:

| Control | What it does |
| --- | --- |
| left stick | orbits the camera: right swings the eye to the camera's right around the car, up raises it, at 120 degrees a second at full deflection |
| - | returns the camera to the default, where the hash is compared again |
| + | exits to the homebrew menu |

Nothing else is bound, so no control changes a render option or the
viewport. The model's rules hold: the stick has the default radial dead zone,
a button held since launch or since a controller connected does nothing
until it has been released, and a frame after a stall moves the camera no
further than a frame 100 ms long would. That last rule is also what the
orbit runs into on the console: a frame rendered at about 110 ms (the #141
render time) orbits as a 100 ms frame, slightly slower than 120 degrees a
second. The frame is rendered again only when the camera moved;
`appletMainLoop` runs once a frame either way, so a request from the system
to close ends the application as + does.

### What it shows and prints

Every report line goes to stdout and to the screen. The screen text is drawn
with `nfsu_screen_text` into the application's own framebuffer, because
libnx's console takes the display for a framebuffer of its own and
`nxlinkStdio()` takes stdout away from the console.

Until there is a frame, the report runs down the screen: the chosen root and
frame, one line per input, `read <path>: <bytes> bytes` or
`NOT READ <root>/<path>: <reason>`, then the progress. An input that cannot
be read stops the run before anything is parsed - `nothing rendered: N of 6
inputs could not be read` - because a partial set of inputs can only ever
differ from the pinned frame. A parser's or the skin's complaint about an
input is printed as `issue: <path>: ...` or `issue: skin: ...`; a failure
the viewer would stop on is printed as `nothing rendered: <reason>`. Either
way + exits.

Once the frame is rendered it fills the screen, with these lines along the
bottom edge, below the car, at the default camera:

```text
nfsu_switch_car: the painted stock 240SX from sdmc:/nfsu, CPU reference renderer, 1280x720
render_ms <ms>  prepare_ms <ms>  read_ms <ms>  encode_sha256_ms <ms>
tga_bytes 3686418  sha256 <64 hexadecimal digits>
result: EQUAL to the pinned desktop frame
camera: default, yaw 35.00 pitch 15.00  frame_ms <ms>  render_ms <ms>
Left stick orbits, - resets the camera, + exits.
```

or `result: DIFFERENT from the pinned desktop frame, 3686418 bytes, sha256
52B9...` in its place. The first four lines are issue #141's, with
`painted` or `unpainted` in the first; the last two are replaced every frame.
Once the stick has moved the camera, the `tga_bytes` and `result:` lines
give way to `the hash is compared only at the default camera; - returns to
it`, and the camera line reads `moved`. Pressing - returns the camera to the
default to the bit, renders it again, and hashes and compares that frame
again. `frame_ms` is the length of the previous pass of the frame loop and
`render_ms` the last `render_scene`; the second line's times are the first
frame's - `render_scene` alone, reading the six files, parsing and preparing
the scene with its skin, and encoding and hashing the frame. The times come
from the ARM system counter. stdout also carries, once, the viewer's
stock-slot summary, render statistics and skin lines, in the viewer's own
words, so a console frame that differs can be compared with the desktop
report line by line, then each later comparison's two lines and, at exit,
the number of frames rendered.

### Why it is the same frame

- The same libraries, compiled with the same options.
  `cmake/car_libraries.cmake` declares the car path once for both configures,
  so the console compiles the same source lists with the same
  floating-point flags. Issue #148 moved the skin layers (`nfsu_body_skin`,
  `nfsu_skin_inputs`, `nfsu_runtime_skin`) and the camera (`nfsu_inspect`
  with its floating-point options, `nfsu_input`, `nfsu_input_camera`) into
  that file unchanged, and added `nfsu_stock_view`; every desktop compile
  command stayed byte for byte what it was. The desktop compile commands
  carry no `-std` option, because the desktop GCC 13 defaults to `gnu++17`,
  which meets every target's `cxx_std_17`. devkitA64's GCC 16 defaults to
  `gnu++20`, where `std::filesystem::path::u8string()` returns
  `std::u8string` and `src/texture_export.cpp` stops compiling
  (`src/render.cpp` did too until issue #144), so the console asks for
  `gnu++17` by name on the car-path targets and on `nfsu_switch_car`.
- The same calls. `nfsu_stock_frame` makes the calls `src/viewer_main.cpp`
  makes for the pinned command - the same parsers over the same bytes, the
  same wanted-hash texture decode in the same source order, the same skin
  sources, plan, target path, assembly, composite and binding, the same
  stock assembly, `render_scene` with the default options - and
  `src/stock_frame.cpp` names the viewer step each one mirrors. The viewer
  itself is unchanged. The application renders every frame through
  `nfsu::StockView::render`, which is `render_scene` over the same scene at
  the camera's options; at the default camera those are `RenderOptions{}`
  in every field, which `nfsu::stock_view_is_default` checks exactly. The
  desktop test `nfsu_stock_frame_viewer_match` holds the two together: it
  renders six synthetic files in the game's layout with the viewer and with
  the application's calls, unpainted and painted, and requires the same TGA
  bytes, by CMake's own `file(SHA256)`, the same 13 report lines, and for
  the painted frame the same 4 skin lines as well and a frame that differs
  from the unpainted one.
- The same encoding. The frame is encoded by `nfsu::write_tga` into a
  `std::ostringstream` opened in binary mode, which is what `write_rgba_tga`
  does before it writes the file; `nfsu_stock_frame_tests` compares the
  bytes with the file `write_frame_tga` writes.
- The same answer on the desktop, over the retail files, read-only, through
  exactly the application's calls:

  ```text
  build/nfsu_stock_frame_tests --painted-frame <the game's installation>
  build/nfsu_stock_frame_tests --frame <the game's installation>
  ```

  print `tga_bytes 3686418` and the painted and the unpainted pin
  respectively, in a Debug and in a Release build, with every render, slot
  and skin line the viewer prints for the same frame.
- The session on the desktop. `nfsu_stock_frame_tests` replays a recorded
  controller session over the synthetic car through `nfsu::StockView`: a +
  held since launch that does not exit, the left stick orbiting by the
  model's rate, the right stick and the D-pad doing nothing, a stalled frame
  moving as 100 ms would, - returning to the default camera and to the
  pinned frame's bytes, and + exiting. It also checks the button mapping
  against [`INPUT.md`](INPUT.md)'s table bit by bit, and
  `switch/car_main.cpp` checks the mapping's constants against libnx's own
  enumerators at compile time.

### Floating-point contraction, closed by issue #91

`nfsu_core` and `nfsu_stock_scene` used to compile without
`-ffp-contract=off` on either platform. On AArch64, GCC 16 at `-O2` fused
seven multiply-adds in `src/part_placement.cpp` - three `fmadd`, one `fmla`
and three `fnmsub` - in `derive_cullable_part_placements` and
`multiply_placement_matrix`, which the stock frame uses to place the wheels,
the brakes, the exhausts and the plate; the desktop builds target x86-64
without FMA, so nothing was fused there. A desktop build of that one file
with `-mfma` - GCC 13 then fused six multiply-adds in the same two functions -
gave bit-identical placements and instance transforms and the pinned frame,
so contraction there was not expected to move this frame, but that was a
simulation on another compiler, not the console, and nothing before issue #91
pinned it there either.

Issue #91 closed that gap: `cmake/car_libraries.cmake` now disables
contraction on both targets, on both platforms, the same way the other car
targets do. Recompiling `src/part_placement.cpp` with the exact
`build-switch` command from `compile_commands.json`, `-S` in place of `-c`,
shows the fix at the instruction level: the seven fused instructions above
with the flags as they were, zero `fmadd`/`fmsub`/`fnmadd`/`fnmsub`/`fmla`/
`fmls` with `-ffp-contract=off -fno-fast-math` added. The pinned stock-car
frame (`FD672733...`, since re-pinned by issue #161) was re-checked after
the pin and is unchanged, on the desktop; nothing here has run on a console,
so whether it was ever at risk on one stays unobserved rather than closed.

Issue #148 added the skin layers, the camera, `nfsu_stock_view` and the
orbit loop to the console build. A census of every object in the Switch
tree at that branch, rebased on the #91 pin (`objdump -d`, `fmadd`,
`fmsub`, `fnmadd`, `fnmsub`, `fmla`, `fmls`), finds no fused instruction in
any of them. The unpainted frame of issue #141 was reported `EQUAL` on the
owner's console before the pin. If the console reports the painted frame
`DIFFERENT` while `--unpainted` is `EQUAL`, the skin layers are the place to
look first.

### The left side of the car (issue #161)

Orbiting the painted car of issue #148 on the console, the owner saw the two
wheels on the car's left, at a yaw of about -94, drawn as flat dark squares,
while the right side, at about +76, looked right. The console runs the same
CPU reference code as the desktop, so the desktop viewer reproduces it with
the orbit option added to the painted command above:

```text
nfsu_viewer <Cars/240SX/GEOMETRY.BIN> <Cars/240SX/TEXTURES.BIN> left.tga
    --shared-textures <Global/GLOBALB.BUN> --shared-textures <Cars/TEXTURES.BIN>
    --stock-car 240SX --stock-parts <Global/GLOBALB.BUN>
    --stock-geometry <Cars/BRAKES/GEOMETRY.BIN> --stock-geometry <Cars/PLATES/GEOMETRY.BIN>
    --place-wheels --skin-car 240SX --skin-doorline plain --skin-base 30 60 160 128
    --yaw -94
```

Every other option is at its default, pitch 15 among them. It writes 3686418
bytes and exits 0 with nothing on standard error. "Before" is `9974ff5`,
"after" this change, each built Debug and Release:

| Camera | Build | Before issue #161 | After |
| --- | --- | --- | --- |
| `--yaw -94`, the left side | Debug and Release | `FF2324A7851FFB5FC624C38FB6D5F063EA976868D36BE0A1921302EDC478A7C0` | `1E5FA0BE9F3B876BDA36A3F17B22A17371F54017FF72B285E75AA1E93CA7F897` |
| `--yaw 76`, the right side | Debug | `0DC727BD8CA0B17B3A0290634698254095B88CCDEEBEF954A6373A1E30829A43` | the same |
| `--yaw 76`, the right side | Release | `DF5EC96B728FECF14DC3B5301F647C97B0252B153693AF0FD7F23B8F6E07406D` | the same |

The application's own calls - `prepare_stock_frame` and
`render_stock_frame` from `nfsu_stock_frame`, run from a scratch program
that is not tracked - give the same hashes as the viewer of the same build,
checked for both builds of this change and the Debug build of `9974ff5`.

The two builds disagree at `--yaw 76`, before this change as after it, and
this change does not touch it: 271 pixels near the two right wheels are
drawn by the same triangles of the body's material group 2 in both builds,
blue in one and black in the other, with every report line equal. That is a
difference in how the two builds texture those pixels, outside issue #161.
At the default camera, the only one the console compares, the two builds
agree, and they agree at `--yaw -94`.

**The cause.** The stand-in placed all four wheel copies, and all four
brake copies, as pure translations to the culling placements, determinant
+1, while the retail draw half-turns the wheel model of positions 0 and 3,
the left pair, about the vertical axis and reflects their brake model
laterally, before positioning them. The wheel model is authored for the
right-hand side, with its rim at the lateral-zero edge and a flat backing
quad 0.2 inboard, so each left wheel carried that quad 0.2 outboard of its
rim, in front of it from the left. With the default material state, which
does not apply `240SX_TIRE`'s alpha test, the quad draws opaque: the dark
square. The issue's first guess, a mirrored placement flipping the winding
under back-face culling, does not hold: no placement was mirrored, the
reference culls nothing by default, and the retail draw draws the wheel
two-sided too. [`PART_PLACEMENT.md`](PART_PLACEMENT.md), "Which way the
wheel and brake models face", has the instructions, the multiply order and
the mesh measurements.

**The fix.** Wheel copies 0 and 3 are now `car_wheel_half_turn() *
translation` and brake copies 0 and 3 `car_brake_reflection() *
translation`, which is the retail order with the untraced lateral terms
left out. The copies stay stand-ins in the report, because the lateral
term, the spin and the steering are still not reproduced. From the left
the wheels now look as the right-hand ones always did: the rim and the tyre,
the brake disc and caliper in the hub, and the corners of the backing quad
showing past the tyre where it is not alpha-tested away, as they do on the
right.

**The pins.** At the default camera the front-left wheel's backing quad
showed through as two black pixels, x 931..932, y 385, above the far
fender. A scratch program that maps a pixel's triangle id back to its
group and copy, as the inspector does, names both as material 1 of
`240SX_KIT00_WHEEL_A`, copy 0. With the fix they are background, and no
other pixel of either frame changed, so both pins above were re-pinned:
`FE51E57A...` to `52B9303A...` painted and `FD672733...` to `E7A042A5...`
unpainted. The pinned frames themselves showed the defect.

**Tests.** `nfsu_stock_frame_tests` builds the synthetic car with a wheel
authored the retail way, renders it from yaw -90 and +90 through the
application's calls and requires the pixel on each near-side wheel to be
that wheel's rim, not its backing quad; on the old placement both left
wheels fail. `nfsu_stock_scene_tests` requires the half turn on wheel
copies 0 and 3, the reflection on brake copies 0 and 3 and neither on 1
and 2, and `nfsu_part_placement_tests` the two matrices themselves.

**On the console.** An NRO carries its pin and its placement together: one
built before this change compares the old frame with the old pin, and one
built from it compares the new frame with the new pin, `52B9303A...`
painted and `E7A042A5...` with `--unpainted`. At the default camera the two
frames differ only in the two pixels above, so the screen looks the same
there; orbiting to the left shows wheels on both sides only with the new
NRO.

## What is deliberately not covered yet

- Nothing issue #148 added had run on a console or an emulator when it
  merged. Both applications are checked as files: an AArch64 ELF and a
  well-formed NRO each. That the painted frame is the pinned one there,
  that the stick
  orbits the camera, how fast the frame loop runs and that + exits cleanly
  is what the code and the desktop tests say, not something observed. The
  unpainted frame of issue #141 is the one console result on record, and
  the owner's orbit of the #160 NRO that found issue #161 the one console
  observation of the painted car.
- The issue #161 change has not run on a console. The owner's view
  of the left side came from the NRO of pull request #160; that the NRO
  built from this change shows wheels there and reports its new pin
  `EQUAL` is what the desktop frames say.
- There is no deko3d device. `nfsu_switch_car` draws with the CPU reference
  renderer; neither application loads a shader, and the compiled `.dksh`
  files are not packaged into an NRO's RomFS; how the deko3d renderer carries
  them is its decision.
- Only the car-path libraries are compiled for the console, the skin,
  inspection and controller layers among them since issue #148. Whether
  the track and platform layers build against devkitA64's newlib is
  untested here.
- Controller, filesystem, timing and logging services beyond the frame loop
  (ROADMAP M3) are not started; `nfsu_switch_car` calls libnx directly for
  the little it needs, and reads player 1 and the handheld controls only.
- The orbiting frame is rendered at full size by the CPU reference renderer,
  about ten frames a second at #141's console render time; nothing renders
  a smaller frame while the camera moves, as the desktop window does during
  a drag.
- Only the NRO format is produced; no NSO or ExeFS.
- A Debug Switch build was not run. The Windows (MSYS2) route was run for
  issue #141, as the Prerequisites section says.
- No CI runs the Switch build.
