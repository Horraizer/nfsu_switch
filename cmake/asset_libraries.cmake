# The retail asset manifest, its check and the console's file system (issue
# #47), declared once for both configures: the top-level CMakeLists.txt
# includes this file after cmake/residency_libraries.cmake and before the
# desktop and Switch configures part, so the check nfsu_probe asset-check
# runs on the desktop is compiled from the same source, with the same
# options, as the one nfsu_switch_assets runs on the console. See
# docs/SWITCH_ASSETS.md.
#
# nfsu_assets: which retail files a feature needs - names, sizes and SHA-256
# hashes, never the files - and the read-only check that finds each one under
# a root through the FileSystemService of include/nfsu/platform.h, matching
# names without regard to ASCII case and hashing in bounded chunks; the
# console's config file and the report as lines. It calls no operating
# system and reads no clock: every file access goes through the service it
# is handed, which is what makes it neutral (the top-level CMakeLists.txt
# lists it). It uses the interface only, so it needs the header and not the
# nfsu_platform library, which the console does not build. It does no
# floating-point arithmetic, but still inherits the global policy.
add_library(nfsu_assets
    src/assets.cpp
)

target_include_directories(nfsu_assets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_assets PUBLIC cxx_std_17)
target_link_libraries(nfsu_assets PUBLIC nfsu_sha256)
nfsu_enable_warnings(nfsu_assets)

# nfsu_posix_file_system: the FileSystemService of the console applications.
# Platform code on purpose: it calls opendir, readdir, closedir and stat,
# which newlib provides and libnx routes to the SD card, and reads with
# std::ifstream. The desktop builds it too, only so that nfsu_asset_tests can
# run it over real directories; the desktop programs use nfsu_arg_path's
# service instead.
add_library(nfsu_posix_file_system
    src/posix_file_system.cpp
)

target_include_directories(nfsu_posix_file_system
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nfsu_posix_file_system PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_posix_file_system)

# gnu++17 on the console, as for the car-path libraries; the comment at the
# end of cmake/car_libraries.cmake says why.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_assets nfsu_posix_file_system
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
