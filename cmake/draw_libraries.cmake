# All floating-point flags below are inherited from the project-wide rule
# in cmake/floating_point.cmake (issue #175); no target owns a separate pin.

# Shared neutral draw-recording dependencies, built on desktop and Switch.

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

add_custom_target(nfsu_shader_manifest DEPENDS ${NFSU_SHADER_MANIFEST_HEADER})
add_library(nfsu_draw_commands src/draw_commands.cpp src/draw_execution.cpp ${NFSU_SHADER_MANIFEST_HEADER})
target_link_libraries(nfsu_draw_commands PUBLIC nfsu_gpu_upload nfsu_render)
target_include_directories(nfsu_draw_commands PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies(nfsu_draw_commands nfsu_shader_manifest)
nfsu_enable_warnings(nfsu_draw_commands)
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_track nfsu_track_visibility nfsu_track_draw_list
        nfsu_memory nfsu_gpu_upload nfsu_draw_commands
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
