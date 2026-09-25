cmake_minimum_required(VERSION 3.20)

# The Nintendo Switch build is opt-in: -DCMAKE_TOOLCHAIN_FILE=
# cmake/switch-toolchain.cmake (docs/SWITCH_BUILD.md). Its SDK is checked
# here, before project() reads that file, so a missing tool is reported by
# name and not by the errors CMake raises after a toolchain file has failed.
if(CMAKE_TOOLCHAIN_FILE MATCHES "switch-toolchain[.]cmake$")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/switch-sdk.cmake)
    nfsu_switch_sdk_locate(NFSU_SWITCH_SDK_PROBLEM)
    if(NOT NFSU_SWITCH_SDK_PROBLEM STREQUAL "")
        message(FATAL_ERROR "${NFSU_SWITCH_SDK_PROBLEM}")
    endif()
endif()

project(nfsu_switch LANGUAGES CXX)

option(NFSU_BUILD_TESTS "Build unit tests" ON)

function(nfsu_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# The Switch build configures switch/ and nothing below this point: the
# desktop programs and their tests are never built for the console, and a
# desktop configure never enters this branch.
if(NINTENDO_SWITCH)
    if(NFSU_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(switch)
    return()
endif()

add_library(nfsu_image
    src/image.cpp
)

target_include_directories(nfsu_image
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_image PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_image)

# One command-line argument turned into a filesystem path. It is the only
# file outside the window backend that calls the Windows API, and it is
# separate from the parsers for that reason: nothing under nfsu_core knows
# what an operating system is. Both programs link it, because both read
# paths out of a narrow argv and both used to terminate on one that the
# active ANSI code page spelled with a byte above 0x7E.
add_library(nfsu_arg_path
    src/arg_path.cpp
)

target_include_directories(nfsu_arg_path
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_arg_path PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_arg_path)

add_library(nfsu_core
    src/car_parts.cpp
    src/chunk.cpp
    src/geometry.cpp
    src/part_placement.cpp
    src/texture.cpp
)

target_include_directories(nfsu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nfsu_core PUBLIC cxx_std_17)
target_link_libraries(nfsu_core PUBLIC nfsu_image)
nfsu_enable_warnings(nfsu_core)

# Backend-neutral prepared car scene: mesh selection and material binding.
# Its public data model depends only on in-memory mesh types.
add_library(nfsu_scene
    src/scene.cpp
)

target_include_directories(nfsu_scene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_scene PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_scene)

# One explicitly selected, bounded track STREAM region: chunk-tree parsing
# of the 24-byte track vertex layout, the scenery sections that place that
# geometry, and the prepared draw list with its cost statistics. The
# parsers (src/track.cpp and src/track_objects.cpp) use only nfsu_core; the
# prepared region (src/track_scene.cpp) also uses the backend-neutral
# scene layer for material binding. src/track_collision.cpp is the
# static collision of one track, which is a separate container in a
# separate file and shares nothing with the render path but the chunk
# grammar and the coordinate frame.
add_library(nfsu_track
    src/track.cpp
    src/track_collision.cpp
    src/track_objects.cpp
    src/track_scene.cpp
)

target_link_libraries(nfsu_track PUBLIC nfsu_core nfsu_scene)
nfsu_enable_warnings(nfsu_track)

# Which scenery instances a camera can see: the recovered frustum test, the
# bounding-tree traversal it drives and the explicit fallbacks. It is its
# own target so that floating-point contraction can be disabled here without
# changing how the parsers beside it are compiled: every decision this
# library makes is a strict comparison of a plane distance against zero, so
# a fused multiply-add would move a boundary case and the visible set with
# it. See docs/TRACK_VISIBILITY.md.
add_library(nfsu_track_visibility
    src/track_visibility.cpp
)

target_link_libraries(nfsu_track_visibility PUBLIC nfsu_track)
nfsu_enable_warnings(nfsu_track_visibility)
if(MSVC)
    target_compile_options(nfsu_track_visibility PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_track_visibility PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The visibility zones of one track: the 0x80034150 records that turn a
# camera position into the section list the group layer draws. It sits above
# nfsu_track_visibility because a decoded zone is handed straight to that
# library's TrackResidency. Floating-point contraction is disabled for the
# same reason it is disabled there: the crossing test at speed.exe
# 0x005679B0 decides on a strict comparison of an interpolated coordinate,
# so a fused multiply-add would move a boundary case and the selected zone
# with it. See docs/TRACK_ZONES.md.
add_library(nfsu_track_zones
    src/track_zones.cpp
)

target_link_libraries(nfsu_track_zones PUBLIC nfsu_track_visibility)
nfsu_enable_warnings(nfsu_track_zones)
if(MSVC)
    target_compile_options(nfsu_track_zones PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_track_zones PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The composition layer of the track path: the prepared region of
# src/track_scene.cpp placed by the scenery instances of
# src/track_objects.cpp, and the per-frame draw list one visible set selects
# out of the two. It decodes nothing and opens no file. It links the culler
# because build_track_draw_list reads a TrackCullReport; a render backend
# links this and never reaches past it to the culler or the parsers. See
# docs/TRACK_DRAW_LIST.md.
#
# No floating-point flags: this library makes no decision from an
# arithmetic result. It copies the transform track_instance_transform
# returns and totals integer counts, so there is nothing here for a fused
# multiply-add to move. nfsu_track_visibility, where that is not true,
# carries its own.
add_library(nfsu_track_draw_list
    src/track_draw_list.cpp
)

target_link_libraries(nfsu_track_draw_list PUBLIC nfsu_track_visibility)
nfsu_enable_warnings(nfsu_track_draw_list)

# The CPU reference renderer's track backend: one PreparedTrackScene and one
# TrackDrawList drawn with the same Rasterizer, camera and clipper the car
# path uses, plus the automatic framing camera a region needs and the world
# bounds it is fitted to. It is declared after nfsu_render and
# nfsu_track_draw_list because it is a consumer of both and adds no third
# source of truth: src/render.cpp is not modified by the commit that adds
# this target, which is what keeps the car path's reference frames identical
# by construction. See docs/TRACK_RENDER.md.
#
# Floating-point contraction is disabled for the same reason nfsu_render
# disables it: every frame this library produces is compared byte for byte
# against another build, and a fused multiply-add in the placement or the
# framing would move a vertex by an ulp and a coverage boundary with it.
add_library(nfsu_track_render
    src/track_render.cpp
)

target_link_libraries(nfsu_track_render PUBLIC nfsu_render nfsu_track_draw_list)
nfsu_enable_warnings(nfsu_track_render)
if(MSVC)
    target_compile_options(nfsu_track_render PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_track_render PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The stock-car profile of PreparedCarScene: the recovered stock part set,
# its per-LOD mesh hashes and the recovered placement, turned into one
# prepared draw list with an instance per drawn copy. It reads no files, so
# it sits above the scene layer and beside the parsers it consumes.
add_library(nfsu_stock_scene
    src/stock_scene.cpp
)

target_link_libraries(nfsu_stock_scene PUBLIC nfsu_core nfsu_scene)
nfsu_enable_warnings(nfsu_stock_scene)

# Texture-pack decoding for prepared scenes. Backends do not depend on it.
add_library(nfsu_texture_decode
    src/texture_decode.cpp
)

target_link_libraries(nfsu_texture_decode PUBLIC nfsu_scene nfsu_core)
nfsu_enable_warnings(nfsu_texture_decode)

# Body-skin pixel compositor reference. It depends on nothing but the
# standard library, and it disables floating-point contraction so that the
# traced truncation behaviour does not depend on FMA availability.
add_library(nfsu_body_skin
    src/body_skin.cpp
)

target_include_directories(nfsu_body_skin
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_body_skin PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_body_skin)
if(MSVC)
    target_compile_options(nfsu_body_skin PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_body_skin PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Body-skin input assembly: turns an explicit customization selection and
# parsed texture packs into an owned BodySkinRequest. It sits between the
# pack reader and the compositor and depends on both.
add_library(nfsu_skin_inputs
    src/skin_inputs.cpp
)

target_link_libraries(nfsu_skin_inputs PUBLIC nfsu_core nfsu_body_skin)
nfsu_enable_warnings(nfsu_skin_inputs)

# Runtime body skin: selects the compositor path from the real target
# record, runs the proven composite over assembled inputs and binds the
# generated texture into a decoded texture set. It parses no packs of its
# own and does no floating-point work; the arithmetic lives in
# nfsu_body_skin, which carries its own flags.
add_library(nfsu_runtime_skin
    src/runtime_skin.cpp
)

target_link_libraries(nfsu_runtime_skin PUBLIC nfsu_skin_inputs nfsu_scene)
nfsu_enable_warnings(nfsu_runtime_skin)

# Deterministic CPU reference renderer. Floating-point contraction is
# disabled so that results do not depend on FMA availability.
add_library(nfsu_render
    src/render.cpp
)

target_link_libraries(nfsu_render PUBLIC nfsu_scene nfsu_image)
# The POSIX frame writer rolls a failed write back with
# rollback_created_files, which nfsu_core defines (src/texture.cpp). The link
# is private because render.h exposes nothing of nfsu_core. The Windows
# writer removes by handle and never calls it, which is why only a POSIX
# build found the link missing (issue #132).
target_link_libraries(nfsu_render PRIVATE nfsu_core)
nfsu_enable_warnings(nfsu_render)
if(MSVC)
    target_compile_options(nfsu_render PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_render PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The read-only inspector. It is declared after every library it uses.
add_executable(nfsu_probe
    src/main.cpp
)

# nfsu_track_render is what `track-render` needs, and it is the only thing
# in this list that writes a file. It carries nfsu_render and
# nfsu_track_draw_list with it, so the two are not named again.
target_link_libraries(nfsu_probe
    PRIVATE nfsu_core nfsu_track nfsu_track_visibility nfsu_track_zones
            nfsu_track_draw_list nfsu_track_render
            nfsu_texture_decode nfsu_skin_inputs nfsu_stock_scene
            nfsu_arg_path)
nfsu_enable_warnings(nfsu_probe)

# Backend-neutral interactive inspection state: the camera the pointer
# drives, the selected material group, the diagnostic views and the overlay
# text. It opens no window and calls no operating system, so it is tested
# headlessly and a deko3d backend reuses it unchanged.
add_library(nfsu_inspect
    src/inspect.cpp
)

# Backend-neutral is about the windowing API, not about this link: the
# state is declared in terms of RenderOptions, RenderLimits and OrbitCamera,
# which live in nfsu_render, so the library carries a link-time dependency on
# the CPU rasterizer even though it never calls render_scene. A deko3d
# backend links it and submits its own frames; moving those three types into
# a smaller target is the only thing that would drop the dependency, and
# nothing needs that yet.
target_link_libraries(nfsu_inspect PUBLIC nfsu_render)
nfsu_enable_warnings(nfsu_inspect)

# The desktop window that drives it. Exactly one backend is compiled: the
# Win32 one, which needs nothing but user32 and gdi32, or a stub that fails
# closed everywhere else. This is the only file in the project that talks to
# a windowing API. src/inspect_window_common.cpp is the arithmetic every
# backend shares and calls no operating system, so it is always compiled.
if(WIN32)
    set(NFSU_INSPECT_WINDOW_SOURCE src/inspect_window_win32.cpp)
else()
    set(NFSU_INSPECT_WINDOW_SOURCE src/inspect_window_none.cpp)
endif()

add_library(nfsu_inspect_window
    src/inspect_window_common.cpp
    ${NFSU_INSPECT_WINDOW_SOURCE}
)

target_link_libraries(nfsu_inspect_window PUBLIC nfsu_inspect)
if(WIN32)
    target_link_libraries(nfsu_inspect_window PRIVATE user32 gdi32)
endif()
nfsu_enable_warnings(nfsu_inspect_window)

add_executable(nfsu_viewer
    src/viewer_main.cpp
)

# nfsu_stock_scene is what --stock-car needs: the part database, the car
# type table and the placement the stock-car profile is built from. The
# renderer consumes the instances that profile produces, so the viewer is
# the first program that links both.
target_link_libraries(nfsu_viewer
    PRIVATE nfsu_render nfsu_texture_decode nfsu_runtime_skin nfsu_inspect_window
            nfsu_stock_scene nfsu_arg_path)
nfsu_enable_warnings(nfsu_viewer)

if(NFSU_BUILD_TESTS)
    include(CTest)

    add_executable(nfsu_chunk_tests
        tests/chunk_tests.cpp
    )

    target_link_libraries(nfsu_chunk_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_chunk_tests)
    add_test(NAME nfsu_chunk_tests COMMAND nfsu_chunk_tests)

    # The narrow-argument-to-path conversion, against stated code pages
    # rather than the one the machine running the test happens to use, and
    # once against the real filesystem so that a conversion that produces
    # the right characters and still cannot open a file is not mistaken for
    # a working one.
    add_executable(nfsu_arg_path_tests
        tests/arg_path_tests.cpp
    )

    target_link_libraries(nfsu_arg_path_tests PRIVATE nfsu_arg_path)
    nfsu_enable_warnings(nfsu_arg_path_tests)
    add_test(NAME nfsu_arg_path_tests COMMAND nfsu_arg_path_tests)

    add_executable(nfsu_geometry_tests
        tests/geometry_tests.cpp
    )

    target_link_libraries(nfsu_geometry_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_geometry_tests)
    add_test(NAME nfsu_geometry_tests COMMAND nfsu_geometry_tests)

    add_executable(nfsu_texture_tests
        tests/texture_tests.cpp
    )

    target_link_libraries(nfsu_texture_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_texture_tests)
    add_test(NAME nfsu_texture_tests COMMAND nfsu_texture_tests)

    add_executable(nfsu_render_tests
        tests/render_tests.cpp
    )

    target_link_libraries(nfsu_render_tests PRIVATE nfsu_render nfsu_texture_decode)
    nfsu_enable_warnings(nfsu_render_tests)
    add_test(NAME nfsu_render_tests COMMAND nfsu_render_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(nfsu_scene_tests
        tests/scene_tests.cpp
    )

    target_link_libraries(nfsu_scene_tests PRIVATE nfsu_render)
    nfsu_enable_warnings(nfsu_scene_tests)
    add_test(NAME nfsu_scene_tests COMMAND nfsu_scene_tests)

    add_executable(nfsu_car_parts_tests
        tests/car_parts_tests.cpp
    )

    target_link_libraries(nfsu_car_parts_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_car_parts_tests)
    add_test(NAME nfsu_car_parts_tests COMMAND nfsu_car_parts_tests)

    add_executable(nfsu_stock_scene_tests
        tests/stock_scene_tests.cpp
    )

    target_link_libraries(nfsu_stock_scene_tests PRIVATE nfsu_stock_scene)
    nfsu_enable_warnings(nfsu_stock_scene_tests)
    add_test(NAME nfsu_stock_scene_tests COMMAND nfsu_stock_scene_tests)

    add_executable(nfsu_track_tests
        tests/track_tests.cpp
    )

    target_link_libraries(nfsu_track_tests PRIVATE nfsu_track nfsu_texture_decode)
    nfsu_enable_warnings(nfsu_track_tests)
    add_test(NAME nfsu_track_tests COMMAND nfsu_track_tests)

    add_executable(nfsu_track_collision_tests
        tests/track_collision_tests.cpp
    )

    target_link_libraries(nfsu_track_collision_tests PRIVATE nfsu_track)
    nfsu_enable_warnings(nfsu_track_collision_tests)
    add_test(NAME nfsu_track_collision_tests COMMAND nfsu_track_collision_tests)

    add_executable(nfsu_track_object_tests
        tests/track_object_tests.cpp
    )

    target_link_libraries(nfsu_track_object_tests PRIVATE nfsu_track)
    nfsu_enable_warnings(nfsu_track_object_tests)
    add_test(NAME nfsu_track_object_tests COMMAND nfsu_track_object_tests)

    add_executable(nfsu_track_visibility_tests
        tests/track_visibility_tests.cpp
    )

    target_link_libraries(nfsu_track_visibility_tests PRIVATE nfsu_track_visibility)
    nfsu_enable_warnings(nfsu_track_visibility_tests)
    add_test(NAME nfsu_track_visibility_tests COMMAND nfsu_track_visibility_tests)

    add_executable(nfsu_track_zone_tests
        tests/track_zone_tests.cpp
    )

    target_link_libraries(nfsu_track_zone_tests PRIVATE nfsu_track_zones)
    nfsu_enable_warnings(nfsu_track_zone_tests)
    if(MSVC)
        target_compile_options(nfsu_track_zone_tests PRIVATE /fp:precise)
    else()
        target_compile_options(nfsu_track_zone_tests PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    add_test(NAME nfsu_track_zone_tests COMMAND nfsu_track_zone_tests)

    add_executable(nfsu_track_draw_list_tests
        tests/track_draw_list_tests.cpp
    )

    target_link_libraries(nfsu_track_draw_list_tests
        PRIVATE nfsu_track_draw_list)
    nfsu_enable_warnings(nfsu_track_draw_list_tests)
    add_test(NAME nfsu_track_draw_list_tests COMMAND nfsu_track_draw_list_tests)

    # The track backend of the CPU reference renderer: the placement, the
    # automatic framing camera, and the frame one draw list produces. Its
    # fixtures go through the real parsers over byte images at absolute
    # offsets, so the vertex field order, the int16 instance matrix and the
    # material tiling rule are exercised rather than assumed.
    add_executable(nfsu_track_render_tests
        tests/track_render_tests.cpp
    )

    target_link_libraries(nfsu_track_render_tests PRIVATE nfsu_track_render)
    nfsu_enable_warnings(nfsu_track_render_tests)
    add_test(NAME nfsu_track_render_tests COMMAND nfsu_track_render_tests)

    add_executable(nfsu_part_placement_tests
        tests/part_placement_tests.cpp
    )

    target_link_libraries(nfsu_part_placement_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_part_placement_tests)
    add_test(NAME nfsu_part_placement_tests COMMAND nfsu_part_placement_tests)

    add_executable(nfsu_body_skin_tests
        tests/body_skin_tests.cpp
    )

    target_link_libraries(nfsu_body_skin_tests PRIVATE nfsu_body_skin)
    nfsu_enable_warnings(nfsu_body_skin_tests)
    add_test(NAME nfsu_body_skin_tests COMMAND nfsu_body_skin_tests)

    add_executable(nfsu_skin_inputs_tests
        tests/skin_inputs_tests.cpp
    )

    target_link_libraries(nfsu_skin_inputs_tests PRIVATE nfsu_skin_inputs)
    nfsu_enable_warnings(nfsu_skin_inputs_tests)
    add_test(NAME nfsu_skin_inputs_tests COMMAND nfsu_skin_inputs_tests)

    add_executable(nfsu_runtime_skin_tests
        tests/runtime_skin_tests.cpp
    )

    target_link_libraries(nfsu_runtime_skin_tests
        PRIVATE nfsu_runtime_skin nfsu_texture_decode)
    nfsu_enable_warnings(nfsu_runtime_skin_tests)
    add_test(NAME nfsu_runtime_skin_tests COMMAND nfsu_runtime_skin_tests)

    # The inspection state, headlessly: the id table against frames the CPU
    # renderer actually produced, the camera under hostile input, the
    # commands, picking, the diagnostic views and the overlay text. It links
    # the window backend too, but only to check that it refuses what it
    # should; no test opens a window, which would wait for a person.
    add_executable(nfsu_inspect_tests
        tests/inspect_tests.cpp
    )

    target_link_libraries(nfsu_inspect_tests PRIVATE nfsu_inspect_window)
    nfsu_enable_warnings(nfsu_inspect_tests)
    add_test(NAME nfsu_inspect_tests COMMAND nfsu_inspect_tests)

    # The viewer refuses a --skin-dump that names the screenshot before it
    # reads an input or writes a byte, so the refusal can be exercised with
    # no input files at all. Two spellings of one path that does not exist
    # is the case that used to get through: the dump is written before the
    # render, so it would be published at the screenshot's path and the run
    # would then fail. The dot-relative spelling is the half that behaves
    # the same everywhere; the case-alias half depends on the filesystem and
    # is checked in nfsu_render_tests against a measured answer.
    add_test(NAME nfsu_viewer_dump_alias
        COMMAND nfsu_viewer missing_geometry.bin missing_textures.bin
                nfsu_dump_alias_probe.tga --skin-car PROBE
                --skin-dump ./nfsu_dump_alias_probe.tga
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    # PASS_REGULAR_EXPRESSION makes CTest ignore the exit code, so the
    # message alone would also pass for a viewer that printed the refusal
    # and then carried on. FAIL_REGULAR_EXPRESSION is checked first and
    # holds the other half: these are what the run prints once it gets past
    # the guard and starts reading the inputs that do not exist.
    set_tests_properties(nfsu_viewer_dump_alias PROPERTIES
        PASS_REGULAR_EXPRESSION "--skin-dump must differ from the output file"
        FAIL_REGULAR_EXPRESSION "failed to read file size;no valid meshes")

    # The two stock-car usage guards, both decided before any file is
    # opened, so they need no inputs at all. A --stock-* option without
    # --stock-car would otherwise be silently ignored, and a mesh selector
    # with --stock-car would ask for two profiles in one frame. The
    # FAIL_REGULAR_EXPRESSION half is what a run prints once it gets past
    # the guard and starts reading the inputs that do not exist, so a
    # viewer that printed the refusal and carried on would still fail.
    add_test(NAME nfsu_viewer_stock_needs_car
        COMMAND nfsu_viewer missing_geometry.bin missing_textures.bin
                nfsu_stock_guard_probe.tga --place-wheels
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_viewer_stock_needs_car PROPERTIES
        PASS_REGULAR_EXPRESSION "--place-wheels need --stock-car NAME"
        FAIL_REGULAR_EXPRESSION "failed to read file size;no valid meshes")

    add_test(NAME nfsu_viewer_stock_excludes_selectors
        COMMAND nfsu_viewer missing_geometry.bin missing_textures.bin
                nfsu_stock_guard_probe.tga --stock-car 240SX
                --stock-parts missing_globalb.bun --mesh-index 0
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_viewer_stock_excludes_selectors PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot be combined with --mesh-index"
        FAIL_REGULAR_EXPRESSION "failed to read file size;no valid meshes")

    # The two `track-render` output guards, both decided before any input is
    # opened, so they need no inputs at all. They are the only tests that run
    # that command's body: like every other nfsu_probe command, the rest of it
    # is exercised only by the read-only retail acceptance in
    # docs/TRACK_RENDER.md. What they pin is the half that must never regress
    # quietly - the command writes a file, and a missing or replaced output is
    # data loss rather than a wrong number.
    #
    # The FAIL_REGULAR_EXPRESSION half is what a run prints once it is past
    # the guard and reading the inputs that are not there, so a command that
    # printed the refusal and carried on would still fail.
    add_test(NAME nfsu_probe_track_render_needs_out
        COMMAND nfsu_probe track-render missing_stream.bun 0 16 0 16
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_probe_track_render_needs_out PROPERTIES
        PASS_REGULAR_EXPRESSION "--out <FILE> is required"
        FAIL_REGULAR_EXPRESSION "failed to read file size")

    # $<TARGET_FILE:nfsu_probe> is passed as the output path on purpose: it
    # is a file that certainly exists next to the working directory, so the
    # refusal is about the target already being there and not about a path
    # the test had to create.
    add_test(NAME nfsu_probe_track_render_keeps_its_output
        COMMAND nfsu_probe track-render missing_stream.bun 0 16 0 16
                --out $<TARGET_FILE:nfsu_probe>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_probe_track_render_keeps_its_output PROPERTIES
        PASS_REGULAR_EXPRESSION "already exists; this command never replaces a file"
        FAIL_REGULAR_EXPRESSION "failed to read file size")

    # What track-render refuses, what it writes and what it exits with, over
    # 36 cases that need no input files. It is a script rather than more
    # add_test entries because PASS_REGULAR_EXPRESSION makes CTest ignore the
    # exit status, and the status is half of what is pinned here: the command
    # survivor that mattered most turned a failing run into exit 0, which
    # changes no printed byte. tests/track_render_guards.cmake says why it
    # exists and what each case is for.
    add_test(NAME nfsu_probe_track_render_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_render_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # nfsu_probe registers a command in one place, and print_usage() composes
    # the usage line out of the name main() dispatches on. This reads the
    # command inventory back out of three independent places - the kCommands
    # table in the source, the built program's usage text, and the README's
    # run lines - and requires all three to be the same set, so a command
    # added to one of them alone is named as missing from the others. It then
    # checks the shape of main(), because a command dispatched from anywhere
    # else is missing from all three inventories at once and leaves them
    # agreeing. That second half catches the accident it was built for - a
    # handler merged into main() after the dispatch loop - and is not an
    # adversarial barrier; tests/usage_commands.cmake names the shapes that
    # get past it.
    add_test(NAME nfsu_probe_usage_commands
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
                -DREADME=${CMAKE_CURRENT_SOURCE_DIR}/README.md
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/usage_commands.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # What both programs do with a path argument that is not plain ASCII:
    # the exit status, the diagnostic, and the absence of the terminate
    # that issue #87 reported. A unit test cannot reach any of those three,
    # because the defect was an exception escaping main().
    add_test(NAME nfsu_path_arguments
        COMMAND ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:nfsu_viewer>
                -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DMAIN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
                -DVIEWER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/viewer_main.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/path_arguments.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # A hung test is killed and reported as a failure.
    set_tests_properties(
        nfsu_arg_path_tests nfsu_path_arguments
        nfsu_chunk_tests nfsu_geometry_tests nfsu_texture_tests
        nfsu_render_tests nfsu_scene_tests nfsu_car_parts_tests
        nfsu_track_tests nfsu_track_object_tests
        nfsu_track_visibility_tests nfsu_track_zone_tests
        nfsu_track_draw_list_tests nfsu_track_render_tests
        nfsu_part_placement_tests nfsu_body_skin_tests
        nfsu_stock_scene_tests
        nfsu_skin_inputs_tests nfsu_runtime_skin_tests nfsu_inspect_tests
        nfsu_viewer_dump_alias nfsu_viewer_stock_needs_car
        nfsu_viewer_stock_excludes_selectors nfsu_probe_usage_commands
        nfsu_probe_track_render_needs_out
        nfsu_probe_track_render_keeps_its_output
        nfsu_probe_track_render_guards
        PROPERTIES TIMEOUT 60)
endif()

# Memory arenas and GPU upload bookkeeping for the Switch renderer, issues #52
# and #50. Appended as one block, after the existing tests, so that the lists
# above are not reordered.
#
# nfsu_memory is the bounded offset allocator of include/nfsu/memory_arena.h:
# the four memory categories, their arenas and the plan that lays them out.
# It depends on nothing but the standard library, because it manages ranges
# rather than memory, so the CPU categories and the GPU one share it. See
# docs/MEMORY_BUDGETS.md.
add_library(nfsu_memory
    src/memory_arena.cpp
)

target_include_directories(nfsu_memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_memory PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_memory)

# What a prepared scene uploads, which texture formats may be uploaded, the
# ledger that tracks every placement until the GPU is done with it, and the
# budgets derived from the same structures. It reads PreparedCarScene and
# PreparedTrackScene and nothing below them, so it links the draw-list layer
# a backend links and never reaches past it. It calls no deko3d function;
# see docs/GPU_UPLOAD.md for the half that needs hardware.
add_library(nfsu_gpu_upload
    src/gpu_upload.cpp
    src/memory_budget.cpp
)

target_link_libraries(nfsu_gpu_upload PUBLIC nfsu_memory nfsu_track_draw_list)
nfsu_enable_warnings(nfsu_gpu_upload)

if(NFSU_BUILD_TESTS)
    # The arenas alone: exact refusal boundaries, the fragmentation figures
    # against an independent model, and thousands of load and release cycles
    # that must end where they started.
    add_executable(nfsu_memory_arena_tests
        tests/memory_arena_tests.cpp
    )

    target_link_libraries(nfsu_memory_arena_tests PRIVATE nfsu_memory)
    nfsu_enable_warnings(nfsu_memory_arena_tests)
    add_test(NAME nfsu_memory_arena_tests COMMAND nfsu_memory_arena_tests)

    # The tables over scenes built by the real preparation code, the format
    # gate against the real decoder, and the ledger's lifetime rules. It links
    # nfsu_runtime_skin to bind a generated skin the way the viewer does, and
    # nfsu_texture_decode for decode_level.
    add_executable(nfsu_gpu_upload_tests
        tests/gpu_upload_tests.cpp
    )

    target_link_libraries(nfsu_gpu_upload_tests
        PRIVATE nfsu_gpu_upload nfsu_runtime_skin nfsu_texture_decode)
    nfsu_enable_warnings(nfsu_gpu_upload_tests)
    add_test(NAME nfsu_gpu_upload_tests COMMAND nfsu_gpu_upload_tests)

    # A hung test is killed and reported as a failure, as above.
    set_tests_properties(nfsu_memory_arena_tests nfsu_gpu_upload_tests
        PROPERTIES TIMEOUT 60)
endif()

# The shader pipeline's checks that need no Switch SDK. The manifests in
# shaders/ state each program's interface; docs/SHADER_PIPELINE.md says what
# every check catches and what none of them can. The check against the
# compiled shaders needs uam and runs in the Switch build, in switch/.
if(NFSU_BUILD_TESTS)
    file(GLOB NFSU_SHADER_MANIFESTS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.manifest)
    set(NFSU_SHADER_MANIFEST_HEADER
        ${CMAKE_CURRENT_BINARY_DIR}/generated/nfsu/shader_manifest.h)
    add_custom_command(
        OUTPUT ${NFSU_SHADER_MANIFEST_HEADER}
        COMMAND ${CMAKE_COMMAND} -DMODE=header
                -DMANIFEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders
                -DOUTPUT=${NFSU_SHADER_MANIFEST_HEADER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_manifest.cmake
        DEPENDS ${NFSU_SHADER_MANIFESTS}
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_manifest.cmake
        COMMENT "Generating shader_manifest.h from shaders/*.manifest"
        VERBATIM)

    # The manifests against what no shader carries: the C++ vertex structs
    # the buffers are uploaded from, and the state combinations the CPU
    # reference renderer can give each kind of draw.
    add_executable(nfsu_shader_manifest_tests
        tests/shader_manifest_tests.cpp
        ${NFSU_SHADER_MANIFEST_HEADER}
    )

    target_include_directories(nfsu_shader_manifest_tests
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(nfsu_shader_manifest_tests PRIVATE nfsu_track_render)
    nfsu_enable_warnings(nfsu_shader_manifest_tests)
    add_test(NAME nfsu_shader_manifest_tests COMMAND nfsu_shader_manifest_tests)

    # Every manifest against the GLSL of its two stages, and then the same
    # check over copies with one thing changed, each of which must fail with
    # the finding that names it, or pass where uam reads the change the same
    # way.
    add_test(NAME nfsu_shader_source_guards
        COMMAND ${CMAKE_COMMAND}
                -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_manifest.cmake
                -DSHADERS=${CMAKE_CURRENT_SOURCE_DIR}/shaders
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_source_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # The Switch configure's SDK diagnostics, against SDK trees that each
    # lack one piece. It needs no SDK: every case stops before a compiler is
    # looked for.
    add_test(NAME nfsu_switch_toolchain_diagnostics
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                "-DGENERATOR=${CMAKE_GENERATOR}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/switch_toolchain_diagnostics.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # tests/switch_nro_check.cmake against a synthetic NRO built from the
    # review's real measurements, and against each header corruption it
    # names, because this machine has no Switch SDK and so never runs the
    # checker against a real build-switch output otherwise. A compiled
    # program, not a CMake script, writes the fixtures: it needs binary
    # content with NUL bytes in it, which CMake's own string(ASCII 0 ...)
    # refuses to produce. It only writes files - no process spawning, and no
    # platform #ifdef - so its own ctest entry is a self-check of the write
    # path (every fixture appears, and a second write refuses to overwrite
    # the first). tests/switch_nro_synthetic_guards.cmake is the CMake
    # script that runs it, then runs the checker against what it wrote and
    # asserts every verdict (review comment 2942, R2): this file used to run
    # the checker itself with the Win32 process-launch API under _WIN32 and
    # the POSIX pair of process-launch calls under #else, and the #else half
    # had never been built anywhere this branch was developed.
    add_executable(nfsu_switch_nro_synthetic_tests
        tests/switch_nro_synthetic_tests.cpp
    )
    nfsu_enable_warnings(nfsu_switch_nro_synthetic_tests)
    add_test(NAME nfsu_switch_nro_synthetic_tests
        COMMAND nfsu_switch_nro_synthetic_tests)

    add_test(NAME nfsu_switch_nro_synthetic_guards
        COMMAND ${CMAKE_COMMAND}
                -DWRITER=$<TARGET_FILE:nfsu_switch_nro_synthetic_tests>
                -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/tests/switch_nro_check.cmake
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/switch_nro_synthetic_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(
        nfsu_shader_manifest_tests nfsu_shader_source_guards
        nfsu_switch_toolchain_diagnostics nfsu_switch_nro_synthetic_tests
        nfsu_switch_nro_synthetic_guards
        PROPERTIES TIMEOUT 60)
endif()
