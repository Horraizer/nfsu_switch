cmake_minimum_required(VERSION 3.20)

project(nfsu_switch LANGUAGES CXX)

option(NFSU_BUILD_TESTS "Build unit tests" ON)

function(nfsu_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

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

# ---------------------------------------------------------------------------
# The platform services boundary and controller input
# ---------------------------------------------------------------------------
#
# docs/PLATFORM_SERVICES.md and docs/INPUT.md. Appended below everything
# above rather than interleaved with it: the libraries above are unchanged
# except where a call here adds a source or a link to one of them, and each
# of those calls says why.

# The controller model: buttons, sticks, dead zones, the remapping table,
# the rules that turn frames into actions and the recorded text form of a
# sequence of frames. Standard library only, and no clock: a frame carries
# its own time. Floating-point contraction is disabled because a recording
# is replayed and compared byte for byte, so a fused multiply-add in the
# dead zone would move a stick value by an ulp and a camera with it.
add_library(nfsu_input
    src/input.cpp
    src/input_recording.cpp
)

target_include_directories(nfsu_input
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_input PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_input)
if(MSVC)
    target_compile_options(nfsu_input PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_input PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The six service interfaces - filesystem, input, timing, logging, lifecycle
# and display - and the code written against them alone. It links nfsu_input
# because the input service reports controllers as its frames.
add_library(nfsu_platform
    src/platform.cpp
)

target_link_libraries(nfsu_platform PUBLIC nfsu_input)
nfsu_enable_warnings(nfsu_platform)

# Deterministic implementations of the six, which the tests drive. They read
# no clock, no environment and no file.
add_library(nfsu_platform_fakes
    src/platform_fakes.cpp
)

target_link_libraries(nfsu_platform_fakes PUBLIC nfsu_platform)
nfsu_enable_warnings(nfsu_platform_fakes)

# The desktop timing and logging services: the steady clock and a stream.
# Platform code, because it reads the machine's clock; the window's three
# services are the Win32 backend, and the filesystem one is nfsu_arg_path.
add_library(nfsu_platform_desktop
    src/platform_desktop.cpp
)

target_link_libraries(nfsu_platform_desktop PUBLIC nfsu_platform)
nfsu_enable_warnings(nfsu_platform_desktop)

# The argument conversion is the desktop filesystem service: its three free
# functions now go through the FileSystemService it implements, and the
# argument loop they share moved into nfsu_platform unchanged.
target_link_libraries(nfsu_arg_path PUBLIC nfsu_platform)

# Controller input on the inspection camera. It drives an Inspector, never
# a renderer, so it links nfsu_inspect and inherits that library's
# link-time dependency on the CPU rasterizer's types and nothing more; the
# comment on nfsu_inspect above says why that dependency exists. Contraction
# is disabled for the reason it is disabled in nfsu_input.
add_library(nfsu_input_camera
    src/input_camera.cpp
)

target_link_libraries(nfsu_input_camera PUBLIC nfsu_input nfsu_inspect)
nfsu_enable_warnings(nfsu_input_camera)
if(MSVC)
    target_compile_options(nfsu_input_camera PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_input_camera PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# nfsu_inspect's orbit, pan and zoom decide the camera a recorded
# controller session replays to (docs/INPUT.md), so contraction is disabled
# there for the reason nfsu_render disables it. Measured, not assumed: GCC
# 13 for AArch64 at -O3 fuses three multiply-adds into Inspector::orbit,
# which fails nfsu_inspect_tests' "the orbit is reversible" and moves the
# replayed session's per-frame digest; with these options both pass. x86-64
# without FMA has nothing to fuse, so its builds are unchanged.
if(MSVC)
    target_compile_options(nfsu_inspect PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_inspect PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The inspection session: everything the window procedure decided that is
# not a window, now compiled on every platform beside the arithmetic in
# src/inspect_window_common.cpp, and the frame loop a console backend runs.
# The window backend chosen above implements three of the services it is
# driven through.
target_sources(nfsu_inspect_window PRIVATE src/inspect_session.cpp)
target_link_libraries(nfsu_inspect_window PUBLIC nfsu_platform nfsu_input_camera)

# Which libraries promise to call no operating system. The neutrality gate
# below reads every source of each of these and fails, by file, line and
# symbol, on a platform header, a platform call, a platform macro test or a
# clock read. The list is taken from the comments above, not invented:
# nfsu_core knows no operating system (nfsu_arg_path's comment), nfsu_scene
# is backend-neutral, nfsu_track's parsers use only nfsu_core, the track
# layers above them decode and open nothing, nfsu_stock_scene reads no
# files, nfsu_body_skin depends on nothing but the standard library and the
# two skin layers above it parse and draw nothing of their own, and
# nfsu_inspect opens no window and calls no operating system. nfsu_image is
# a TGA encoder over a std::ostream. nfsu_inspect_window is in it for its
# neutral files; its one window backend is exempted by name below.
set(NFSU_NEUTRAL_LIBRARIES
    nfsu_image nfsu_core nfsu_scene nfsu_track nfsu_track_visibility
    nfsu_track_zones nfsu_track_draw_list nfsu_track_render nfsu_stock_scene
    nfsu_texture_decode nfsu_body_skin nfsu_skin_inputs nfsu_runtime_skin
    nfsu_inspect nfsu_inspect_window nfsu_input nfsu_platform
    nfsu_platform_fakes nfsu_input_camera)

# The libraries that are platform code on purpose, and are not read:
# nfsu_arg_path decodes arguments with the Windows API, nfsu_render's frame
# writer creates and publishes files with CreateFileW or open() and link(),
# and nfsu_platform_desktop reads the steady clock. docs/PLATFORM_SERVICES.md
# says what would move each one behind a service.
set(NFSU_PLATFORM_LIBRARIES nfsu_arg_path nfsu_render nfsu_platform_desktop)

# Sources of a neutral library that are platform code on purpose: the one
# window backend. The stub that replaces it elsewhere is neutral and read.
set(NFSU_PLATFORM_SOURCES src/inspect_window_win32.cpp)

# Every library target and each neutral one's sources, written for the gate
# once the whole directory has been read, so that a library added anywhere
# in this file - above this block or below it - is in the list the gate
# classifies. A library in neither list fails the gate by name.
function(nfsu_write_neutrality_manifest)
    get_directory_property(targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                           BUILDSYSTEM_TARGETS)
    set(manifest "")
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "LIBRARY$")
            string(APPEND manifest "library ${target}\n")
        endif()
    endforeach()
    foreach(target IN LISTS NFSU_NEUTRAL_LIBRARIES)
        string(APPEND manifest
               "neutral ${target} $<JOIN:$<TARGET_PROPERTY:${target},SOURCES>,|>\n")
    endforeach()
    foreach(target IN LISTS NFSU_PLATFORM_LIBRARIES)
        string(APPEND manifest "platform ${target}\n")
    endforeach()
    foreach(source IN LISTS NFSU_PLATFORM_SOURCES)
        string(APPEND manifest "exempt ${source}\n")
    endforeach()
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nfsu_platform_neutrality.txt
         CONTENT "${manifest}")
endfunction()
cmake_language(DEFER CALL nfsu_write_neutrality_manifest)

if(NFSU_BUILD_TESTS)
    # The controller model and its camera, from recorded frames: the dead
    # zone edges, the remapping refusals, connect and disconnect, conflicts,
    # a stick pinned at its extreme, and a scripted session replayed to a
    # camera that is pinned byte for byte.
    add_executable(nfsu_input_tests
        tests/input_tests.cpp
    )

    target_link_libraries(nfsu_input_tests PRIVATE nfsu_input_camera)
    nfsu_enable_warnings(nfsu_input_tests)
    if(MSVC)
        target_compile_options(nfsu_input_tests PRIVATE /fp:precise)
    else()
        target_compile_options(nfsu_input_tests PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    add_test(NAME nfsu_input_tests COMMAND nfsu_input_tests)

    # The session the window procedure used to be, driven through the fakes:
    # drags, clicks, the wheel, keys, resizes, a lost capture and failed
    # renders, then the frame loop with a controller. Also the neutral
    # filesystem helpers and the desktop clock and log.
    add_executable(nfsu_platform_tests
        tests/platform_tests.cpp
    )

    target_link_libraries(nfsu_platform_tests
        PRIVATE nfsu_inspect_window nfsu_platform_fakes nfsu_platform_desktop
                nfsu_arg_path)
    nfsu_enable_warnings(nfsu_platform_tests)
    add_test(NAME nfsu_platform_tests COMMAND nfsu_platform_tests)

    # The neutrality gate. It reads sources, not binaries, so it needs no
    # build to have succeeded; tests/platform_neutrality.cmake says what each
    # rule catches and what none of them can.
    add_test(NAME nfsu_platform_neutrality
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/nfsu_platform_neutrality.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_neutrality.cmake)

    set_tests_properties(
        nfsu_input_tests nfsu_platform_tests nfsu_platform_neutrality
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
