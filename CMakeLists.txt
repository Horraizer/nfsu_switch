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
# scene layer for material binding.
add_library(nfsu_track
    src/track.cpp
    src/track_objects.cpp
    src/track_scene.cpp
)

target_link_libraries(nfsu_track PUBLIC nfsu_core nfsu_scene)
nfsu_enable_warnings(nfsu_track)

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

target_link_libraries(nfsu_probe
    PRIVATE nfsu_core nfsu_track nfsu_texture_decode nfsu_skin_inputs
            nfsu_stock_scene)
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

target_link_libraries(nfsu_viewer
    PRIVATE nfsu_render nfsu_texture_decode nfsu_runtime_skin nfsu_inspect_window)
nfsu_enable_warnings(nfsu_viewer)

if(NFSU_BUILD_TESTS)
    include(CTest)

    add_executable(nfsu_chunk_tests
        tests/chunk_tests.cpp
    )

    target_link_libraries(nfsu_chunk_tests PRIVATE nfsu_core)
    nfsu_enable_warnings(nfsu_chunk_tests)
    add_test(NAME nfsu_chunk_tests COMMAND nfsu_chunk_tests)

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

    add_executable(nfsu_track_object_tests
        tests/track_object_tests.cpp
    )

    target_link_libraries(nfsu_track_object_tests PRIVATE nfsu_track)
    nfsu_enable_warnings(nfsu_track_object_tests)
    add_test(NAME nfsu_track_object_tests COMMAND nfsu_track_object_tests)

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

    # nfsu_probe registers a command in one place, and print_usage() composes
    # the usage line out of the name main() dispatches on. This reads the
    # command inventory back out of three independent places - the kCommands
    # table in the source, the built program's usage text, and the README's
    # run lines - and requires all three to be the same set, so a command
    # added to one of them alone is named as missing from the others: see
    # tests/usage_commands.cmake.
    add_test(NAME nfsu_probe_usage_commands
        COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:nfsu_probe>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
                -DREADME=${CMAKE_CURRENT_SOURCE_DIR}/README.md
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/usage_commands.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # A hung test is killed and reported as a failure.
    set_tests_properties(
        nfsu_chunk_tests nfsu_geometry_tests nfsu_texture_tests
        nfsu_render_tests nfsu_scene_tests nfsu_car_parts_tests
        nfsu_track_tests nfsu_track_object_tests
        nfsu_part_placement_tests nfsu_body_skin_tests
        nfsu_stock_scene_tests
        nfsu_skin_inputs_tests nfsu_runtime_skin_tests nfsu_inspect_tests
        nfsu_viewer_dump_alias nfsu_probe_usage_commands
        PROPERTIES TIMEOUT 60)
endif()
