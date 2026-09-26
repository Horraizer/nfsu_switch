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

# The car path - the parsers, the stock assembly, the texture decode, the
# CPU reference renderer and the stock-car frame built from them - is
# declared in one file that both configures read, so the Switch build
# compiles those libraries from the same source lists and with the same
# options as the desktop build. cmake/car_libraries.cmake says which they
# are and why each is there.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/car_libraries.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/draw_libraries.cmake)
# The zone decoder and the race route (issue #61), for both configures.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/race_libraries.cmake)
# The section streamer (issue #157), for both configures.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/residency_libraries.cmake)
# The retail asset manifest, its check and the console's file system (issue
# #47), for both configures; it links nfsu_sha256 from cmake/car_libraries.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/asset_libraries.cmake)
# The vehicle model of issue #58, for both configures as well; it links
# nfsu_sha256 from cmake/car_libraries.cmake, so it is included after it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/vehicle_libraries.cmake)

# The Switch build configures the shared car and draw libraries and switch/,
# and nothing below this point: the desktop programs, the other libraries and every test of
# them are never built for the console, and a desktop configure never enters
# this branch.
if(NINTENDO_SWITCH)
    if(NFSU_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(switch)
    return()
endif()

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



# nfsu_track_zones, the visibility zones of one track, was declared here
# until issue #61 moved it, as it was, into cmake/race_libraries.cmake: the
# race route links it, and the console builds the race route.


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

# The desktop track slice's cost accounting: the four stages' own reports
# rearranged into one named, stage-tagged counter table, and the budget
# check a gate runs over it. It is declared after nfsu_track_render because
# it reads that layer's RenderStats and TrackRenderStats, and it adds no
# fifth source of truth: every counter it exposes is a field some other
# layer already computed.
#
# It carries no floating-point flags, and that is a statement rather than an
# omission: this library does no floating-point arithmetic at all. Every
# counter it touches is an integer count or an integer byte total, so there
# is nothing here for contraction to move, exactly as for
# nfsu_track_draw_list. It also reads no clock - see the header for why a
# wall-clock number is not a counter this project pins.
add_library(nfsu_track_budget
    src/track_budget.cpp
)

target_link_libraries(nfsu_track_budget PUBLIC nfsu_track_render)
nfsu_enable_warnings(nfsu_track_budget)

# The read-only inspection layer over that scene: where every placed
# instance came from, what it draws, and why it is or is not on screen.
# It renders nothing and decodes nothing, and it links nfsu_track_render
# rather than duplicating two of its rules - track_placed_vertex, the one
# placement composition, and frame_track_bounds, the view-independent
# sphere fit the free camera's default is read out of.
#
# Floating-point contraction is disabled for the reason
# nfsu_track_visibility disables it, not the reason nfsu_render does:
# every verdict this library reports is a strict comparison - a plane
# distance against zero, a screen-size level against kTrackMinDrawLevel,
# a camera value against a range - so a fused multiply-add would move a
# boundary and with it a printed answer. See docs/TRACK_PROBE.md.
add_library(nfsu_track_probe
    src/track_probe.cpp
)

target_link_libraries(nfsu_track_probe PUBLIC nfsu_track_render)
nfsu_enable_warnings(nfsu_track_probe)
if(MSVC)
    target_compile_options(nfsu_track_probe PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_track_probe PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The career event table of FRONTEND/FrontB.lzc and the track table of
# GLOBAL/GLOBALB.LZC it points into: which track, lap count and direction
# each Underground-mode event runs, and the stream and texture file names
# the game builds for a track. It links nfsu_core for the JDLZ decoder and
# nothing else, decodes integers only and reads no clock, so it carries no
# floating-point flags. See docs/RACE_EVENTS.md.
add_library(nfsu_race_events
    src/race_events.cpp
)

target_link_libraries(nfsu_race_events PUBLIC nfsu_core)
nfsu_enable_warnings(nfsu_race_events)

# The whole track one race loads (issue #156): the streaming table of a
# track's TRACKB file, the recovered rule that picks which of its sections
# speed.exe loads, the bounded read of exactly those byte ranges, and their
# merge into one geometry report and one scenery report that the existing
# scene layer places as one scene. It links the zone decoder because the
# rule reads two of the zone file's lists, nfsu_texture_decode for the
# per-section texture sources, and nfsu_gpu_upload for the budgets of issue
# #52. It reads its files with std::ifstream, as nfsu_core's loaders do,
# reads no clock and makes no decision from a floating-point result - it
# only takes the minimum and maximum of bounds the parsers computed - so it
# carries no floating-point flags. See docs/TRACK_WHOLE.md.
add_library(nfsu_track_stream
    src/track_stream.cpp
)

target_link_libraries(nfsu_track_stream
    PUBLIC nfsu_track_zones nfsu_texture_decode nfsu_gpu_upload)
nfsu_enable_warnings(nfsu_track_stream)

# nfsu_race_route, the route of one race over a TRACKB bundle (issue #61),
# is declared in cmake/race_libraries.cmake, which both configures read.

# The read-only inspector. It is declared after every library it uses.
add_executable(nfsu_probe
    src/main.cpp
)

# nfsu_track_render is what `track-render` needs, and it is the only thing
# in this list that writes a file. It carries nfsu_render and
# nfsu_track_draw_list with it, so the two are not named again.
target_link_libraries(nfsu_probe
    PRIVATE nfsu_core nfsu_texture_export nfsu_track nfsu_track_visibility
            nfsu_track_zones
            nfsu_track_draw_list nfsu_track_render nfsu_track_budget
            nfsu_track_probe nfsu_race_events
            nfsu_texture_decode nfsu_skin_inputs nfsu_stock_scene
            nfsu_arg_path)
# `track-whole` (issue #156) is the one command that needs the whole-track
# layer; it is linked on its own line so the list above stays as it was.
target_link_libraries(nfsu_probe PRIVATE nfsu_track_stream)
# `race-route` (issue #61) needs the race route layer, linked on its own line
# for the same reason.
target_link_libraries(nfsu_probe PRIVATE nfsu_race_route)
nfsu_enable_warnings(nfsu_probe)

# nfsu_body_skin, nfsu_skin_inputs, nfsu_runtime_skin and nfsu_inspect were
# declared here until issue #148 moved them, as they were, into
# cmake/car_libraries.cmake: the Switch application composes the body skin
# and orbits its camera with them, so the console builds them too.

# The desktop window that drives nfsu_inspect. Exactly one backend is
# compiled: the Win32 one, which needs nothing but user32 and gdi32, or a
# stub that fails closed everywhere else. This is the only file in the
# project that talks to a windowing API. src/inspect_window_common.cpp is
# the arithmetic every backend shares and calls no operating system, so it
# is always compiled.
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

    target_link_libraries(nfsu_texture_tests PRIVATE nfsu_core nfsu_texture_export)
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

    # The cost table, the four gatherers and the budget check. It builds no
    # byte fixture and parses nothing: every function under test reads a
    # report struct and writes counters, so the inputs are hand-set reports
    # with a different value in every field, which is what makes a gatherer
    # reading the wrong source field produce the wrong number rather than
    # the right one by luck. The integration - that these counters equal
    # what the pipeline really costs - is the retail acceptance of
    # docs/TRACK_BUDGET.md, which needs the installation and is therefore
    # not a CTest entry.
    add_executable(nfsu_track_budget_tests
        tests/track_budget_tests.cpp
    )

    target_link_libraries(nfsu_track_budget_tests PRIVATE nfsu_track_budget)
    nfsu_enable_warnings(nfsu_track_budget_tests)
    add_test(NAME nfsu_track_budget_tests COMMAND nfsu_track_budget_tests)

    # The inspection layer: the free camera and its two moves, the
    # residency reason, the bounding-tree provenance and the three gate
    # verdicts. Its fixtures go through the real parsers over byte images
    # at absolute offsets, like the render tests beside it, and every
    # visibility answer it asserts is also compared against the one
    # cull_track_region reaches for the same frustum.
    add_executable(nfsu_track_probe_tests
        tests/track_probe_tests.cpp
    )

    target_link_libraries(nfsu_track_probe_tests PRIVATE nfsu_track_probe)
    nfsu_enable_warnings(nfsu_track_probe_tests)
    add_test(NAME nfsu_track_probe_tests COMMAND nfsu_track_probe_tests)
    add_executable(nfsu_part_placement_tests
        tests/part_placement_tests.cpp
    )

    target_link_libraries(nfsu_part_placement_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_part_placement_tests)
    add_test(NAME nfsu_part_placement_tests COMMAND nfsu_part_placement_tests)

    # The track and event tables over synthetic chunk images: every decoded
    # field at its offset, every refusal at the offset it names, the
    # availability rule of 0x005A0FC0, and the JDLZ load path through a
    # literal-only blob. It also writes the fixture the race-events guards
    # drive the probe over.
    add_executable(nfsu_race_event_tests
        tests/race_event_tests.cpp
    )

    target_link_libraries(nfsu_race_event_tests PRIVATE nfsu_race_events)
    nfsu_enable_warnings(nfsu_race_event_tests)
    add_test(NAME nfsu_race_event_tests COMMAND nfsu_race_event_tests)

    # The whole-track layer over synthetic multi-section streams: a TRACKB
    # image carrying a streaming table and a zone container, and a stream
    # of several sections at their own offsets, parsed by the real parsers.
    # It also writes the fixture the track-whole guards drive the probe over.
    add_executable(nfsu_track_stream_tests
        tests/track_stream_tests.cpp
    )

    target_link_libraries(nfsu_track_stream_tests PRIVATE nfsu_track_stream)
    nfsu_enable_warnings(nfsu_track_stream_tests)
    add_test(NAME nfsu_track_stream_tests COMMAND nfsu_track_stream_tests)
    set_tests_properties(nfsu_track_stream_tests PROPERTIES TIMEOUT 60)

    # The race route over synthetic TRACKB images: every decoded field at
    # its offset, every refusal, the spline and its arc length, the
    # coordinate, the boundaries, lap counting, the start/finish crossing, a
    # missed boundary, a reverse crossing, a path grazing a boundary and the
    # wrong way. It also writes the fixture the race-route guards drive the
    # probe over.
    add_executable(nfsu_race_route_tests
        tests/race_route_tests.cpp
    )

    target_link_libraries(nfsu_race_route_tests PRIVATE nfsu_race_route nfsu_sha256)
    nfsu_enable_warnings(nfsu_race_route_tests)
    if(MSVC)
        target_compile_options(nfsu_race_route_tests PRIVATE /fp:precise)
    else()
        target_compile_options(nfsu_race_route_tests PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    add_test(NAME nfsu_race_route_tests COMMAND nfsu_race_route_tests)
    set_tests_properties(nfsu_race_route_tests PROPERTIES TIMEOUT 60)

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

    # The same for track-budget, for the same reason and with one more: this
    # command's exit 4 and exit 3 differ in a single printed line and mean
    # opposite things about a budget, so a property that ignored the status
    # would leave the verdict itself untested.
    # tests/track_budget_guards.cmake says what each case is for and names
    # the one exit no case there can reach.
    add_test(NAME nfsu_probe_track_budget_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_budget_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # The same treatment for `track-probe`, which is the command the render
    # guards' own header argues every probe command deserves: 123 cases, each
    # asserting the exact exit status and the diagnostic. track-probe writes
    # no file, so the output rules of the render script have no counterpart
    # here and the exit status carries the whole machine-readable contract.
    #
    # 80 of them need no input files, which is what tests/track_render_
    # guards.cmake can do. The other 43 drive the command over a fixture
    # nfsu_track_probe_tests writes on request - the same bytes its own
    # cases parse, at the same absolute offsets - because the listing limit,
    # the per-instance block and every total that is non-zero only over a
    # real region are unreachable without one. That is the gap the render
    # guards document and could not close, and it is why TESTS is passed in
    # beside PROBE.
    add_test(NAME nfsu_probe_track_probe_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_track_probe_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_probe_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # What `race-events` refuses and exits with when it has no input at all,
    # and what it prints and exits with over the three fixture files
    # nfsu_race_event_tests writes on request: the whole listing, the
    # Underground order lines, and the refusal of a race that names a track
    # the track table does not hold. tests/race_events_guards.cmake says
    # what each case is for.
    add_test(NAME nfsu_probe_race_events_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_race_event_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/race_events_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # What `track-whole` refuses, prints and exits with: every refusal with
    # no input at all, and the whole listing over the synthetic stream
    # nfsu_track_stream_tests writes on request. tests/track_whole_guards.cmake
    # says what each case is for.
    add_test(NAME nfsu_probe_track_whole_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_track_stream_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_whole_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_probe_track_whole_guards PROPERTIES TIMEOUT 60)

    # What `race-route` refuses and exits with, and what it prints over a
    # synthetic TRACKB bundle nfsu_race_route_tests writes on request beside
    # the race-events fixture pair: the route listing, a replayed path file,
    # the built-in drives, every malformed path line refused, and three
    # bundles of absurd numbers refused rather than replayed forever.
    # tests/race_route_guards.cmake says what each case is for.
    add_test(NAME nfsu_probe_race_route_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_race_route_tests>
                -DEVENT_TESTS=$<TARGET_FILE:nfsu_race_event_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/race_route_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(nfsu_probe_race_route_guards PROPERTIES TIMEOUT 60)
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
        nfsu_track_budget_tests nfsu_track_probe_tests
        nfsu_part_placement_tests nfsu_race_event_tests nfsu_body_skin_tests
        nfsu_stock_scene_tests
        nfsu_skin_inputs_tests nfsu_runtime_skin_tests nfsu_inspect_tests
        nfsu_viewer_dump_alias nfsu_viewer_stock_needs_car
        nfsu_viewer_stock_excludes_selectors nfsu_probe_usage_commands
        nfsu_probe_track_render_needs_out
        nfsu_probe_track_render_keeps_its_output
        nfsu_probe_track_render_guards
        nfsu_probe_track_budget_guards
        nfsu_probe_track_probe_guards
        nfsu_probe_race_events_guards
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
#
# The controller model, nfsu_input, and the camera it drives,
# nfsu_input_camera, were declared in this block until issue #148 moved
# them, as they were and with nfsu_inspect's floating-point options, into
# cmake/car_libraries.cmake, which the console builds too.

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
# clock read. The list is taken from the comments above and in
# cmake/car_libraries.cmake, not invented:
# nfsu_core knows no operating system (nfsu_arg_path's comment), nfsu_scene
# is backend-neutral, nfsu_track's parsers use only nfsu_core, the track
# layers above them decode and open nothing, nfsu_stock_scene reads no
# files, nfsu_body_skin depends on nothing but the standard library and the
# two skin layers above it parse and draw nothing of their own, and
# nfsu_inspect opens no window and calls no operating system. nfsu_image is
# a TGA encoder over a std::ostream. nfsu_inspect_window is in it for its
# neutral files; its one window backend is exempted by name below.
# nfsu_memory depends on nothing but the standard library (its own comment
# above), and nfsu_gpu_upload calls no deko3d function and reads only the
# scene and draw-list types below it, so both joined this list when they
# merged in from main. nfsu_track_budget carries no floating-point flags and
# reads no clock (its own comment above); it links nfsu_track_render and
# calls no operating system function of its own. The three libraries issue
# #141 added for the Switch application are neutral by their comments in
# cmake/car_libraries.cmake: nfsu_sha256 and nfsu_screen_text use nothing but
# the standard library, and nfsu_stock_frame reads its inputs with
# std::ifstream, as nfsu_core's loaders do, and reads no clock - the
# application times it from outside. nfsu_race_events (issue #155) decodes
# two tables with nothing but the standard library and nfsu_core's JDLZ
# decoder, and its loaders read with std::ifstream as nfsu_core's do.
# nfsu_stock_view, which issue #148 added, is the control table and camera
# session of that application over nfsu_input_camera and nfsu_render; it
# reads no clock either, because a controller frame carries its own time.
set(NFSU_NEUTRAL_LIBRARIES
    nfsu_image nfsu_core nfsu_scene nfsu_track nfsu_track_visibility
    nfsu_track_zones nfsu_track_draw_list nfsu_track_render nfsu_track_probe
    nfsu_track_budget nfsu_race_events
    nfsu_stock_scene
    nfsu_texture_decode nfsu_body_skin nfsu_skin_inputs nfsu_runtime_skin
    nfsu_inspect nfsu_inspect_window nfsu_input nfsu_platform
    nfsu_platform_fakes nfsu_input_camera nfsu_memory nfsu_gpu_upload nfsu_draw_commands
    nfsu_sha256 nfsu_stock_frame nfsu_screen_text nfsu_stock_view nfsu_frame_compare)
# nfsu_race_route (issue #61) decodes a TRACKB bundle with nothing but the
# standard library, nfsu_core's JDLZ decoder and nfsu_track_zones' polygon
# test, reads with std::ifstream as nfsu_core's loaders do, and reads no
# clock: a replay step carries its own duration.
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_race_route)

# The libraries that are platform code on purpose, and are not read:
# nfsu_arg_path decodes arguments with the Windows API, nfsu_render's frame
# writer creates and publishes files with CreateFileW or open() and link(),
# nfsu_texture_export's export helpers (issue #139, split out of nfsu_core)
# call fs::remove, fs::rename and fs::create_directories to write, roll back
# and clean up the files a texture export asks for, and nfsu_platform_desktop
# reads the steady clock. docs/PLATFORM_SERVICES.md says what would move each
# one behind a service.
set(NFSU_PLATFORM_LIBRARIES nfsu_arg_path nfsu_render nfsu_texture_export nfsu_platform_desktop)

# Sources of a neutral library that are platform code on purpose: the one
# window backend. src/texture.cpp used to be exempted here too, whole, for
# its texture-export helpers (remove_created, rollback_created_files,
# export_textures), which genuinely touch the filesystem - fs::remove,
# fs::rename, fs::create_directories - to clean up and write the files a
# caller asked for. Issue #139 moved those helpers into
# src/texture_export.cpp and the nfsu_texture_export library above
# (NFSU_PLATFORM_LIBRARIES), so src/texture.cpp holds only the texture
# decoder now and the gate reads it again.
set(NFSU_PLATFORM_SOURCES src/inspect_window_win32.cpp)

# Neutral as well: nfsu_track_stream (issue #156) reads its files with
# std::ifstream, as nfsu_core's loaders do, and reads no clock; its own
# comment above says so.
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_track_stream)

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
                -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_neutrality.cmake)

    set_tests_properties(
        nfsu_input_tests nfsu_platform_tests nfsu_platform_neutrality
        PROPERTIES TIMEOUT 60)
endif()

# Memory arenas and GPU upload bookkeeping for the Switch renderer, issues #52
# and #50. Appended as one block, after the existing tests, so that the lists
# above are not reordered.
#



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
        PRIVATE nfsu_draw_commands nfsu_track_render nfsu_runtime_skin nfsu_texture_decode)
    nfsu_enable_warnings(nfsu_gpu_upload_tests)
    add_test(NAME nfsu_gpu_upload_tests COMMAND nfsu_gpu_upload_tests)

    add_executable(nfsu_draw_execution_tests tests/draw_execution_tests.cpp)
    target_link_libraries(nfsu_draw_execution_tests PRIVATE nfsu_draw_commands)
    nfsu_enable_warnings(nfsu_draw_execution_tests)
    add_test(NAME nfsu_draw_execution_tests COMMAND nfsu_draw_execution_tests)

    # A hung test is killed and reported as a failure, as above.
    set_tests_properties(nfsu_memory_arena_tests nfsu_gpu_upload_tests
        PROPERTIES TIMEOUT 60)
endif()

# The shader pipeline's checks that need no Switch SDK. The manifests in
# shaders/ state each program's interface; docs/SHADER_PIPELINE.md says what
# every check catches and what none of them can. The check against the
# compiled shaders needs uam and runs in the Switch build, in switch/.
if(NFSU_BUILD_TESTS)

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

# The stock-car frame the Switch application shows, and the three libraries
# issue #141 added for it (cmake/car_libraries.cmake), tested here because
# the console cannot run a test. Appended as one block, after the existing
# tests, so that the lists above are not reordered.
if(NFSU_BUILD_TESTS)
    # FIPS 180-2's published examples, the padding boundaries against an
    # independent implementation, and any split of a message across updates.
    add_executable(nfsu_sha256_tests
        tests/sha256_tests.cpp
    )

    target_link_libraries(nfsu_sha256_tests PRIVATE nfsu_sha256)
    nfsu_enable_warnings(nfsu_sha256_tests)
    add_test(NAME nfsu_sha256_tests COMMAND nfsu_sha256_tests)

    # The font table, the pixels one glyph and a scaled glyph put down, the
    # clipping at every edge of an image whose rows are padded, and wrapping.
    add_executable(nfsu_screen_text_tests
        tests/screen_text_tests.cpp
    )

    target_link_libraries(nfsu_screen_text_tests PRIVATE nfsu_screen_text)
    nfsu_enable_warnings(nfsu_screen_text_tests)
    add_test(NAME nfsu_screen_text_tests COMMAND nfsu_screen_text_tests)

    # The application's four calls over six synthetic files in the game's
    # layout: the whole frame, each input missing in turn, each one damaged
    # three ways, the report lines and the encoding against the file
    # write_frame_tga writes. It links nfsu_arg_path for its --frame and
    # --write-fixture modes, which take a path on the command line. Since
    # issue #148 also the painted frame and the console's controller
    # session (nfsu_stock_view): the button mapping, the control table, the
    # default-camera check and a recorded session over the synthetic car.
    add_executable(nfsu_stock_frame_tests
        tests/stock_frame_tests.cpp
    )

    target_link_libraries(nfsu_stock_frame_tests
        PRIVATE nfsu_stock_frame nfsu_stock_view nfsu_arg_path)
    nfsu_enable_warnings(nfsu_stock_frame_tests)
    add_test(NAME nfsu_stock_frame_tests COMMAND nfsu_stock_frame_tests)

    # The same synthetic files rendered by nfsu_viewer with the pinned
    # stock-car command and by the application's path, which must agree on
    # every byte of the frame and every line of the render report; then the
    # same for the painted command (issue #148).
    # tests/stock_frame_viewer_match.cmake says what else it requires.
    add_test(NAME nfsu_stock_frame_viewer_match
        COMMAND ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:nfsu_viewer>
                -DTESTS=$<TARGET_FILE:nfsu_stock_frame_tests>
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/nfsu_stock_frame_viewer_match
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stock_frame_viewer_match.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(
        nfsu_sha256_tests nfsu_screen_text_tests nfsu_stock_frame_tests
        nfsu_stock_frame_viewer_match
        PROPERTIES TIMEOUT 60)
endif()

if(NFSU_BUILD_TESTS)
    add_executable(nfsu_draw_commands_retail tests/draw_commands_retail.cpp)
    target_link_libraries(nfsu_draw_commands_retail
        PRIVATE nfsu_draw_commands nfsu_stock_frame nfsu_arg_path)
    nfsu_enable_warnings(nfsu_draw_commands_retail)
endif()

# ---------------------------------------------------------------------------
# Whole-track collision and its contact queries (issue #158)
# ---------------------------------------------------------------------------
#
# Appended as one block, after everything above, so that no list above is
# reordered. The static collision of a whole race track loaded once, with its
# memory placed against the arena plan of issue #52, and the bounded point
# and segment contact queries issue #60 needs, with the units, normals and
# coordinate conversion that issue asks for. It reads the collision
# nfsu_track already decodes; see docs/TRACK_COLLISION.md, "The whole
# circuit". It is desktop only for now: the Switch tree does not configure
# this block, because nothing on the console queries collision yet.
#
# Floating-point contraction is disabled for the reason nfsu_track_visibility
# disables it: every answer here turns on a strict comparison - a determinant
# against zero, a barycentric coordinate against 0 and 1, a slope against
# kTrackContactRoadMinUp - so a fused multiply-add would move a boundary
# case, and with it a contact the documentation pins.
add_library(nfsu_track_contact
    src/track_contact.cpp
)

target_link_libraries(nfsu_track_contact PUBLIC nfsu_track nfsu_memory)
nfsu_enable_warnings(nfsu_track_contact)
if(MSVC)
    target_compile_options(nfsu_track_contact PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_track_contact PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Neutral: it decodes nothing of its own, reads files only through
# load_track_collision's std::ifstream, calls no operating system function and
# reads no clock. Appended to the list rather than written into it, for the
# reason this block is appended at all; the manifest is written once the whole
# directory has been read, so it sees this.
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_track_contact)

# `track-contact` loads a whole track through it, and places the load in the
# Switch arena plan, which switch_memory_budget() in nfsu_gpu_upload builds.
target_link_libraries(nfsu_probe PRIVATE nfsu_track_contact nfsu_gpu_upload)

if(NFSU_BUILD_TESTS)
    # Synthetic images of four separate sections of track: the triangle table,
    # road and wall, point and segment contacts and their normals, a query
    # inside one section against the same query over that section alone, the
    # refusals and caps, the memory plan and the coordinate map. It links
    # nfsu_arg_path for its --write-fixture mode, which takes paths on the
    # command line.
    add_executable(nfsu_track_contact_tests
        tests/track_contact_tests.cpp
    )

    target_link_libraries(nfsu_track_contact_tests PRIVATE nfsu_track_contact nfsu_arg_path)
    nfsu_enable_warnings(nfsu_track_contact_tests)
    add_test(NAME nfsu_track_contact_tests COMMAND nfsu_track_contact_tests)

    # What `track-contact` refuses, prints and exits with, over the fixture
    # the tests above write on request. A script for the reason
    # tests/track_render_guards.cmake gives: the exit status is half of what
    # is pinned, and PASS_REGULAR_EXPRESSION would ignore it.
    add_test(NAME nfsu_probe_track_contact_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_track_contact_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_contact_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(nfsu_track_contact_tests nfsu_probe_track_contact_guards
        PROPERTIES TIMEOUT 60)
endif()

# Section residency along a path (issue #157). nfsu_track_residency is
# declared for both configures in cmake/residency_libraries.cmake, with its
# floating-point options; on the desktop it also links nfsu_track_stream,
# whose table lookup and zone test it calls. It reads no file and no clock
# and calls no library mathematics, so it is neutral. See
# docs/TRACK_STREAMING.md.
target_link_libraries(nfsu_track_residency PUBLIC nfsu_track_stream)
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_track_residency)

# `track-stream-path` is the one command that needs it; linked on its own
# line, as nfsu_track_stream is, so the lists above stay as they were.
target_link_libraries(nfsu_probe PRIVATE nfsu_track_residency)

if(NFSU_BUILD_TESTS)
    # The streamer over synthetic zone graphs: entering, leaving and
    # revisiting a zone, the start set, the look-ahead, the three-sample
    # commit, a request that does not fit, the release order and the
    # sequence hash, plus the path format and its sampling.
    add_executable(nfsu_track_residency_tests
        tests/track_residency_tests.cpp
    )

    target_link_libraries(nfsu_track_residency_tests PRIVATE nfsu_track_residency)
    nfsu_enable_warnings(nfsu_track_residency_tests)
    add_test(NAME nfsu_track_residency_tests COMMAND nfsu_track_residency_tests)

    # What `track-stream-path` refuses, prints and exits with: every refusal
    # with no input at all, and whole runs over the synthetic stream
    # nfsu_track_stream_tests writes on request, frames included.
    # tests/track_stream_path_guards.cmake says what each case is for.
    add_test(NAME nfsu_probe_track_stream_path_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_track_stream_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/track_stream_path_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(nfsu_track_residency_tests
        nfsu_probe_track_stream_path_guards
        PROPERTIES TIMEOUT 120)
endif()

# ---------------------------------------------------------------------------
# Retail assets: the manifest, the check and the console's file system
# (issue #47)
# ---------------------------------------------------------------------------
#
# Appended as one block, after everything above, so that no list above is
# reordered. Both libraries are declared in cmake/asset_libraries.cmake for
# both configures; this block adds what only the desktop has - the probe's
# link, the classification and the tests. See docs/SWITCH_ASSETS.md.
#
# nfsu_assets is neutral: it reaches files only through the FileSystemService
# it is handed and reads no clock (its own comment in that file).
# nfsu_posix_file_system is platform code on purpose: it calls opendir,
# readdir, closedir and stat, which is what the console's service is.
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_assets)
list(APPEND NFSU_PLATFORM_LIBRARIES nfsu_posix_file_system)

# `asset-check` runs the check against a directory through the desktop
# service; linked on its own line so the lists above stay as they were.
target_link_libraries(nfsu_probe PRIVATE nfsu_assets)

if(NFSU_BUILD_TESTS)
    # The manifests against the files the other layers name, the naming
    # rule, every classification over a fake file system and over real
    # directories through both services, the bounds of every read, the
    # config file and the console's whole report. It links nfsu_stock_frame
    # and nfsu_race_events only to compare their file names with the sets.
    add_executable(nfsu_asset_tests
        tests/asset_tests.cpp
    )

    target_link_libraries(nfsu_asset_tests
        PRIVATE nfsu_assets nfsu_platform_fakes nfsu_arg_path nfsu_posix_file_system
                nfsu_stock_frame nfsu_race_events)
    nfsu_enable_warnings(nfsu_asset_tests)
    add_test(NAME nfsu_asset_tests COMMAND nfsu_asset_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # What `asset-check` refuses, prints and exits with, over directories the
    # script writes. A script for the reason tests/track_render_guards.cmake
    # gives: the exit status is half of what is pinned.
    add_test(NAME nfsu_probe_asset_check_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/asset_check_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(nfsu_asset_tests nfsu_probe_asset_check_guards
        PROPERTIES TIMEOUT 60)
endif()

# ---------------------------------------------------------------------------
# The vehicle model (issue #58)
# ---------------------------------------------------------------------------
#
# Appended as one block, after everything above, so that no list above is
# reordered: nfsu_vehicle itself is declared in cmake/vehicle_libraries.cmake
# for both configures, and this block adds what only the desktop has - the
# probe's link, the neutrality classification and the tests.
#
# `vehicle-sim` runs the model over a scripted input sequence, and with
# --handling it reads the car's handling out of GLOBALB.BUN through
# nfsu_core's car type table first.
target_link_libraries(nfsu_probe PRIVATE nfsu_vehicle)

# nfsu_vehicle is neutral by its comment in cmake/vehicle_libraries.cmake: it
# reads no file and no clock and calls nothing but the standard library and
# nfsu_sha256. The manifest is written once the whole directory has been
# read, so this append is in the list the gate classifies.
list(APPEND NFSU_NEUTRAL_LIBRARIES nfsu_vehicle)

if(NFSU_BUILD_TESTS)
    # The model against its recovered rules and its approximations: the
    # arithmetic, the record decoder and its refusals, the torque curve and
    # the shift tables, rest, acceleration through the gears, braking,
    # cornering, the grip limit, the top speed, the drivetrain, the script
    # parser's refusals and the pinned telemetry hash. It also writes, on
    # request, the synthetic GLOBALB.BUN the probe guards read. It carries
    # the model's floating-point options because it recomputes the model's
    # own expressions and compares them bit for bit.
    add_executable(nfsu_vehicle_tests
        tests/vehicle_tests.cpp
    )

    target_link_libraries(nfsu_vehicle_tests PRIVATE nfsu_vehicle nfsu_arg_path)
    nfsu_enable_warnings(nfsu_vehicle_tests)
    if(MSVC)
        target_compile_options(nfsu_vehicle_tests PRIVATE /fp:precise)
    else()
        target_compile_options(nfsu_vehicle_tests PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    add_test(NAME nfsu_vehicle_tests COMMAND nfsu_vehicle_tests)

    # What `vehicle-sim` refuses, prints and exits with, over no input, over
    # script files this test writes, and over the synthetic GLOBALB.BUN
    # nfsu_vehicle_tests writes. tests/vehicle_sim_guards.cmake says what
    # each case is for.
    add_test(NAME nfsu_probe_vehicle_sim_guards
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DTESTS=$<TARGET_FILE:nfsu_vehicle_tests>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/vehicle_sim_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(nfsu_vehicle_tests nfsu_probe_vehicle_sim_guards
        PROPERTIES TIMEOUT 120)
endif()

# Whole-frame comparison metrics for the console proof of issue #147.
if(NFSU_BUILD_TESTS)
    add_executable(nfsu_frame_compare_tests tests/frame_compare_tests.cpp)
    target_link_libraries(nfsu_frame_compare_tests PRIVATE nfsu_frame_compare)
    nfsu_enable_warnings(nfsu_frame_compare_tests)
    add_test(NAME nfsu_frame_compare_tests COMMAND nfsu_frame_compare_tests)
endif()
