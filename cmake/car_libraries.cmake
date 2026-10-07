# All floating-point flags below are inherited from the project-wide rule
# in cmake/floating_point.cmake (issue #175); no target owns a separate pin.

# The car path: every library the stock-car frame of docs/RENDERER.md is
# made with, from the parsers to the CPU reference renderer, the three
# small ones the Switch application that shows that frame adds (issue #141),
# and the runtime body skin and the controller camera that paint the car and
# orbit it there (issue #148).
# This file is the one place they are declared. The top-level
# CMakeLists.txt includes it before the desktop and Switch configures part,
# so the console compiles these sources from the same lists and with the
# same options as the desktop, including the project-wide floating-point rule,
# while draw-recording dependencies are shared separately by
# cmake/draw_libraries.cmake. Other programs and tests stay desktop only.
#
# The first seven, nfsu_image to nfsu_render, were moved here from
# CMakeLists.txt as they were, comments and all. nfsu_texture_export is
# among them although the stock frame calls none of it: nfsu_render links it
# for the POSIX frame writer's rollback, and the POSIX writer is the one the
# console compiles. Moving them changes the order in which the desktop
# configure declares its targets and nothing else: no source list, option,
# link or dependency of any target differs. The neutrality gate's manifest
# lists the libraries in declaration order, and the gate reads that list as
# a set. nfsu_det_sqrt, declared between nfsu_texture_decode and nfsu_render
# below, is not one of the seven: issue #165 added it here, new, rather than
# moving it from anywhere.
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

# Data-driven car selection over the car type table (issue #315): resolves a
# playable car's name to its own retail asset paths and classification. It
# reads only the GLOBALB.BUN it is pointed at, with the same std::ifstream
# loaders nfsu_core owns, so the console compiles it from this list too.
add_library(nfsu_car_assets
    src/car_assets.cpp
)

target_include_directories(nfsu_car_assets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_car_assets PUBLIC cxx_std_17)
target_link_libraries(nfsu_car_assets PUBLIC nfsu_core)
nfsu_enable_warnings(nfsu_car_assets)

target_include_directories(nfsu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nfsu_core PUBLIC cxx_std_17)
target_link_libraries(nfsu_core PUBLIC nfsu_image)
nfsu_enable_warnings(nfsu_core)
# The project-wide contraction rule also covers issue #91: devkitA64 GCC 16.1.0 fuses
# seven multiply-adds in src/part_placement.cpp, in
# derive_cullable_part_placements and multiply_placement_matrix, which place
# the wheels, brakes, exhaust and plate of the stock frame (issue #83,
# docs/SWITCH_BUILD.md). No difference in the pinned frame has been observed
# from that fusion on either platform, but this repository's determinism
# claims are byte-identity claims, and an unpinned target holds one by
# accident rather than by construction, so cmake/floating_point.cmake
# disables contraction for every target.

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
    src/car_shading.cpp
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
# The same project-wide rule covers issue #91 here, for the same reason as
# nfsu_core just above: src/stock_scene.cpp builds the wheel and brake
# instance transforms from the placements nfsu_core derives, so a fused
# multiply-add here would sit next to the ones issue #83 measured in
# src/part_placement.cpp rather than instead of them.

# Texture-pack decoding for prepared scenes. Backends do not depend on it.
add_library(nfsu_texture_decode
    src/texture_decode.cpp
)

target_link_libraries(nfsu_texture_decode PUBLIC nfsu_scene nfsu_core)
nfsu_enable_warnings(nfsu_texture_decode)

# The correctly rounded double-precision square root (issue #165, moved out
# of nfsu_render, which used to define it): one hardware rounding to
# binary64, docs/RENDERER.md "Determinism" says why the library's own sqrt
# cannot be trusted for it at -O0 and -Os. include/nfsu/det_sqrt.h is the
# only header its callers need; nfsu_render's public header, render.h,
# includes it too, so every earlier `#include "nfsu/render.h"` still finds
# nfsu::det_sqrt. Its own source picks an intrinsic header and an inline-asm
# path by architecture (see src/det_sqrt.cpp), which is why it is classified
# platform code (NFSU_PLATFORM_LIBRARIES in CMakeLists.txt) rather than
# neutral, although it calls no operating system: the neutrality gate's
# include rule does not distinguish an operating-system header from a
# compiler-intrinsics one. Declared here, ahead of nfsu_render and
# nfsu_input below, which both link it; nfsu_track_contact links it too,
# from cmake/vehicle_track_libraries.cmake on both platforms.
add_library(nfsu_det_sqrt
    src/det_sqrt.cpp
)

target_include_directories(nfsu_det_sqrt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_det_sqrt PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_det_sqrt)

# Deterministic CPU reference renderer. Floating-point contraction is
# disabled so that results do not depend on FMA availability.
add_library(nfsu_render
    src/render.cpp
)

# PUBLIC: render.h declares det_sqrt (via nfsu/det_sqrt.h) and callers that
# link nfsu_render publicly - nfsu_track_render, and nfsu_track_probe and
# nfsu_stock_view above it - resolve the symbol through this link, as they
# already did when det_sqrt lived in nfsu_render itself.
target_link_libraries(nfsu_render PUBLIC nfsu_scene nfsu_image nfsu_det_sqrt)
# The POSIX frame writer rolls a failed write back with
# rollback_created_files, which nfsu_texture_export now defines
# (src/texture_export.cpp; issue #139 split it out of nfsu_core, which used
# to hold it in src/texture.cpp). The link is private because render.h
# exposes nothing of nfsu_texture_export. The Windows writer removes by
# handle and never calls it, which is why only a POSIX build found the link
# missing (issue #132).
target_link_libraries(nfsu_render PRIVATE nfsu_texture_export)
nfsu_enable_warnings(nfsu_render)

# SHA-256 (FIPS 180-4) over bytes in memory. docs/ pins every reference
# frame by the SHA-256 of its TGA file, printed by a host tool, and the
# Switch application writes no file, so it needs a digest of its own to
# report the same number. Standard library and integer arithmetic only;
# it still inherits the global floating-point policy.
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
# own - it hands the options to nfsu_render. Both inherit the global policy.
# docs/SWITCH_BUILD.md.
add_library(nfsu_stock_frame
    src/stock_frame.cpp
)

target_link_libraries(nfsu_stock_frame
    PUBLIC nfsu_stock_scene nfsu_texture_decode nfsu_render nfsu_sha256
           nfsu_runtime_skin nfsu_car_assets)
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

# The runtime body skin and the controller camera (issue #148). The Switch
# application composes the body skin the way nfsu_viewer's --skin-car does
# and orbits its camera with the controller model of docs/INPUT.md, so the
# console builds these too. The blocks below, down to nfsu_input_camera,
# were moved here from CMakeLists.txt as they were, comments and all:
# nfsu_body_skin, nfsu_skin_inputs and nfsu_runtime_skin from beside
# nfsu_probe, nfsu_inspect from above the desktop window, and nfsu_inspect's
# floating-point options, nfsu_input and nfsu_input_camera from the
# platform services block. As for the first seven above, no source list,
# option, link or dependency of any target differs on the desktop; only the
# declaration order does.

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
# nfsu_body_skin; both inherit the global policy.
add_library(nfsu_runtime_skin
    src/runtime_skin.cpp
)

target_link_libraries(nfsu_runtime_skin PUBLIC nfsu_skin_inputs nfsu_scene)
nfsu_enable_warnings(nfsu_runtime_skin)

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

# nfsu_inspect's orbit, pan and zoom decide the camera a recorded
# controller session replays to (docs/INPUT.md), so contraction is disabled
# there for the reason nfsu_render disables it. Measured, not assumed: GCC
# 13 for AArch64 at -O3 fuses three multiply-adds into Inspector::orbit,
# which fails nfsu_inspect_tests' "the orbit is reversible" and moves the
# replayed session's per-frame digest; with these options both pass. x86-64
# without FMA has nothing to fuse, so its builds are unchanged. Measured
# once, in the worker's cloud session for this branch, cross-compiling and
# running under QEMU there; not reproduced on the project's own toolchain,
# and nothing here has run on a console.

# The controller model: buttons, sticks, dead zones, the remapping table,
# the rules that turn frames into actions and the recorded text form of a
# sequence of frames. Standard library and the correctly rounded det_sqrt
# only, and no clock: a frame carries its own time. The dead zone's one
# square root used to be std::sqrt, which is not correctly rounded at -O0
# (issue #165, the same MinGW libm defect issue #125 found in the
# renderer); it now calls det_sqrt through nfsu/det_sqrt.h, which is a
# project header, and is linked below rather than exposed - input.h states
# nothing of it. Floating-point contraction is disabled because a recording
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
target_link_libraries(nfsu_input PRIVATE nfsu_det_sqrt)
nfsu_enable_warnings(nfsu_input)

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

# The stock car orbited on the console (issue #148): the application's
# control table - the left stick orbits, - resets the camera, + exits -
# over nfsu_input_camera's GamepadCamera, the Inspector camera it moves,
# the check that the camera is the one the pinned frame was rendered from,
# and the render at that camera. It reads no clock: a controller frame
# carries its own time. It does no floating-point arithmetic of its own -
# the camera's is nfsu_inspect's and nfsu_input_camera's, the render's
# nfsu_render's. All inherit the global policy.
# docs/SWITCH_BUILD.md.
add_library(nfsu_stock_view
    src/stock_view.cpp
)

target_link_libraries(nfsu_stock_view PUBLIC nfsu_input_camera nfsu_render)
nfsu_enable_warnings(nfsu_stock_view)

# SDK-free whole-frame acceptance metrics and amplified RGBA8 difference
# for issue #147. Standard library only; no clock, file or device access.
add_library(nfsu_frame_compare src/frame_compare.cpp)
target_include_directories(nfsu_frame_compare PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_frame_compare PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_frame_compare)

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
        nfsu_image nfsu_core nfsu_car_assets nfsu_texture_export nfsu_scene nfsu_stock_scene
        nfsu_texture_decode nfsu_render nfsu_sha256 nfsu_stock_frame
        nfsu_screen_text nfsu_frame_compare nfsu_body_skin nfsu_skin_inputs nfsu_runtime_skin
        nfsu_inspect nfsu_input nfsu_input_camera nfsu_stock_view
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
