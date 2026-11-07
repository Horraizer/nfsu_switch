# CMake toolchain file for the Nintendo Switch build of this project.
#
#   cmake -S . -B build-switch -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake
#
# It is opt-in: a configure that does not name it never reads it, and the
# desktop build is exactly what it was. docs/SWITCH_BUILD.md has the full
# commands, the prerequisites and what is not covered yet.
#
# It locates DEVKITPRO and DEVKITA64 (cmake/switch-sdk.cmake), then hands
# over to devkitPro's own toolchain file, which sets the platform, the
# compiler flags and the helpers (nx_create_nro, nx_generate_nacp) that
# switch/CMakeLists.txt uses. The top-level CMakeLists.txt checks the same
# SDK before project() runs, so in this project a missing piece is reported
# there, once; the check below is for any other project that uses this file,
# and for the try_compile projects CMake runs while detecting the compiler.

include("${CMAKE_CURRENT_LIST_DIR}/switch-sdk.cmake")
nfsu_switch_sdk_locate(_nfsu_switch_sdk_problem)
if(NOT _nfsu_switch_sdk_problem STREQUAL "")
    message(FATAL_ERROR "${_nfsu_switch_sdk_problem}")
endif()

# devkitPro's toolchain finds every tool under DEVKITPRO/devkitA64 and only
# searches for a tool whose variable is unset, so setting them here from
# DEVKITA64 is what makes that directory the one the build uses. Changing
# either root takes a fresh build directory, as changing a compiler always
# does in CMake.
set(CMAKE_C_COMPILER "${NFSU_SWITCH_TOOL_GCC}" CACHE FILEPATH "devkitA64 C compiler")
set(CMAKE_CXX_COMPILER "${NFSU_SWITCH_TOOL_GXX}" CACHE FILEPATH "devkitA64 C++ compiler")
set(CMAKE_ASM_COMPILER "${NFSU_SWITCH_TOOL_GCC}" CACHE FILEPATH "devkitA64 assembler driver")
set(CMAKE_AR "${NFSU_SWITCH_TOOL_GCC_AR}" CACHE FILEPATH "devkitA64 archiver")
set(CMAKE_RANLIB "${NFSU_SWITCH_TOOL_GCC_RANLIB}" CACHE FILEPATH "devkitA64 archive indexer")
set(CMAKE_LINKER "${NFSU_SWITCH_TOOL_LD}" CACHE FILEPATH "devkitA64 linker")
set(CMAKE_STRIP "${NFSU_SWITCH_TOOL_STRIP}" CACHE FILEPATH "devkitA64 strip tool")
set(DKP_OBJCOPY "${NFSU_SWITCH_TOOL_OBJCOPY}" CACHE FILEPATH "devkitA64 objcopy")
set(DKP_NM "${NFSU_SWITCH_TOOL_NM}" CACHE FILEPATH "devkitA64 symbol lister")

# libnx's link specs find the linker script through the DEVKITPRO
# environment variable when the link runs (switch.specs:
# %:getenv(DEVKITPRO /libnx/switch.ld)), not when CMake configures. Every
# link therefore runs with DEVKITPRO set to the root checked above, so a
# build needs nothing exported and cannot link against a different SDK than
# the one this configure checked. Linker launchers need CMake 3.21, which
# cmake/switch-sdk.cmake requires of a Switch configure.
set(CMAKE_C_LINKER_LAUNCHER "${CMAKE_COMMAND}" -E env "DEVKITPRO=${NFSU_SWITCH_DEVKITPRO}")
set(CMAKE_CXX_LINKER_LAUNCHER "${CMAKE_COMMAND}" -E env "DEVKITPRO=${NFSU_SWITCH_DEVKITPRO}")

include("${NFSU_SWITCH_DEVKITPRO}/cmake/Switch.cmake")
