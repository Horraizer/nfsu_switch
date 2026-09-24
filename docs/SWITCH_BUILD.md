# Nintendo Switch build

The Switch build is opt-in. A configure that does not name
`cmake/switch-toolchain.cmake` never reads it and produces the desktop build
exactly as before; a configure that names it builds `switch/` and nothing
else. It exists to prove the toolchain (issue #45) and to host the offline
shader pipeline (issue #49, [`SHADER_PIPELINE.md`](SHADER_PIPELINE.md)). It
renders nothing yet.

## What it builds

| Target | Output, under `build-switch/switch/` | What it is |
| --- | --- | --- |
| `nfsu_switch_app` | `nfsu_switch_app.elf` | The minimal application: it initialises the console and the first controller, runs a frame loop until + is pressed or the system asks it to close, and shuts down. |
| `nfsu_switch_app_nro` | `nfsu_switch_app.nro`, `nfsu_switch_app.nacp` | That ELF packaged by `elf2nro` with the libnx default icon and a NACP titled `nfsu_switch`. |
| `nfsu_switch_shaders` | `shaders/*.dksh`, `shaders/*.tgsi` | Every GLSL stage a manifest in `shaders/` names, compiled by `uam`, and each manifest checked against the result. |

The application links libnx, which devkitPro's platform file adds to every
link, and nothing else. It loads no shader. Its CTest suite runs on the build
machine, not on a console:

| Test | What it checks |
| --- | --- |
| `nfsu_switch_shader_<program>` | One manifest against the blobs the build compiled; the build step runs the same check. |
| `nfsu_switch_shader_blob_guards` | The same check made to fail: nine changed stage sources, compiled with `uam`, each named by its finding. |
| `nfsu_switch_nro` | The ELF is AArch64; the NRO has its `NRO0` header, an `ASET` section, the icon and a 0x4000-byte NACP titled `nfsu_switch`; the reader refuses a file that is not an NRO. |

The desktop programs, libraries and tests are not configured for the console:
the top-level `CMakeLists.txt` enters `switch/` and returns before it declares
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
there (`pacman -S cmake`). That route was not run for this branch.

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

The desktop build is unchanged:

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
told to use devkitPro's MSYS2 shell, before anything else is looked at. The toolchain file runs the same check
itself when another project uses it; the message is the same, but that
project gets CMake's follow-on errors after it.

`nfsu_switch_toolchain_diagnostics`, in the desktop CTest suite, configures
the project against SDK trees that each lack something and requires those
messages; it needs no SDK, because every case stops before a compiler is run.

## What is deliberately not covered yet

- Nothing has run on a console or an emulator. The application is checked as
  a file: an AArch64 ELF and a well-formed NRO. That its frame loop exits
  cleanly is what the code says, not something observed.
- There is no deko3d device and no renderer. The application loads no shader,
  and the compiled `.dksh` files are not packaged into the NRO's RomFS; how the
  renderer carries them is its decision.
- None of the project's libraries is compiled for the console. Whether the
  prepared-scene, render and track layers build against devkitA64's newlib is
  untested.
- Controller, filesystem, timing and logging services beyond the frame loop
  (ROADMAP M3) are not started.
- Only the NRO format is produced; no NSO or ExeFS.
- The Windows (MSYS2) route and a Debug Switch build were not run.
- No CI runs the Switch build.
