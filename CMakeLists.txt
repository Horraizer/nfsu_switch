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
# of the 24-byte track vertex layout, and the prepared draw list with its
# cost statistics. The parser (src/track.cpp) uses only nfsu_core; the
# prepared region (src/track_scene.cpp) also uses the backend-neutral
# scene layer for material binding.
add_library(nfsu_track
    src/track.cpp
    src/track_scene.cpp
)

target_link_libraries(nfsu_track PUBLIC nfsu_core nfsu_scene)
nfsu_enable_warnings(nfsu_track)

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

target_link_libraries(nfsu_probe PRIVATE nfsu_core nfsu_track nfsu_texture_decode)
nfsu_enable_warnings(nfsu_probe)

add_executable(nfsu_viewer
    src/viewer_main.cpp
)

target_link_libraries(nfsu_viewer PRIVATE nfsu_render nfsu_texture_decode)
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

    add_executable(nfsu_track_tests
        tests/track_tests.cpp
    )

    target_link_libraries(nfsu_track_tests PRIVATE nfsu_track)
    nfsu_enable_warnings(nfsu_track_tests)
    add_test(NAME nfsu_track_tests COMMAND nfsu_track_tests)

    add_executable(nfsu_body_skin_tests
        tests/body_skin_tests.cpp
    )

    target_link_libraries(nfsu_body_skin_tests PRIVATE nfsu_body_skin)
    nfsu_enable_warnings(nfsu_body_skin_tests)
    add_test(NAME nfsu_body_skin_tests COMMAND nfsu_body_skin_tests)

    # A hung test is killed and reported as a failure.
    set_tests_properties(
        nfsu_chunk_tests nfsu_geometry_tests nfsu_texture_tests
        nfsu_render_tests nfsu_scene_tests nfsu_car_parts_tests
        nfsu_track_tests nfsu_body_skin_tests
        PROPERTIES TIMEOUT 60)
endif()
