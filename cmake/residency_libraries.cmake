# All floating-point flags below are inherited from the project-wide rule
# in cmake/floating_point.cmake (issue #175); no target owns a separate pin.

# The section streamer of issue #157, declared once for both configures: the
# top-level CMakeLists.txt includes this file after cmake/car_libraries.cmake,
# whose nfsu_sha256 it links, and before the desktop and Switch configures
# part, so the console compiles src/track_residency.cpp from the same source
# list and with the same floating-point options as the desktop.
#
# What it is: speed.exe's streamer - the zone key a car's look-ahead picks,
# the request it rebuilds when that key changes, and the placements and
# releases that keep the wanted sections within a byte budget - as a
# deterministic function of a streaming table, a zone report, a cost per
# record and one position per fixed step. It reads no file and no clock and
# calls no library mathematics: its square roots are its own, correctly
# rounded in integer arithmetic (review comment 3630, F1). See
# docs/TRACK_STREAMING.md.
#
# The project-wide rule disables contraction, including nfsu_core (issue #91):
# every zone the look-ahead picks and every sample of a path is a comparison
# of a float the arithmetic produced, the desktop's x86-64 target has no FMA
# to fuse with, and devkitA64's GCC fuses a multiply-add at -O2 unless told
# not to. The top-level CMakeLists.txt registers the whole-tree check through
# cmake/switch_fma_census.cmake after all Switch targets are declared.
#
# On the desktop the top-level CMakeLists.txt also links nfsu_track_stream,
# whose table lookup and zone test it calls. The console builds neither of
# those yet, so there this library is compiled - and censused - but nothing
# links it; a console program that does will need nfsu_track_stream and
# nfsu_track_zones built there too.
add_library(nfsu_track_residency
    src/track_residency.cpp
)

target_include_directories(nfsu_track_residency
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_track_residency PUBLIC cxx_std_17)
target_link_libraries(nfsu_track_residency PUBLIC nfsu_sha256)
nfsu_enable_warnings(nfsu_track_residency)

# gnu++17 on the console, as for the car-path libraries; the comment at the
# end of cmake/car_libraries.cmake says why.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_track_residency
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
