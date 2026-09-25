# The car path: every library the stock-car frame of docs/RENDERER.md is
# made with, from the parsers to the CPU reference renderer, and the three
# small ones the Switch application that shows that frame adds (issue #141).
# This file is the one place they are declared. The top-level
# CMakeLists.txt includes it before the desktop and Switch configures part,
# so the console compiles these sources from the same lists and with the
# same options as the desktop - every floating-point flag below included -
# while every other library, program and test stays desktop only.
#
# The first seven, nfsu_image to nfsu_render, were moved here from
# CMakeLists.txt as they were, comments and all. nfsu_texture_export is
# among them although the stock frame calls none of it: nfsu_render links it
# for the POSIX frame writer's rollback, and the POSIX writer is the one the
# console compiles. Moving them changes the order in which the desktop
# configure declares its targets and nothing else: no source list, option,
# link or dependency of any target differs. The neutrality gate's manifest
# lists the libraries in declaration order, and the gate reads that list as
# a set.
#
# ${CMAKE_CURRENT_SOURCE_DIR} and the relative source paths below mean the
# repository root, exactly as they did in CMakeLists.txt, because include()
# does not change the current source directory.

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
# Floating-point contraction pinned (issue #91): devkitA64 GCC 16.1.0 fuses
# seven multiply-adds in src/part_placement.cpp, in
# derive_cullable_part_placements and multiply_placement_matrix, which place
# the wheels, brakes, exhaust and plate of the stock frame (issue #83,
# docs/SWITCH_BUILD.md). No difference in the pinned frame has been observed
# from that fusion on either platform, but this repository's determinism
# claims are byte-identity claims, and an unpinned target holds one by
# accident rather than by construction, so contraction is disabled here as
# it already is on the targets above and below.
if(MSVC)
    target_compile_options(nfsu_core PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_core PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# The texture-export file I/O split out of nfsu_core (issue #139): the
# export helpers - remove_created, rollback_created_files and
# export_textures - call fs::remove, fs::rename and fs::create_directories
# directly to write, roll back and clean up the files a texture export asks
# for, which is platform code the neutrality gate must not read. See
# NFSU_PLATFORM_LIBRARIES below and docs/PLATFORM_SERVICES.md.
add_library(nfsu_texture_export
    src/texture_export.cpp
)

target_include_directories(nfsu_texture_export
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nfsu_texture_export PUBLIC cxx_std_17)
target_link_libraries(nfsu_texture_export PUBLIC nfsu_core)
nfsu_enable_warnings(nfsu_texture_export)

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

# The stock-car profile of PreparedCarScene: the recovered stock part set,
# its per-LOD mesh hashes and the recovered placement, turned into one
# prepared draw list with an instance per drawn copy. It reads no files, so
# it sits above the scene layer and beside the parsers it consumes.
add_library(nfsu_stock_scene
    src/stock_scene.cpp
)

target_link_libraries(nfsu_stock_scene PUBLIC nfsu_core nfsu_scene)
nfsu_enable_warnings(nfsu_stock_scene)
# Floating-point contraction pinned (issue #91), for the same reason as
# nfsu_core just above: src/stock_scene.cpp builds the wheel and brake
# instance transforms from the placements nfsu_core derives, so a fused
# multiply-add here would sit next to the ones issue #83 measured in
# src/part_placement.cpp rather than instead of them.
if(MSVC)
    target_compile_options(nfsu_stock_scene PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_stock_scene PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Texture-pack decoding for prepared scenes. Backends do not depend on it.
add_library(nfsu_texture_decode
    src/texture_decode.cpp
)

target_link_libraries(nfsu_texture_decode PUBLIC nfsu_scene nfsu_core)
nfsu_enable_warnings(nfsu_texture_decode)

# Deterministic CPU reference renderer. Floating-point contraction is
# disabled so that results do not depend on FMA availability.
add_library(nfsu_render
    src/render.cpp
)

target_link_libraries(nfsu_render PUBLIC nfsu_scene nfsu_image)
# The POSIX frame writer rolls a failed write back with
# rollback_created_files, which nfsu_texture_export now defines
# (src/texture_export.cpp; issue #139 split it out of nfsu_core, which used
# to hold it in src/texture.cpp). The link is private because render.h
# exposes nothing of nfsu_texture_export. The Windows writer removes by
# handle and never calls it, which is why only a POSIX build found the link
# missing (issue #132).
target_link_libraries(nfsu_render PRIVATE nfsu_texture_export)
nfsu_enable_warnings(nfsu_render)
if(MSVC)
    target_compile_options(nfsu_render PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_render PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# SHA-256 (FIPS 180-4) over bytes in memory. docs/ pins every reference
# frame by the SHA-256 of its TGA file, printed by a host tool, and the
# Switch application writes no file, so it needs a digest of its own to
# report the same number. Standard library and integer arithmetic only, so
# it carries no floating-point flags.
add_library(nfsu_sha256
    src/sha256.cpp
)

target_include_directories(nfsu_sha256
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_sha256 PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_sha256)

# The stock 240SX frame of docs/RENDERER.md from six files held in memory:
# the parsing, texture decode, stock assembly and render the viewer's pinned
# command runs, then the TGA encoding write_frame_tga writes and its
# SHA-256. The Switch application runs it on the console; the desktop test
# nfsu_stock_frame_viewer_match holds it to the viewer on the same files.
# It reads its inputs with std::ifstream, as nfsu_core's loaders do, writes
# nothing and reads no clock. It does no floating-point arithmetic of its
# own - it hands the options to nfsu_render, which carries its own flags -
# so it carries none. docs/SWITCH_BUILD.md.
add_library(nfsu_stock_frame
    src/stock_frame.cpp
)

target_link_libraries(nfsu_stock_frame
    PUBLIC nfsu_stock_scene nfsu_texture_decode nfsu_render nfsu_sha256)
nfsu_enable_warnings(nfsu_stock_frame)

# Text drawn into an RGBA8 image with a built-in 5x7 font: how the Switch
# application shows its report on the same screen as the frame, because
# libnx's console takes the display for a framebuffer of its own. Standard
# library and integer arithmetic only.
add_library(nfsu_screen_text
    src/screen_text.cpp
)

target_include_directories(nfsu_screen_text
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_screen_text PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_screen_text)

# The language the console compiles these as. Every target above asks for
# cxx_std_17, and CMake reads that as a minimum: it adds no -std option when
# the compiler's own default already meets it. The desktop's GCC 13 defaults
# to gnu++17, so its compile commands carry no -std option at all, and this
# block does not touch them. devkitA64's GCC 16 defaults to gnu++20, where
# std::filesystem::path::u8string() returns std::u8string rather than
# std::string, and src/render.cpp and src/texture_export.cpp, which append
# that value to a std::string for every message about a file name, stop
# compiling. The console therefore asks for gnu++17 by name - the desktop's
# language, GNU extensions included - on these targets only; nfsu_switch_app
# and the shader pipeline are left as they were.
if(NINTENDO_SWITCH)
    set_target_properties(
        nfsu_image nfsu_core nfsu_texture_export nfsu_scene nfsu_stock_scene
        nfsu_texture_decode nfsu_render nfsu_sha256 nfsu_stock_frame
        nfsu_screen_text
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
