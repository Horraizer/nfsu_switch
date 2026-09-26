# The vehicle model of issue #58, declared once for both configures: the
# top-level CMakeLists.txt includes this file before the desktop and Switch
# configures part, as it includes cmake/car_libraries.cmake, so the console
# compiles the model from the same source list with the same floating-point
# options as the desktop. It must come after cmake/car_libraries.cmake,
# which declares nfsu_sha256.
#
# nfsu_vehicle: the fixed-step car of docs/VEHICLE.md - the recovered
# handling fields of a CarTypeInfo record, the rigid body on its struts, the
# tyres, the engine and gearbox, the per-step telemetry and its SHA-256, and
# the scripted input sequences the probe and the tests drive it with. It
# reads no file, opens no window and reads no clock: the caller supplies the
# record bytes, the ground and the input, and a step is a fixed 1/120 s.
#
# Floating-point contraction is disabled for the reason every library whose
# output is pinned byte for byte disables it: the telemetry hash of a
# scripted run is pinned in docs/VEHICLE.md and must be the same bits on the
# desktop and on the console, and devkitA64's GCC fuses multiply-adds at -O2
# where the desktop's x86-64 target has no FMA to fuse with. The fused
# instruction count of this library's Switch objects, with and without these
# options, is in docs/VEHICLE.md.
add_library(nfsu_vehicle
    src/vehicle.cpp
    src/vehicle_script.cpp
)

target_include_directories(nfsu_vehicle
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_vehicle PUBLIC cxx_std_17)
target_link_libraries(nfsu_vehicle PUBLIC nfsu_sha256)
nfsu_enable_warnings(nfsu_vehicle)
if(MSVC)
    target_compile_options(nfsu_vehicle PRIVATE /fp:precise)
else()
    target_compile_options(nfsu_vehicle PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# gnu++17 on the console, as for the car-path libraries; the comment at the
# end of cmake/car_libraries.cmake says why it is named there.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_vehicle
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
