# Floating-point contraction (issue #175)

Every project library, executable and test inherits one compile policy from
`cmake/floating_point.cmake`, included immediately after `project()` and before
any target or subdirectory. GCC/Clang receive `-fno-fast-math` followed by
`-ffp-contract=off`; MSVC retains `/fp:precise`, the mode used by the previous
target pins;
centralizing contraction does not opt into strict floating-point environment
semantics. No MSVC build was run. This replaces all separate
per-target blocks and covers future targets by directory inheritance, in both
the desktop and Switch configures. The census does not audit external SDK
archives or shader code; they are not CMake-built project objects.

After all Switch targets have been declared, `cmake/switch_fma_census.cmake`
recursively reads CMake's `BUILDSYSTEM_TARGETS` and uses `TARGET_OBJECTS` to
write `switch-objects-Release.txt` (one target/object pair per line). Static,
shared, module and object libraries and executables participate, whether or
not an application links them. Custom packaging targets have no objects.
No hand-maintained target or source list can omit a new library or application.
Stale objects left by a removed target are not objects of this configure.

`nfsu_switch_fma_census` disassembles every manifest object, prints its relative
path and counts, and fails on `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, `fmla` or
`fmls`. Empty/malformed manifests, duplicate or missing objects, objdump errors,
non-AArch64 objects and a census without any `fmul` also fail. Individual
integer-only objects legitimately have zero `fmul`. The guards compile real
AArch64 fixtures with contraction fast and off, require refusal and success
respectively, and check each of the six mnemonics with a separate assembly
fixture. They also exercise empty, malformed, missing, duplicate and invalid
object inputs, an integer-only census, and a valid mixed integer/float
census. None of these fixtures executes on the host. The single-instruction
refusal pattern has a numeric boundary: changing the checker diagnostic from
`1 fused` to `21 fused` makes the guard fail with its expected-refusal error.

## Measured whole-tree census

Base: `a953ebb` (PRs #180 and #190 included), Release, devkitA64 GCC 16.1.0.
Before and after use the same
59 project objects from 45 compiled targets. Before: 23 fused, 1199 `fmul`.
After: 0 fused, 1222 `fmul`. The generated manifest was also compared with an
independent recursive scan of on-disk project objects: exact coverage, with
59/59 compile commands carrying the global flags. The three desktop trees
each had 107/107 compile commands carrying them. All objects, including
applications, are below;
paths are relative to the build tree.

| Object | Fused before | Fused after | fmul before | fmul after |
| --- | ---: | ---: | ---: | ---: |
| `CMakeFiles/nfsu_assets.dir/src/assets.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_body_skin.dir/src/body_skin.o` | 0 | 0 | 18 | 18 |
| `CMakeFiles/nfsu_chase_camera.dir/src/chase_camera.o` | 0 | 0 | 66 | 66 |
| `CMakeFiles/nfsu_core.dir/src/car_parts.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_core.dir/src/chunk.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_core.dir/src/geometry.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_core.dir/src/part_placement.o` | 0 | 0 | 17 | 17 |
| `CMakeFiles/nfsu_core.dir/src/texture.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_det_sqrt.dir/src/det_sqrt.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_draw_commands.dir/src/draw_commands.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_draw_commands.dir/src/draw_execution.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_drive_controls.dir/src/drive_controls.o` | 0 | 0 | 13 | 13 |
| `CMakeFiles/nfsu_drive_session.dir/src/drive_session.o` | 0 | 0 | 40 | 40 |
| `CMakeFiles/nfsu_frame_compare.dir/src/frame_compare.o` | 0 | 0 | 2 | 2 |
| `CMakeFiles/nfsu_gpu_upload.dir/src/gpu_upload.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_gpu_upload.dir/src/memory_budget.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_image.dir/src/image.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_input.dir/src/input.o` | 0 | 0 | 2 | 2 |
| `CMakeFiles/nfsu_input.dir/src/input_recording.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_input_camera.dir/src/input_camera.o` | 0 | 0 | 9 | 9 |
| `CMakeFiles/nfsu_inspect.dir/src/inspect.o` | 0 | 0 | 18 | 18 |
| `CMakeFiles/nfsu_memory.dir/src/memory_arena.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_posix_file_system.dir/src/posix_file_system.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_race_route.dir/src/race_route.o` | 0 | 0 | 76 | 76 |
| `CMakeFiles/nfsu_race_timing.dir/src/race_timing.o` | 0 | 0 | 18 | 18 |
| `CMakeFiles/nfsu_render.dir/src/render.o` | 0 | 0 | 170 | 170 |
| `CMakeFiles/nfsu_runtime_skin.dir/src/runtime_skin.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_scene.dir/src/scene.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_screen_text.dir/src/screen_text.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_sha256.dir/src/sha256.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_skin_inputs.dir/src/skin_inputs.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_stock_frame.dir/src/stock_frame.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_stock_scene.dir/src/stock_scene.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_stock_view.dir/src/stock_view.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_texture_decode.dir/src/texture_decode.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_texture_export.dir/src/texture_export.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_track.dir/src/track.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_track.dir/src/track_collision.o` | 20 | 0 | 18 | 38 |
| `CMakeFiles/nfsu_track.dir/src/track_objects.o` | 3 | 0 | 3 | 6 |
| `CMakeFiles/nfsu_track.dir/src/track_scene.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_track_contact.dir/src/track_contact.o` | 0 | 0 | 81 | 81 |
| `CMakeFiles/nfsu_track_draw_list.dir/src/track_draw_list.o` | 0 | 0 | 0 | 0 |
| `CMakeFiles/nfsu_track_residency.dir/src/track_residency.o` | 0 | 0 | 18 | 18 |
| `CMakeFiles/nfsu_track_visibility.dir/src/track_visibility.o` | 0 | 0 | 71 | 71 |
| `CMakeFiles/nfsu_track_zones.dir/src/track_zones.o` | 0 | 0 | 1 | 1 |
| `CMakeFiles/nfsu_vehicle.dir/src/vehicle.o` | 0 | 0 | 339 | 339 |
| `CMakeFiles/nfsu_vehicle.dir/src/vehicle_script.o` | 0 | 0 | 1 | 1 |
| `CMakeFiles/nfsu_vehicle_track.dir/src/vehicle_track.o` | 0 | 0 | 180 | 180 |
| `CMakeFiles/nfsu_vehicle_track.dir/src/vehicle_track_course.o` | 0 | 0 | 35 | 35 |
| `switch/CMakeFiles/nfsu_switch_app.dir/main.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_assets.dir/assets_main.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_car.dir/car_main.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_compare.dir/compare_main.o` | 0 | 0 | 2 | 2 |
| `switch/CMakeFiles/nfsu_switch_compare.dir/deko_renderer.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_deko3d.dir/deko_main.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_deko3d.dir/deko_renderer.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_drive.dir/drive_main.o` | 0 | 0 | 1 | 1 |
| `switch/CMakeFiles/nfsu_switch_vehicle.dir/vehicle_main.o` | 0 | 0 | 0 | 0 |
| `switch/CMakeFiles/nfsu_switch_vehicle_track.dir/vehicle_track_main.o` | 0 | 0 | 0 | 0 |

## Track slice extension (#54)

The whole-tree census automatically adds `nfsu_track_stream` (one object),
`nfsu_track_slice` (one object) and `nfsu_switch_track` (two objects).
The earlier before/after table remains the historical #175 measurement.
The #54 Release build has 49 targets and 64 objects, with zero fused
instructions; its generated object manifest is the authoritative current
inventory. No per-target flag or additional census was added.

## Deliberate break and restore

With the global rule present, temporarily append this after the declaration
of `nfsu_track` in `cmake/draw_libraries.cmake`:

```cmake
if(NINTENDO_SWITCH)
    target_compile_options(nfsu_track PRIVATE -ffp-contract=fast)
endif()
```

`cmake --build build-ascii-switch` succeeds (exit 0), but
`ctest --test-dir build-ascii-switch -V -R '^nfsu_switch_fma_census$'` fails
(exit 8). Its actual diagnostic is:

```text
-- census: 45 targets, 59 objects, 1199 fmul, 23 fused
23 fused multiply-add instruction(s); every target must disable contraction
```

`track_collision.o` has 20 fused instructions and `track_objects.o` has 3.
Restore the CMake file byte for byte, touch it, and rebuild: the same census
reports `45 targets, 59 objects, 1222 fmul, 0 fused` and passes. This tests the
actual whole-tree CTest entry against a project target built fast, separately
from the small fast/off fixtures in the guards.

## Desktop pins and gates

With MinGW GCC 13.2, default, Debug and Release full CTest each pass 68/68,
including their pinned
fixture frames, telemetry and replay outputs. Retail replays also reproduce
the existing documented hashes: six car frames (default, shared global,
subset, stock, placed wheels, painted), painted yaw 76, two track-region
orders, whole-track street and overview, and all twelve streamed lap frames.
The five TRACK_PROBE stdout pins, including `BA2565408...`, and all four
retail draw-command frame/state pins are unchanged in all three configures.
No hash or expected value is re-pinned for this issue. Each retail invocation
has a before/after full manifest (476 paths, sizes, mtimes and SHA-256) and
unchanged inputs; prior output artifacts are removed by exact name first.

The path-argument gate reports 33 probe commands and 72 path positions, plus
3 viewer positional arguments and 5 viewer path options. The Switch tree
builds without warnings and passes its full CTest (15/15), including the single
whole-tree census and the census guards. These are instruction/build checks;
no console frame or telemetry equality is newly claimed by this issue.

Run desktop gates with MinGW on PATH, for each build directory. The measured
runs use an ASCII checkout path. Smoke runs from the actual Cyrillic workspace
path exposed existing fixture filename-conversion failures on desktop and a
Switch RomFS packaging failure; the unchanged `a953ebb` baseline also fails
that packaging step there. These path limitations are outside issue #175.
All automation derives the checkout from its working directory.

Commands:

```text
cmake -S . -B build-ascii -G Ninja -DCMAKE_BUILD_TYPE=
cmake --build build-ascii
ctest --test-dir build-ascii --output-on-failure
cmake -S . -B build-ascii-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ascii-debug
ctest --test-dir build-ascii-debug --output-on-failure
cmake -S . -B build-ascii-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ascii-release
ctest --test-dir build-ascii-release --output-on-failure
```

In the MSYS2 environment described by [SWITCH_BUILD.md](SWITCH_BUILD.md):

```text
cmake -S . -B build-ascii-switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-ascii-switch
ctest --test-dir build-ascii-switch --output-on-failure
ctest --test-dir build-ascii-switch -V -R fma_census
```

## Switch race extension (#196)

The global policy also covers `nfsu_race_events`, `nfsu_race_run` (two
objects) and `nfsu_switch_race` (two objects). The current Release census
has 52 targets and 69 objects, with zero fused instructions. No local flag
pins or separate census are introduced.
