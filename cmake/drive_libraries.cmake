# The driving controls and the chase camera of issue #59, declared once for
# both configures: the top-level CMakeLists.txt includes this file before the
# desktop and Switch configures part, after cmake/car_libraries.cmake (which
# declares nfsu_input, nfsu_render, nfsu_scene and nfsu_sha256) and
# cmake/vehicle_libraries.cmake (nfsu_vehicle), so the console compiles these
# from the same source lists with the same floating-point options as the
# desktop. docs/CONTROLS_CAMERA.md.
#
# nfsu_drive_controls: a controller frame to one VehicleInput per vehicle
# step - the remapping table and its ASCII file, per-axis calibration, the
# throttle shaping, the speed-sensitive steering filter and the reverse
# assist.
#
# nfsu_chase_camera: the recovered chase camera - the per-car anchors of
# CarTypeInfo +0xB50, the target heading, the lag spring, the field of view -
# and its explicit approximations, camera collision among them, over a world
# the caller supplies.
#
# nfsu_drive_session: one controller frame through the controls, two vehicle
# steps and the camera, hashed; the built-in demonstration recording; and
# what the CPU reference renderer needs to show it - the renderer's camera,
# the car placed at its pose and a flat ground.
#
# None reads a file, a clock or the environment, and none calls an operating
# system: the caller supplies the frames, the record bytes and the world.
#
# Floating-point contraction is disabled on all three for the reason every
# library whose output is pinned disables it: the demonstration's hash is
# pinned in docs/CONTROLS_CAMERA.md and must be the same bits on the desktop
# and on the console, and devkitA64's GCC fuses multiply-adds at -O2 where
# the desktop's x86-64 target has no FMA to fuse with. The Switch tree
# disassembles their objects and fails on any fused instruction
# (nfsu_switch_drive_fma_census).
add_library(nfsu_drive_controls
    src/drive_controls.cpp
)
target_link_libraries(nfsu_drive_controls PUBLIC nfsu_input nfsu_vehicle)

add_library(nfsu_chase_camera
    src/chase_camera.cpp
)
target_link_libraries(nfsu_chase_camera PUBLIC nfsu_vehicle)

add_library(nfsu_drive_session
    src/drive_session.cpp
)
target_link_libraries(nfsu_drive_session
    PUBLIC nfsu_drive_controls nfsu_chase_camera nfsu_render nfsu_scene nfsu_sha256)

foreach(target IN ITEMS nfsu_drive_controls nfsu_chase_camera nfsu_drive_session)
    target_include_directories(${target}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(${target} PUBLIC cxx_std_17)
    nfsu_enable_warnings(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    # gnu++17 on the console, as for the car-path libraries; the comment at
    # the end of cmake/car_libraries.cmake says why it is named there.
    if(NINTENDO_SWITCH)
        set_target_properties(${target}
            PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
    endif()
endforeach()
